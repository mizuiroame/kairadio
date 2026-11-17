#include "radio.h"

#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <switch.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>

#include "ring.h"
#include "stream.h"

#define RING_BYTES (1u << 20)        // About a minute at 128 kbit/s.
#define PREBUFFER_SECONDS 3
#define PREBUFFER_MIN (16u * 1024)
#define PREBUFFER_DEFAULT (48u * 1024)
#define AVIO_BUFFER 16384
#define PROBE_BYTES (64 * 1024)
#define READ_WAIT_MS 100
#define BUFFER_COUNT 4
#define BUFFER_FRAMES 2048
#define BUFFER_BYTES (BUFFER_FRAMES * 2 * sizeof(s16))
#define BUFFER_ALIGN 0x1000
#define PCM_FRAMES (BUFFER_FRAMES * 8)  // Decoded audio waiting for a buffer.
#define WAIT_TIMEOUT_NS 20000000ull
#define IDLE_SLEEP_NS 10000000ull
#define THREAD_STACK_SIZE 0x20000
#define THREAD_PRIORITY 0x2B

// Shared with the UI thread: guarded by g_lock.
static Mutex g_lock;
static bool g_has_station;
static Station g_station;
static bool g_play_request;   // Start g_station.
static bool g_stop_request;
static bool g_playing;        // A station is meant to be playing.
static bool g_buffering;
static bool g_decoding;       // The decoder has produced audio.
static char g_codec[16];
static unsigned g_sample_rate;
static char g_error[160];

// Owned by the audio thread.
static Thread g_thread;
static volatile bool g_running;
static volatile bool g_abort;  // Makes a blocked read give up (a new command came).
static Ring g_ring;
static size_t g_prebuffer;
static bool g_started;         // The decoder has been opened for this station.
static AVFormatContext *g_format;
static AVIOContext *g_avio;
static AVCodecContext *g_codec_ctx;
static SwrContext *g_swr;
static int g_stream_index = -1;
static AVPacket *g_packet;
static AVFrame *g_frame;
static int g_swr_rate, g_swr_format;
static AVChannelLayout g_swr_layout;
static u32 g_out_rate;
static s16 g_pcm[PCM_FRAMES * 2];
static size_t g_pcm_frames;
static AudioOutBuffer g_buffers[BUFFER_COUNT];
static bool g_queued[BUFFER_COUNT];

const char *radioStateLabel(RadioState state)
{
    switch (state) {
    case RADIO_CONNECTING: return "Connecting\xE2\x80\xA6";
    case RADIO_BUFFERING: return "Buffering\xE2\x80\xA6";
    case RADIO_PLAYING: return "Live";
    case RADIO_RECONNECTING: return "Reconnecting\xE2\x80\xA6";
    case RADIO_FAILED: return "Can't play";
    default: return "Stopped";
    }
}

static void setError(const char *message)
{
    mutexLock(&g_lock);
    snprintf(g_error, sizeof(g_error), "%s", message);
    g_playing = false;
    mutexUnlock(&g_lock);
}

// --- Reading the ring from FFmpeg ---

static void setBuffering(bool on)
{
    mutexLock(&g_lock);
    g_buffering = on;
    mutexUnlock(&g_lock);
}

static int readPacket(void *opaque, uint8_t *buf, int size)
{
    (void)opaque;
    // An empty ring after playback started: wait to refill a few seconds
    // rather than stutter on every packet.
    if (g_started && ringFill(&g_ring) == 0 && !ringFinished(&g_ring)) {
        setBuffering(true);
        while (!g_abort && ringFill(&g_ring) < g_prebuffer && !ringFinished(&g_ring))
            svcSleepThread(50000000ull);
    }
    while (!g_abort) {
        size_t n = ringRead(&g_ring, buf, (size_t)size, READ_WAIT_MS);
        if (n > 0) {
            setBuffering(false);
            return (int)n;
        }
        if (ringFinished(&g_ring))
            return AVERROR_EOF;
    }
    return AVERROR_EXIT;
}

