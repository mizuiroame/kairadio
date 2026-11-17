#include "radio.h"

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "audiodec.h"
#include "ring.h"
#include "stream.h"

#define RING_BYTES (1u << 20)        // About a minute at 128 kbit/s.
#define PREBUFFER_SECONDS 3
#define PREBUFFER_MIN (16u * 1024)
#define PREBUFFER_DEFAULT (48u * 1024)
#define BUFFER_COUNT 3
#define BUFFER_FRAMES 4096
#define BUFFER_BYTES (BUFFER_FRAMES * 2 * sizeof(s16))
#define BUFFER_ALIGN 0x1000
#define WAIT_TIMEOUT_NS 20000000ull
#define IDLE_SLEEP_NS 10000000ull
#define THREAD_STACK_SIZE 0x40000
#define THREAD_PRIORITY 0x2B
#define LOG_EVERY_NS 2000000000ull

// Shared with the UI thread: guarded by g_lock.
static Mutex g_lock;
static bool g_has_station;
static Station g_station;
static bool g_play_request;   // Start g_station.
static bool g_stop_request;
static bool g_playing;        // A station is meant to be playing.
static bool g_decoding;       // The decoder has produced audio.
static char g_error[160];

// Owned by the audio thread.
static Thread g_thread;
static volatile bool g_running;
static volatile bool g_abort;  // Makes a blocked read give up (a new command came).
static Ring g_ring;
static size_t g_prebuffer;
static bool g_open;            // The decoder is open for this station.
static u32 g_out_rate;
static AudioOutBuffer g_buffers[BUFFER_COUNT];
static bool g_queued[BUFFER_COUNT];

// What the audio log reports, for the station playing.
static char g_log_path[256];  // audio.log in KaiRadio's folder, from radioInit.

static struct {
    FILE *file;
    u64 started, last;
    u64 frames;       // Decoded and queued.
    u64 appended, released, short_buffers;
    u64 played_start;
} g_log;

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

static u64 nowNs(void)
{
    return armTicksToNs(armGetSystemTick());
}

static void setError(const char *message)
{
    mutexLock(&g_lock);
    snprintf(g_error, sizeof(g_error), "%s", message);
    g_playing = false;
    mutexUnlock(&g_lock);
}

// --- The audio log: how fast the output takes audio, to find stutters ---

static void logStart(const char *url)
{
    if (!g_log.file)
        g_log.file = fopen(g_log_path, "w");
    if (!g_log.file)
        return;
    g_log.started = g_log.last = nowNs();
    g_log.frames = g_log.appended = g_log.released = g_log.short_buffers = 0;
    audoutGetAudioOutPlayedSampleCount(&g_log.played_start);
    fprintf(g_log.file, "start %s, output %u Hz, %u channels\n", url, g_out_rate, audoutGetChannelCount());
    fflush(g_log.file);
}

static void logTick(void)
{
    u64 now = nowNs();
    if (!g_log.file || now - g_log.last < LOG_EVERY_NS)
        return;
    g_log.last = now;
    DecoderInfo d;
    decoderGetInfo(&d);
    StreamInfo s;
    streamGetInfo(&s);
    u64 played = 0;
    audoutGetAudioOutPlayedSampleCount(&played);
    u32 queued = 0;
    audoutGetAudioOutBufferCount(&queued);
    double t = (double)(now - g_log.started) / 1e9;
    fprintf(g_log.file,
            "%6.1fs decoded %.1fs played %.1fs (%llu samples) | appended %llu released %llu queued %u short %llu | "
            "%s %u Hz %u ch | ring %zu buffering %d | stream %d %d kbps '%s'\n",
            t, (double)g_log.frames / g_out_rate, (double)(played - g_log.played_start) / g_out_rate,
            (unsigned long long)(played - g_log.played_start), (unsigned long long)g_log.appended,
            (unsigned long long)g_log.released, queued, (unsigned long long)g_log.short_buffers, d.codec,
            d.sample_rate, d.channels, ringFill(&g_ring), decoderBuffering(), s.state, s.bitrate, s.content_type);
    fflush(g_log.file);
}

static void logLine(const char *message)
{
    if (!g_log.file)
        return;
    fprintf(g_log.file, "%6.1fs %s\n", (double)(nowNs() - g_log.started) / 1e9, message);
    fflush(g_log.file);
}

// --- Output ---

