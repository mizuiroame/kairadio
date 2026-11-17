// Plays a station on the host the way the console does (stream.c into the
// ring, audiodec.c out of it), paced like the audio output, and writes what
// would be heard to a WAV file with a report: how much audio came out per
// second of real time, and how often it ran dry.
//   streamdump URL SECONDS OUT.wav
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "audiodec.h"
#include "net.h"
#include "ring.h"
#include "stream.h"

#define OUT_RATE 48000
#define BUFFER_FRAMES 2048
#define RING_BYTES (1u << 20)
#define PREBUFFER (48u * 1024)

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void writeHeader(FILE *f, unsigned frames)
{
    unsigned data = frames * 4;
    unsigned char h[44] = "RIFF....WAVEfmt ";
    unsigned v[] = {36 + data, 16, 0x00020001, OUT_RATE, OUT_RATE * 4, 0x00100004};
    memcpy(h + 4, &v[0], 4);
    memcpy(h + 16, &v[1], 4);
    memcpy(h + 20, &v[2], 4);
    memcpy(h + 24, &v[3], 4);
    memcpy(h + 28, &v[4], 4);
    memcpy(h + 32, &v[5], 4);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data, 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: streamdump URL SECONDS OUT.wav\n");
        return 2;
    }
    double seconds = atof(argv[2]);
    netInit();
    static Ring ring;
    ringInit(&ring, RING_BYTES);
    streamStart(&ring, argv[1]);
    double start = now();
    while (ringFill(&ring) < PREBUFFER && !ringFinished(&ring) && now() - start < 20)
        usleep(20000);
    StreamInfo info;
    streamGetInfo(&info);
    printf("stream: state %d, type '%s', %d kbps, hls %d, prebuffered %zu bytes in %.2f s\n", info.state,
           info.content_type, info.bitrate, info.hls, ringFill(&ring), now() - start);
    static volatile bool abort_flag;
    if (!decoderOpen(&ring, info.content_type, OUT_RATE, &abort_flag, PREBUFFER)) {
        printf("decoder: can't open (%s)\n", info.error);
        return 1;
    }
    DecoderInfo d;
    FILE *out = fopen(argv[3], "wb");
    writeHeader(out, 0);
    static int16_t pcm[BUFFER_FRAMES * 2];
    unsigned frames = 0, short_reads = 0;
    double play_start = now(), late = 0;
    while (now() - play_start < seconds) {
        double due = play_start + (double)frames / OUT_RATE;
        double t = now();
        if (t < due - 0.1)  // Keep about 100 ms queued, like the console.
            usleep((useconds_t)((due - 0.1 - t) * 1e6));
        else if (t > due)
            late += t - due;
        size_t n = decoderRead(pcm, BUFFER_FRAMES);
        if (n < BUFFER_FRAMES)
            short_reads++;
        fwrite(pcm, 4, n, out);
        frames += (unsigned)n;
        if (n == 0)
            break;
    }
    double elapsed = now() - play_start;
    writeHeader(out, frames);
    fclose(out);
    decoderGetInfo(&d);
    streamGetInfo(&info);
    printf("decoder: %s, %u Hz, %u channels\n", d.codec, d.sample_rate, d.channels);
    printf("audio: %.2f s out in %.2f s; behind schedule %.2f s; short reads %u; ring %zu bytes; title '%s'\n",
           frames / (double)OUT_RATE, elapsed, late, short_reads, ringFill(&ring), info.title);
    abort_flag = true;
    streamStop();
    decoderClose();
    ringFree(&ring);
    netExit();
    return 0;
}