static int interrupted(void *opaque)
{
    (void)opaque;
    return g_abort;
}

// --- The decoder ---

static const char *formatFor(const char *content_type)
{
    static const struct {
        const char *type;
        const char *format;
    } TYPES[] = {
        {"audio/mpeg", "mp3"},       {"audio/mp3", "mp3"},        {"audio/aac", "aac"},
        {"audio/aacp", "aac"},       {"audio/x-aac", "aac"},      {"audio/ogg", "ogg"},
        {"application/ogg", "ogg"},  {"audio/opus", "ogg"},       {"audio/flac", "flac"},
        {"video/mp2t", "mpegts"},    {"audio/mp2t", "mpegts"},
    };
    for (size_t i = 0; i < sizeof(TYPES) / sizeof(TYPES[0]); i++)
        if (strncasecmp(content_type, TYPES[i].type, strlen(TYPES[i].type)) == 0)
            return TYPES[i].format;
    return NULL;
}

static void codecLabel(const AVCodecContext *c, char *out, size_t size)
{
    const char *name;
    switch (c->codec_id) {
    case AV_CODEC_ID_MP3: name = "MP3"; break;
    case AV_CODEC_ID_AAC:
    case AV_CODEC_ID_AAC_LATM:
        name = c->profile == AV_PROFILE_AAC_HE || c->profile == AV_PROFILE_AAC_HE_V2 ? "HE-AAC" : "AAC";
        break;
    case AV_CODEC_ID_OPUS: name = "Opus"; break;
    case AV_CODEC_ID_VORBIS: name = "Vorbis"; break;
    case AV_CODEC_ID_FLAC: name = "FLAC"; break;
    default: name = avcodec_get_name(c->codec_id); break;
    }
    snprintf(out, size, "%s", name);
}

static void closeDecoder(void)
{
    swr_free(&g_swr);
    av_channel_layout_uninit(&g_swr_layout);
    avcodec_free_context(&g_codec_ctx);
    if (g_format)
        avformat_close_input(&g_format);
    if (g_avio) {
        av_freep(&g_avio->buffer);
        avio_context_free(&g_avio);
    }
    g_stream_index = -1;
    g_started = false;
    g_pcm_frames = 0;
}

static bool openDecoder(const char *content_type)
{
    uint8_t *buffer = av_malloc(AVIO_BUFFER);
    g_avio = buffer ? avio_alloc_context(buffer, AVIO_BUFFER, 0, NULL, readPacket, NULL, NULL) : NULL;
    g_format = avformat_alloc_context();
    if (!g_avio || !g_format) {
        if (!g_avio)
            av_free(buffer);
        return false;
    }
    g_format->pb = g_avio;
    g_format->flags |= AVFMT_FLAG_CUSTOM_IO;
    g_format->interrupt_callback = (AVIOInterruptCB){interrupted, NULL};
    g_format->probesize = PROBE_BYTES;
    g_format->max_analyze_duration = AV_TIME_BASE / 2;
    const AVInputFormat *hint = formatFor(content_type) ? av_find_input_format(formatFor(content_type)) : NULL;
    if (avformat_open_input(&g_format, NULL, hint, NULL) < 0)
        return false;  // avformat_open_input freed the context.
    if (avformat_find_stream_info(g_format, NULL) < 0)
        return false;
    const AVCodec *codec = NULL;
    g_stream_index = av_find_best_stream(g_format, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (g_stream_index < 0 || !codec)
        return false;
    g_codec_ctx = avcodec_alloc_context3(codec);
    if (!g_codec_ctx ||
        avcodec_parameters_to_context(g_codec_ctx, g_format->streams[g_stream_index]->codecpar) < 0 ||
        avcodec_open2(g_codec_ctx, codec, NULL) < 0)
        return false;
    mutexLock(&g_lock);
    codecLabel(g_codec_ctx, g_codec, sizeof(g_codec));
    mutexUnlock(&g_lock);
    g_started = true;
    return true;
}

// Makes the resampler match the frame (HE-AAC can change rate after the
// first frames). False if it can't be set up.
static bool prepareResampler(const AVFrame *f)
{
    if (g_swr && f->sample_rate == g_swr_rate && f->format == g_swr_format &&
        av_channel_layout_compare(&f->ch_layout, &g_swr_layout) == 0)
        return true;
    swr_free(&g_swr);
    av_channel_layout_uninit(&g_swr_layout);
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&g_swr, &stereo, AV_SAMPLE_FMT_S16, (int)g_out_rate, &f->ch_layout, f->format,
                            f->sample_rate, 0, NULL) < 0 ||
        swr_init(g_swr) < 0) {
        swr_free(&g_swr);
        return false;
    }
    g_swr_rate = f->sample_rate;
    g_swr_format = f->format;
    av_channel_layout_copy(&g_swr_layout, &f->ch_layout);
    mutexLock(&g_lock);
    g_sample_rate = (unsigned)f->sample_rate;
    mutexUnlock(&g_lock);
    return true;
}

