#include "audiodec.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>

#ifndef AV_PROFILE_AAC_HE  // FFmpeg before 6.1.
#define AV_PROFILE_AAC_HE FF_PROFILE_AAC_HE
#define AV_PROFILE_AAC_HE_V2 FF_PROFILE_AAC_HE_V2
#endif

#define AVIO_BUFFER 16384
#define PROBE_BYTES (64 * 1024)
#define READ_WAIT_MS 100
#define REFILL_POLL_US 50000

static Ring *g_ring;
static const volatile bool *g_abort;
static size_t g_rebuffer;
static unsigned g_out_rate;
static volatile bool g_buffering;
static bool g_playing;  // Audio has come out: an empty ring from now on is an underrun.

static AVFormatContext *g_format;
static AVIOContext *g_avio;
static AVCodecContext *g_codec;
static SwrContext *g_swr;
static AVPacket *g_packet;
static AVFrame *g_frame;
static int g_stream = -1;
static int g_swr_rate, g_swr_format;
static AVChannelLayout g_swr_layout;
static DecoderInfo g_info;
static bool g_seekable;          // Opened from a reader: it can jump.
static volatile double g_position;  // Seconds into the episode of the audio decoded last.

static int readPacket(void *opaque, uint8_t *buf, int size)
{
    (void)opaque;
    // An empty ring after playback started: wait to refill a few seconds
    // rather than stutter on every packet.
    if (g_playing && ringFill(g_ring) == 0 && !ringFinished(g_ring)) {
        g_buffering = true;
        while (!*g_abort && ringFill(g_ring) < g_rebuffer && !ringFinished(g_ring))
            usleep(REFILL_POLL_US);
    }
    while (!*g_abort) {
        size_t n = ringRead(g_ring, buf, (size_t)size, READ_WAIT_MS);
        if (n > 0) {
            g_buffering = false;
            return (int)n;
        }
        if (ringFinished(g_ring))
            return AVERROR_EOF;
    }
    return AVERROR_EXIT;
}

// An episode: read through the seekable reader instead of the ring.
static int readReader(void *opaque, uint8_t *buf, int size)
{
    int n = podReaderRead(opaque, buf, size, g_abort);
    return n > 0 ? n : n == 0 ? AVERROR_EOF : n == -2 ? AVERROR_EXIT : AVERROR(EIO);
}

static int64_t seekReader(void *opaque, int64_t offset, int whence)
{
    PodReader *r = opaque;
    int64_t size = podReaderSize(r);
    switch (whence & ~AVSEEK_FORCE) {
    case AVSEEK_SIZE: return size >= 0 ? size : -1;
    case SEEK_SET: return podReaderSeek(r, offset);
    case SEEK_CUR: {
        PodReaderInfo info;
        podReaderInfo(r, &info);
        return podReaderSeek(r, info.position + offset);
    }
    case SEEK_END: return size >= 0 ? podReaderSeek(r, size + offset) : -1;
    default: return -1;
    }
}

static int interrupted(void *opaque)
{
    (void)opaque;
    return *g_abort;
}

