// Plays a podcast episode on the host the way the console will: Apple's
// search, the first podcast's feed, its newest episode read through
// podread.c into audiodec.c. Decodes a few seconds, jumps, decodes again,
// and reports the length, the positions and how loud the audio was.
//   episodedump "search words" [SECONDS_TO_JUMP_TO]
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audiodec.h"
#include "net.h"
#include "podcast.h"
#include "podread.h"

#define OUT_RATE 48000
#define FRAMES 4096

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// Decodes about seconds of audio; returns its RMS level (0 for silence).
static double decodeFor(double seconds)
{
    static int16_t pcm[FRAMES * 2];
    double sum = 0;
    size_t total = 0, want = (size_t)(seconds * OUT_RATE);
    while (total < want) {
        size_t n = decoderRead(pcm, FRAMES);
        if (n == 0)
            break;
        for (size_t i = 0; i < n * 2; i++)
            sum += (double)pcm[i] * pcm[i];
        total += n;
    }
    return total ? sqrt(sum / (double)(total * 2)) : 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: episodedump \"search words\" [seconds]\n");
        return 2;
    }
    netInit();
    char url[LINK_URL_MAX];
    NetResponse r;
    PodcastResults found;
    if (!podcastSearchUrl(argv[1], url, sizeof(url)) || !netGet(url, 2u << 20, NULL, &r) ||
        !podcastParseSearch(r.data, &found) || found.count == 0) {
        fprintf(stderr, "search failed: %s\n", r.error);
        return 1;
    }
    netResponseFree(&r);
    printf("podcast: %s (%s)\nfeed: %s\n", found.items[0].title, found.items[0].author, found.items[0].feed);
    Feed feed;
    double t = now();
    if (!netGet(found.items[0].feed, PODCAST_FEED_MAX, NULL, &r) || !podcastParseFeed(r.data, r.size, &feed) ||
        feed.count == 0) {
        fprintf(stderr, "feed failed: %s\n", r.error);
        return 1;
    }
    printf("feed: %zu bytes, %zu episodes, read in %.2f s\n", r.size, feed.count, now() - t);
    netResponseFree(&r);
    const Episode *e = &feed.episodes[0];
    printf("episode: %s\n  %s (%s, %lld bytes, %d s said)\n", e->title, e->url, e->type, (long long)e->bytes,
           e->duration);

    volatile bool abort = false;
    t = now();
    PodReader *reader = podReaderOpen(e->url);
    if (!reader || !decoderOpenReader(reader, e->type, OUT_RATE, &abort)) {
        PodReaderInfo info;
        if (reader)
            podReaderInfo(reader, &info);
        fprintf(stderr, "open failed: %s\n", reader ? info.error : "no memory");
        return 1;
    }
    DecoderInfo d;
    decoderGetInfo(&d);
    printf("opened in %.2f s: %s, length %.1f s, file %lld bytes\n", now() - t, d.codec, decoderDuration(),
           (long long)podReaderSize(reader));
    t = now();
    double level = decodeFor(5);
    printf("start: level %.0f, at %.1f s (%.2f s to decode 5 s)\n", level, decoderPosition(), now() - t);
    double jump = argc > 2 ? atof(argv[2]) : decoderDuration() / 2;
    t = now();
    bool ok = decoderSeek(jump);
    level = decodeFor(5);
    printf("jump to %.1f s: %s, level %.0f, at %.1f s (%.2f s)\n", jump, ok ? "ok" : "FAILED", level,
           decoderPosition(), now() - t);
    t = now();
    ok = decoderSeek(jump / 4);
    level = decodeFor(2);
    printf("back to %.1f s: %s, level %.0f, at %.1f s (%.2f s)\n", jump / 4, ok ? "ok" : "FAILED", level,
           decoderPosition(), now() - t);
    decoderClose();
    podReaderClose(reader);
    podcastFeedFree(&feed);
    podcastResultsFree(&found);
    netExit();
    return 0;
}