static bool convertFrame(const AVFrame *f)
{
    if (!prepareResampler(f))
        return false;
    int room = (int)(PCM_FRAMES - g_pcm_frames);
    uint8_t *out = (uint8_t *)(g_pcm + g_pcm_frames * 2);
    int n = swr_convert(g_swr, &out, room, (const uint8_t **)f->extended_data, f->nb_samples);
    if (n > 0)
        g_pcm_frames += (size_t)n;
    return n >= 0;
}

// Decodes until there is a buffer's worth of audio. False at the end of the
// stream or on an error that stops playback.
static bool decodeMore(void)
{
    while (g_pcm_frames < BUFFER_FRAMES && !g_abort) {
        int rc = avcodec_receive_frame(g_codec_ctx, g_frame);
        if (rc == 0) {
            bool ok = convertFrame(g_frame);
            av_frame_unref(g_frame);
            if (!ok)
                return false;
            mutexLock(&g_lock);
            g_decoding = true;
            mutexUnlock(&g_lock);
            continue;
        }
        if (rc != AVERROR(EAGAIN))
            return false;
        rc = av_read_frame(g_format, g_packet);
        if (rc < 0)
            return false;
        if (g_packet->stream_index == g_stream_index) {
            // A damaged packet is skipped; the stream goes on.
            avcodec_send_packet(g_codec_ctx, g_packet);
        }
        av_packet_unref(g_packet);
    }
    return g_pcm_frames > 0;
}

// --- Output ---

static bool fillBuffer(AudioOutBuffer *buffer)
{
    if (g_pcm_frames < BUFFER_FRAMES && !decodeMore())
        return false;
    size_t frames = g_pcm_frames < BUFFER_FRAMES ? g_pcm_frames : BUFFER_FRAMES;
    s16 *out = buffer->buffer;
    memcpy(out, g_pcm, frames * 2 * sizeof(s16));
    memset(out + frames * 2, 0, (BUFFER_FRAMES - frames) * 2 * sizeof(s16));
    memmove(g_pcm, g_pcm + frames * 2, (g_pcm_frames - frames) * 2 * sizeof(s16));
    g_pcm_frames -= frames;
    buffer->data_size = BUFFER_BYTES;
    return true;
}

static int queuedCount(void)
{
    int n = 0;
    for (int i = 0; i < BUFFER_COUNT; i++)
        n += g_queued[i];
    return n;
}

static void reapBuffers(void)
{
    if (queuedCount() == 0) {
        svcSleepThread(IDLE_SLEEP_NS);
        return;
    }
    AudioOutBuffer *released = NULL;
    u32 count = 0;
    if (R_FAILED(audoutWaitPlayFinish(&released, &count, WAIT_TIMEOUT_NS)) || count == 0)
        return;
    for (int i = 0; i < BUFFER_COUNT; i++)
        if (released == &g_buffers[i])
            g_queued[i] = false;
}

