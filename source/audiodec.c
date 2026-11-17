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
        {"audio/mp2t", "mpegts"},
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
    memset(&g_info, 0, sizeof(g_info));
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
    g_format = avformat_alloc_context();
    if (!g_packet || !g_frame || !g_avio || !g_format) {
        if (!g_avio)
            av_free(buffer);
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