// Marks the buffers the output has finished with. Asks about each one, so a
// release is never missed.
static void reapBuffers(void)
{
    bool any = false;
    for (int i = 0; i < BUFFER_COUNT; i++)
        any |= g_queued[i];
    if (!any) {
        svcSleepThread(IDLE_SLEEP_NS);
        return;
    }
    AudioOutBuffer *released = NULL;
    u32 count = 0;
    audoutWaitPlayFinish(&released, &count, WAIT_TIMEOUT_NS);
    for (int i = 0; i < BUFFER_COUNT; i++) {
        bool contains = true;
        if (g_queued[i] && R_SUCCEEDED(audoutContainsAudioOutBuffer(&g_buffers[i], &contains)) && !contains) {
            g_queued[i] = false;
            g_log.released++;
        }
    }
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
    g_abort = true;
    streamStop();
    decoderClose();
    g_open = false;
    flushOutput();
    g_abort = false;
    mutexLock(&g_lock);
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
    logStart(url);
}

static void handleCommands(void)
{
    mutexLock(&g_lock);
    bool play = g_play_request, stop = g_stop_request;
    g_play_request = g_stop_request = false;
    mutexUnlock(&g_lock);
    g_abort = false;
    if (play) {
        startPlayback();
    } else if (stop) {
        stopPlayback();
        logLine("stopped");
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

static void failWith(const char *fallback)
{
    StreamInfo info;
    streamGetInfo(&info);
    stopPlayback();
    const char *message = info.state == STREAM_FAILED ? info.error : fallback;
    logLine(message);
    setError(message);
}

// Opens the decoder once the start of the stream is buffered.
static bool openWhenBuffered(void)
{
    StreamInfo info;
    streamGetInfo(&info);
    if (info.state == STREAM_FAILED) {
        failWith(info.error);
        return false;
    }
    if (ringFill(&g_ring) < g_prebuffer && !ringFinished(&g_ring)) {
        svcSleepThread(IDLE_SLEEP_NS * 5);
        return false;
    }
    streamGetInfo(&info);
    if (!decoderOpen(&g_ring, info.content_type, g_out_rate, &g_abort, g_prebuffer)) {
        if (!g_abort)
            failWith("This station's audio format isn't supported.");
        return false;
    }
    g_open = true;
    DecoderInfo d;
    decoderGetInfo(&d);
    char line[160];
    snprintf(line, sizeof(line), "decoder %s, content type '%s'", d.codec, info.content_type);
    logLine(line);
    return true;
}

static void playStep(void)
{
    if (!g_open && !openWhenBuffered())
        return;
    for (int i = 0; i < BUFFER_COUNT && !g_abort; i++) {
        if (g_queued[i])
            continue;
        AudioOutBuffer *b = &g_buffers[i];
        s16 *pcm = b->buffer;
        size_t n = decoderRead(pcm, BUFFER_FRAMES);
        if (n == 0) {
            bool waiting = false;
            for (int k = 0; k < BUFFER_COUNT; k++)
                waiting |= g_queued[k];
            if (g_abort || waiting)
                break;  // Let what's queued play out first.
            StreamInfo info;
            streamGetInfo(&info);
            failWith(info.state == STREAM_ENDED ? "The station ended the broadcast." : "The station's audio stopped.");
            return;
        }
        if (n < BUFFER_FRAMES) {
            memset(pcm + n * 2, 0, (BUFFER_FRAMES - n) * 2 * sizeof(s16));
            g_log.short_buffers++;
        }
        b->data_size = BUFFER_BYTES;
        b->data_offset = 0;
        if (R_SUCCEEDED(audoutAppendAudioOutBuffer(b))) {
            g_queued[i] = true;
            g_log.appended++;
            g_log.frames += BUFFER_FRAMES;
        }
        mutexLock(&g_lock);
        g_decoding = true;
        mutexUnlock(&g_lock);
    }
    reapBuffers();
    logTick();
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

bool radioInit(const char *folder)
{
    snprintf(g_log_path, sizeof(g_log_path), "%s/audio.log", folder);
    mutexInit(&g_lock);
    if (!ringInit(&g_ring, RING_BYTES))
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
    if (g_log.file)
        fclose(g_log.file);
    g_log.file = NULL;
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
    DecoderInfo d;
    decoderGetInfo(&d);
    memset(out, 0, sizeof(*out));
    mutexLock(&g_lock);
    out->has_station = g_has_station;
    out->station = g_station;
    snprintf(out->error, sizeof(out->error), "%s", g_error);
    bool playing = g_playing, decoding = g_decoding;
    mutexUnlock(&g_lock);

    snprintf(out->codec, sizeof(out->codec), "%s", d.codec);
    out->sample_rate = d.sample_rate;
    snprintf(out->title, sizeof(out->title), "%s", info.title);
    out->title_changes = info.title_changes;
    out->hls = info.hls;
    out->bitrate = info.bitrate > 0 ? info.bitrate : out->station.bitrate;
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
        out->state = decoderBuffering() ? RADIO_BUFFERING : RADIO_PLAYING;
}