static void flushOutput(void)
{
    bool flushed;
    audoutFlushAudioOutBuffers(&flushed);
    for (int i = 0; i < BUFFER_COUNT; i++)
        g_queued[i] = false;
}

// --- The audio thread ---

static void stopPlayback(void)
{
    streamStop();
    closeDecoder();
    flushOutput();
    mutexLock(&g_lock);
    g_buffering = false;
    g_decoding = false;
    mutexUnlock(&g_lock);
}

static void startPlayback(void)
{
    stopPlayback();
    mutexLock(&g_lock);
    char url[LINK_URL_MAX];
    snprintf(url, sizeof(url), "%s", g_station.url);
    int bitrate = g_station.bitrate;
    g_codec[0] = '\0';
    g_sample_rate = 0;
    g_error[0] = '\0';
    g_playing = true;
    mutexUnlock(&g_lock);
    size_t per_second = bitrate > 0 ? (size_t)bitrate * 1000 / 8 : PREBUFFER_DEFAULT / PREBUFFER_SECONDS;
    g_prebuffer = per_second * PREBUFFER_SECONDS;
    if (g_prebuffer < PREBUFFER_MIN)
        g_prebuffer = PREBUFFER_MIN;
    if (g_prebuffer > RING_BYTES / 2)
        g_prebuffer = RING_BYTES / 2;
    streamStart(&g_ring, url);
}

static void handleCommands(void)
{
    mutexLock(&g_lock);
    bool play = g_play_request, stop = g_stop_request;
    g_play_request = g_stop_request = false;
    mutexUnlock(&g_lock);
    g_abort = false;
    if (play)
        startPlayback();
    else if (stop) {
        stopPlayback();
        mutexLock(&g_lock);
        g_playing = false;
        mutexUnlock(&g_lock);
    }
}

static bool isPlaying(void)
{
    mutexLock(&g_lock);
    bool playing = g_playing;
    mutexUnlock(&g_lock);
    return playing;
}

static void playStep(void)
{
    if (!g_started) {
        StreamInfo info;
        streamGetInfo(&info);
        if (info.state == STREAM_FAILED) {
            stopPlayback();
            setError(info.error);
            return;
        }
        if (ringFill(&g_ring) < g_prebuffer && !ringFinished(&g_ring)) {
            svcSleepThread(IDLE_SLEEP_NS * 5);
            return;
        }
        streamGetInfo(&info);
        if (!openDecoder(info.content_type)) {
            if (g_abort)
                return;
            streamGetInfo(&info);
            stopPlayback();
            setError(info.state == STREAM_FAILED ? info.error : "This station's audio format isn't supported.");
            return;
        }
    }
    for (int i = 0; i < BUFFER_COUNT && !g_abort; i++) {
        if (g_queued[i])
            continue;
        if (!fillBuffer(&g_buffers[i])) {
            if (g_abort || queuedCount() > 0)
                break;  // Let what's queued play out first.
            StreamInfo info;
            streamGetInfo(&info);
            stopPlayback();
            setError(info.state == STREAM_FAILED ? info.error :
                     info.state == STREAM_ENDED  ? "The station ended the broadcast." :
                                                   "The station's audio stopped.");
            return;
        }
        if (R_SUCCEEDED(audoutAppendAudioOutBuffer(&g_buffers[i])))
            g_queued[i] = true;
    }
    reapBuffers();
}

static void audioThreadMain(void *arg)
{
    (void)arg;
    while (g_running) {
        handleCommands();
        if (isPlaying())
            playStep();
        else
            reapBuffers();
    }
    stopPlayback();
}