static const char *formatFor(const char *content_type)
{
    static const struct {
        const char *type;
        const char *format;
    } TYPES[] = {
        {"audio/mpeg", "mp3"},      {"audio/mp3", "mp3"},       {"audio/aac", "aac"},  {"audio/aacp", "aac"},
        {"audio/x-aac", "aac"},     {"audio/ogg", "ogg"},       {"application/ogg", "ogg"},
        {"audio/opus", "ogg"},      {"audio/flac", "flac"},     {"video/mp2t", "mpegts"},
        {"audio/mp2t", "mpegts"},   {"audio/mp4", "mp4"},       {"video/mp4", "mp4"},  {"audio/x-m4a", "mp4"},
        {"application/mp4", "mp4"}, {"audio/iso.segment", "mp4"}, {"video/iso.segment", "mp4"},
    };
    for (size_t i = 0; content_type && i < sizeof(TYPES) / sizeof(TYPES[0]); i++)
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

void decoderClose(void)
{
    swr_free(&g_swr);
    av_channel_layout_uninit(&g_swr_layout);
    avcodec_free_context(&g_codec);
    if (g_format)
        avformat_close_input(&g_format);
    if (g_avio) {
        av_freep(&g_avio->buffer);
        avio_context_free(&g_avio);
    }
    av_packet_free(&g_packet);
    av_frame_free(&g_frame);
    g_stream = -1;
    g_playing = false;
    g_buffering = false;
    g_seekable = false;
    g_position = 0;
    memset(&g_info, 0, sizeof(g_info));
}

// Opens what g_avio reads (the ring's stream, or an episode).
static bool openInput(const char *content_type)
{
    g_format = avformat_alloc_context();
    if (!g_packet || !g_frame || !g_avio || !g_format) {
        decoderClose();
        return false;
    }
    g_format->pb = g_avio;
    g_format->flags |= AVFMT_FLAG_CUSTOM_IO;
    g_format->interrupt_callback = (AVIOInterruptCB){interrupted, NULL};
    g_format->probesize = PROBE_BYTES;
    g_format->max_analyze_duration = AV_TIME_BASE / 2;
    const char *name = formatFor(content_type);
    const AVInputFormat *hint = name ? av_find_input_format(name) : NULL;
    const AVCodec *codec = NULL;
    bool ok = avformat_open_input(&g_format, NULL, hint, NULL) == 0 &&
              avformat_find_stream_info(g_format, NULL) >= 0 &&
              (g_stream = av_find_best_stream(g_format, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0)) >= 0 && codec &&
              (g_codec = avcodec_alloc_context3(codec)) != NULL &&
              avcodec_parameters_to_context(g_codec, g_format->streams[g_stream]->codecpar) >= 0 &&
              avcodec_open2(g_codec, codec, NULL) == 0;
    if (!ok) {
        decoderClose();
        return false;
    }
    codecLabel(g_codec, g_info.codec, sizeof(g_info.codec));
    return true;
}

bool decoderOpen(Ring *ring, const char *content_type, unsigned out_rate, const volatile bool *abort,
                 size_t rebuffer)
{
    decoderClose();
    g_ring = ring;
    g_abort = abort;
    g_rebuffer = rebuffer;
    g_out_rate = out_rate;
    g_packet = av_packet_alloc();
    g_frame = av_frame_alloc();
    uint8_t *buffer = av_malloc(AVIO_BUFFER);
    g_avio = buffer ? avio_alloc_context(buffer, AVIO_BUFFER, 0, NULL, readPacket, NULL, NULL) : NULL;
    if (buffer && !g_avio)
        av_free(buffer);
    return openInput(content_type);
}

bool decoderOpenReader(PodReader *reader, const char *content_type, unsigned out_rate, const volatile bool *abort)
{
    decoderClose();
    g_ring = NULL;
    g_abort = abort;
    g_rebuffer = 0;
    g_out_rate = out_rate;
    g_packet = av_packet_alloc();
    g_frame = av_frame_alloc();
    uint8_t *buffer = av_malloc(AVIO_BUFFER);
    g_avio = buffer ? avio_alloc_context(buffer, AVIO_BUFFER, 0, reader, readReader, NULL, seekReader) : NULL;
    if (buffer && !g_avio)
        av_free(buffer);
    if (g_avio)
        g_avio->seekable = AVIO_SEEKABLE_NORMAL;
    g_seekable = true;
    return openInput(content_type);
}

double decoderDuration(void)
{
    if (!g_format)
        return 0;
    if (g_format->duration > 0 && g_format->duration != AV_NOPTS_VALUE)
        return (double)g_format->duration / AV_TIME_BASE;
    const AVStream *s = g_stream >= 0 ? g_format->streams[g_stream] : NULL;
    if (s && s->duration > 0 && s->duration != AV_NOPTS_VALUE)
        return (double)s->duration * av_q2d(s->time_base);
    return 0;
}

double decoderPosition(void)
{
    return g_position;
}

bool decoderSeek(double seconds)
{
    if (!g_format || !g_codec || !g_seekable)
        return false;
    if (seconds < 0)
        seconds = 0;
    int64_t target = (int64_t)(seconds * AV_TIME_BASE);
    g_avio->eof_reached = 0;  // A read stopped for this jump left these set.
    g_avio->error = 0;
    if (avformat_seek_file(g_format, -1, INT64_MIN, target, INT64_MAX, 0) < 0)
        return false;
    avcodec_flush_buffers(g_codec);
    swr_free(&g_swr);  // What it held was from before: made again for the next frame.
    av_channel_layout_uninit(&g_swr_layout);
    g_position = seconds;
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
    g_info.sample_rate = (unsigned)f->sample_rate;
    g_info.channels = (unsigned)f->ch_layout.nb_channels;
    return true;
}

// Audio the resampler holds from earlier frames, before decoding more.
static size_t drainResampler(int16_t *out, size_t frames)
{
    if (!g_swr || swr_get_out_samples(g_swr, 0) <= 0)
        return 0;
    uint8_t *dst = (uint8_t *)out;
    int n = swr_convert(g_swr, &dst, (int)frames, NULL, 0);
    return n > 0 ? (size_t)n : 0;
}

size_t decoderRead(int16_t *out, size_t frames)
{
    size_t done = 0;
    while (done < frames && !*g_abort) {
        done += drainResampler(out + done * 2, frames - done);
        if (done >= frames)
            break;
        int rc = avcodec_receive_frame(g_codec, g_frame);
        if (rc == 0) {
            int64_t pts = g_frame->best_effort_timestamp;
            if (pts != AV_NOPTS_VALUE && g_stream >= 0)
                g_position = (double)pts * av_q2d(g_format->streams[g_stream]->time_base);
            bool ok = prepareResampler(g_frame);
            if (ok) {
                // Converts what fits; swr keeps the rest for drainResampler.
                uint8_t *dst = (uint8_t *)(out + done * 2);
                int n = swr_convert(g_swr, &dst, (int)(frames - done), (const uint8_t **)g_frame->extended_data,
                                    g_frame->nb_samples);
                ok = n >= 0;
                if (n > 0) {
                    done += (size_t)n;
                    g_playing = true;
                }
            }
            av_frame_unref(g_frame);
            if (!ok)
                break;
            continue;
        }
        if (rc != AVERROR(EAGAIN))
            break;
        rc = av_read_frame(g_format, g_packet);
        if (rc < 0)
            break;
        // A damaged packet is skipped; the stream goes on.
        if (g_packet->stream_index == g_stream)
            avcodec_send_packet(g_codec, g_packet);
        av_packet_unref(g_packet);
    }
    return done;
}

void decoderGetInfo(DecoderInfo *out)
{
    *out = g_info;
}

bool decoderBuffering(void)
{
    return g_buffering;
}