static bool allocateBuffers(void)
{
    for (int i = 0; i < BUFFER_COUNT; i++) {
        void *memory = memalign(BUFFER_ALIGN, BUFFER_BYTES);
        if (!memory)
            return false;
        memset(memory, 0, BUFFER_BYTES);
        g_buffers[i] = (AudioOutBuffer){
            .next = NULL,
            .buffer = memory,
            .buffer_size = BUFFER_BYTES,
            .data_size = BUFFER_BYTES,
            .data_offset = 0,
        };
    }
    return true;
}

static void freeBuffers(void)
{
    for (int i = 0; i < BUFFER_COUNT; i++) {
        free(g_buffers[i].buffer);
        g_buffers[i].buffer = NULL;
    }
}

bool radioInit(void)
{
    mutexInit(&g_lock);
    g_packet = av_packet_alloc();
    g_frame = av_frame_alloc();
    if (!g_packet || !g_frame || !ringInit(&g_ring, RING_BYTES))
        return false;
    if (R_FAILED(audoutInitialize()))
        return false;
    if (audoutGetChannelCount() != 2 || audoutGetPcmFormat() != PcmFormat_Int16 || !allocateBuffers() ||
        R_FAILED(audoutStartAudioOut())) {
        freeBuffers();
        audoutExit();
        return false;
    }
    g_out_rate = audoutGetSampleRate();
    g_running = true;
    if (R_FAILED(threadCreate(&g_thread, audioThreadMain, NULL, NULL, THREAD_STACK_SIZE, THREAD_PRIORITY, -2)) ||
        R_FAILED(threadStart(&g_thread))) {
        g_running = false;
        audoutStopAudioOut();
        freeBuffers();
        audoutExit();
        return false;
    }
    return true;
}

void radioExit(void)
{
    g_running = false;
    g_abort = true;
    ringCancel(&g_ring);
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    audoutStopAudioOut();
    flushOutput();
    freeBuffers();
    audoutExit();
    ringFree(&g_ring);
    av_packet_free(&g_packet);
    av_frame_free(&g_frame);
}

void radioPlay(const Station *station)
{
    mutexLock(&g_lock);
    g_station = *station;
    g_has_station = true;
    g_play_request = true;
    g_stop_request = false;
    g_playing = true;
    g_error[0] = '\0';
    g_codec[0] = '\0';
    g_decoding = false;
    mutexUnlock(&g_lock);
    g_abort = true;
    ringCancel(&g_ring);
}

void radioStop(void)
{
    mutexLock(&g_lock);
    g_stop_request = true;
    g_play_request = false;
    g_playing = false;
    mutexUnlock(&g_lock);
    g_abort = true;
    ringCancel(&g_ring);
}

void radioGetStatus(RadioStatus *out)
{
    StreamInfo info;
    streamGetInfo(&info);
    memset(out, 0, sizeof(*out));
    mutexLock(&g_lock);
    out->has_station = g_has_station;
    out->station = g_station;
    snprintf(out->codec, sizeof(out->codec), "%s", g_codec);
    out->sample_rate = g_sample_rate;
    snprintf(out->error, sizeof(out->error), "%s", g_error);
    bool playing = g_playing, buffering = g_buffering, decoding = g_decoding;
    mutexUnlock(&g_lock);

    snprintf(out->title, sizeof(out->title), "%s", info.title);
    out->title_changes = info.title_changes;
    out->hls = info.hls;
    out->bitrate = info.bitrate > 0 ? info.bitrate : out->station.bitrate;
    if (g_out_rate)
        out->buffered_seconds = (float)ringFill(&g_ring) / (float)(out->bitrate > 0 ? out->bitrate * 125 : 16000);
    if (out->error[0])
        out->state = RADIO_FAILED;
    else if (!playing)
        out->state = RADIO_STOPPED;
    else if (info.state == STREAM_RECONNECTING)
        out->state = RADIO_RECONNECTING;
    else if (!decoding)
        out->state = info.state == STREAM_RECEIVING ? RADIO_BUFFERING : RADIO_CONNECTING;
    else
        out->state = buffering ? RADIO_BUFFERING : RADIO_PLAYING;
}
