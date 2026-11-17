// ICY metadata, playlist links and the byte ring.
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "hls.h"
#include "icy.h"
#include "link.h"
#include "ring.h"

typedef struct {
    char titles[4][128];
    int count;
} Titles;

static void onBlock(const char *block, void *user)
{
    Titles *t = user;
    if (t->count < 4 && icyStreamTitle(block, t->titles[t->count], sizeof(t->titles[0])))
        t->count++;
}

// Builds a stream: blocks of `metaint` audio bytes ('a' + block index), each
// followed by the given metadata (NULL: an empty block).
static size_t buildStream(uint8_t *out, size_t metaint, const char **metas, int blocks)
{
    size_t o = 0;
    for (int b = 0; b < blocks; b++) {
        memset(out + o, 'a' + b, metaint);
        o += metaint;
        const char *m = metas[b];
        size_t len = m ? strlen(m) : 0;
        size_t padded = (len + 15) / 16 * 16;
        out[o++] = (uint8_t)(padded / 16);
        memset(out + o, 0, padded);
        if (m)
            memcpy(out + o, m, len);
        o += padded;
    }
    return o;
}

static void testIcySplit(size_t chunk)
{
    const char *metas[3] = {"StreamTitle='Artist - Song';StreamUrl='';", NULL, "StreamTitle='Second';"};
    uint8_t stream[1024];
    size_t size = buildStream(stream, 100, metas, 3);
    IcyParser p;
    icyInit(&p, 100);
    Titles titles = {0};
    uint8_t audio[1024];
    size_t got = 0;
    for (size_t i = 0; i < size; i += chunk) {
        size_t n = size - i < chunk ? size - i : chunk;
        got += icyFeed(&p, stream + i, n, audio + got, onBlock, &titles);
    }
    CHECK(got == 300);
    CHECK(audio[0] == 'a' && audio[99] == 'a' && audio[100] == 'b' && audio[299] == 'c');
    CHECK(titles.count == 2);
    CHECK(strcmp(titles.titles[0], "Artist - Song") == 0);
    CHECK(strcmp(titles.titles[1], "Second") == 0);
}

void testIcy(void)
{
    testIcySplit(1);
    testIcySplit(7);
    testIcySplit(1000);

    // Without metadata everything is audio.
    IcyParser p;
    icyInit(&p, 0);
    uint8_t in[5] = {1, 2, 3, 4, 5}, out[5];
    CHECK(icyFeed(&p, in, 5, out, NULL, NULL) == 5 && memcmp(in, out, 5) == 0);

    char title[64];
    CHECK(icyStreamTitle("StreamTitle='Rock 'n' Roll';", title, sizeof(title)) && strcmp(title, "Rock 'n' Roll") == 0);
    CHECK(!icyStreamTitle("StreamTitle='';", title, sizeof(title)));
    CHECK(!icyStreamTitle("StreamTitle=' - ';", title, sizeof(title)) || strcmp(title, "-") != 0);
    CHECK(!icyStreamTitle("StreamUrl='x';", title, sizeof(title)));
    // Latin-1 becomes UTF-8; UTF-8 stays.
    CHECK(icyStreamTitle("StreamTitle='Caf\xE9';", title, sizeof(title)) && strcmp(title, "Caf\xC3\xA9") == 0);
    CHECK(icyStreamTitle("StreamTitle='Caf\xC3\xA9';", title, sizeof(title)) && strcmp(title, "Caf\xC3\xA9") == 0);
    // No closing quote: up to the end.
    CHECK(icyStreamTitle("StreamTitle='Open", title, sizeof(title)) && strcmp(title, "Open") == 0);
    // A small buffer cuts on a character boundary.
    char small[5];
    icyToUtf8("Caf\xC3\xA9s", 6, small, sizeof(small));
    CHECK(strcmp(small, "Caf") == 0);
    icyToUtf8("a\tb", 3, small, sizeof(small));
    CHECK(strcmp(small, "a b") == 0);
}

void testLinks(void)
{
    const char pls[] = "[playlist]\nNumberOfEntries=2\nFile1=http://a.example/stream\nTitle1=A\nFile2=http://b/\n";
    const char m3u[] = "#EXTM3U\n#EXTINF:-1,Radio\nhttp://c.example:8000/live.mp3\n";
    const char hls[] = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-STREAM-INF:BANDWIDTH=128000\nchunks.m3u8\n";
    char out[256];

    CHECK(linkKind("http://x/a.pls", "audio/x-scpls", pls, strlen(pls)) == LINK_PLS);
    CHECK(linkKind("http://x/listen", "text/plain", pls, strlen(pls)) == LINK_PLS);
    CHECK(linkKind("http://x/a.m3u", "audio/x-mpegurl", m3u, strlen(m3u)) == LINK_M3U);
    CHECK(linkKind("http://x/a.m3u8", "application/vnd.apple.mpegurl", hls, strlen(hls)) == LINK_HLS);
    CHECK(linkKind("http://x/a.m3u8", "", "", 0) == LINK_HLS);
    CHECK(linkKind("http://x/a.mp3", "audio/mpeg", "\xFF\xFB", 2) == LINK_AUDIO);
    CHECK(linkKind("http://x/stream", "application/ogg", "OggS", 4) == LINK_AUDIO);
    CHECK(linkKind("http://x/stream", NULL, "ID3", 3) == LINK_AUDIO);
    CHECK(linkKind("http://x/list", "text/plain", "http://y/s\n", 11) == LINK_M3U);
    CHECK(linkKind("http://x/a.pls?x=1", NULL, "", 0) == LINK_PLS);
    // A byte order mark before the header.
    CHECK(linkKind("http://x/a", NULL, "\xEF\xBB\xBF#EXTM3U\nhttp://y\n", 19) == LINK_M3U);

    CHECK(linkFirstUrl(LINK_PLS, pls, "http://x/a.pls", out, sizeof(out)) && strcmp(out, "http://a.example/stream") == 0);
    CHECK(linkFirstUrl(LINK_M3U, m3u, "http://x/a.m3u", out, sizeof(out)) &&
          strcmp(out, "http://c.example:8000/live.mp3") == 0);
    CHECK(linkFirstUrl(LINK_M3U, "#EXTM3U\r\n\r\n  live/stream.aac  \r\n", "https://h.example/radio/list.m3u?x=1", out,
                       sizeof(out)) &&
          strcmp(out, "https://h.example/radio/live/stream.aac") == 0);
    CHECK(!linkFirstUrl(LINK_M3U, "#EXTM3U\n#nothing\n", "http://x/", out, sizeof(out)));
    CHECK(!linkFirstUrl(LINK_PLS, "[playlist]\nTitle1=x\n", "http://x/", out, sizeof(out)));

    CHECK(linkResolve("http://h:80/a/b.m3u", "/c.mp3", out, sizeof(out)) && strcmp(out, "http://h:80/c.mp3") == 0);
    CHECK(linkResolve("http://h", "c.mp3", out, sizeof(out)) && strcmp(out, "http://h/c.mp3") == 0);
    CHECK(linkResolve("https://h/a/", "//cdn/x", out, sizeof(out)) && strcmp(out, "https://cdn/x") == 0);
    CHECK(linkResolve("http://h/a", "https://o/x", out, sizeof(out)) && strcmp(out, "https://o/x") == 0);
    char tiny[8];
    CHECK(!linkResolve("http://h/a", "https://other/x", tiny, sizeof(tiny)));
}

typedef struct {
    Ring *ring;
    size_t total;
} Writer;

static void *writeAll(void *arg)
{
    Writer *w = arg;
    uint8_t chunk[333];
    for (size_t sent = 0; sent < w->total;) {
        size_t n = w->total - sent < sizeof(chunk) ? w->total - sent : sizeof(chunk);
        for (size_t i = 0; i < n; i++)
            chunk[i] = (uint8_t)(sent + i);
        if (!ringWrite(w->ring, chunk, n))
            return NULL;
        sent += n;
    }
    ringEnd(w->ring);
    return NULL;
}

void testRing(void)
{
    Ring r;
    CHECK(ringInit(&r, 1000));
    Writer w = {&r, 100000};
    pthread_t thread;
    pthread_create(&thread, NULL, writeAll, &w);
    uint8_t buf[257];
    size_t got = 0;
    bool ordered = true;
    while (!ringFinished(&r)) {
        size_t n = ringRead(&r, buf, sizeof(buf), 1000);
        for (size_t i = 0; i < n; i++)
            ordered &= buf[i] == (uint8_t)(got + i);
        got += n;
    }
    pthread_join(thread, NULL);
    CHECK(got == 100000);
    CHECK(ordered);

    // Cancelling wakes a blocked writer and stops reads.
    ringReset(&r);
    Writer big = {&r, 5000};
    pthread_create(&thread, NULL, writeAll, &big);
    while (ringFill(&r) < 1000)
        ;
    ringCancel(&r);
    pthread_join(thread, NULL);
    CHECK(ringFinished(&r));
    CHECK(ringRead(&r, buf, sizeof(buf), 10) == 0);

    // A read with nothing to read times out.
    ringReset(&r);
    CHECK(ringRead(&r, buf, sizeof(buf), 20) == 0 && !ringFinished(&r));
    ringFree(&r);
}

void testHls(void)
{
    const char master[] = "#EXTM3U\n"
                          "#EXT-X-STREAM-INF:BANDWIDTH=64000,CODECS=\"mp4a.40.5\"\nlow/index.m3u8\n"
                          "#EXT-X-STREAM-INF:CODECS=\"mp4a.40.2\",BANDWIDTH=192000\nhigh/index.m3u8\n"
                          "#EXT-X-STREAM-INF:BANDWIDTH=512000\nhuge/index.m3u8\n";
    HlsPlaylist p;
    CHECK(hlsParse(master, "https://cdn.example/fip/fip.m3u8", 320000, &p));
    CHECK(p.master && p.count == 0);
    CHECK(strcmp(p.variant, "https://cdn.example/fip/high/index.m3u8") == 0 && p.variant_bandwidth == 192000);
    hlsFree(&p);
    // All above the limit: the lowest.
    CHECK(hlsParse(master, "https://cdn.example/fip/fip.m3u8", 32000, &p));
    CHECK(strcmp(p.variant, "https://cdn.example/fip/low/index.m3u8") == 0);
    hlsFree(&p);

    const char media[] = "#EXTM3U\r\n#EXT-X-VERSION:3\r\n#EXT-X-TARGETDURATION:4\r\n#EXT-X-MEDIA-SEQUENCE:100\r\n"
                         "#EXTINF:4.0,\r\nseg100.aac\r\n#EXTINF:4.0,\r\nseg101.aac\r\n#EXTINF:4.0,\r\n/abs/seg102.aac\r\n"
                         "#EXTINF:4.0,\r\nhttp://other/seg103.ts\r\n#EXTINF:4.0,\r\nseg104.aac\r\n";
    CHECK(hlsParse(media, "http://h/live/list.m3u8?token=1", 320000, &p));
    CHECK(!p.master && !p.ended && !p.encrypted && !p.fragmented_mp4);
    CHECK(p.count == 5 && p.target_duration == 4.0);
    if (p.count == 5) {
        CHECK(strcmp(p.segments[0].url, "http://h/live/seg100.aac") == 0 && p.segments[0].sequence == 100);
        CHECK(strcmp(p.segments[2].url, "http://h/abs/seg102.aac") == 0);
        CHECK(strcmp(p.segments[3].url, "http://other/seg103.ts") == 0 && p.segments[4].sequence == 104);
        CHECK(hlsFirstSegment(&p, -1) == 2);   // Three from the live end.
        CHECK(hlsFirstSegment(&p, 101) == 2);  // The next one.
        CHECK(hlsFirstSegment(&p, 104) == 5);  // Nothing new.
        CHECK(hlsFirstSegment(&p, 50) == 0);   // Fell behind: the oldest listed.
    }
    hlsFree(&p);

    // fMP4: the init segment, resolved like the segments, comes first.
    CHECK(hlsParse("#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:2,\na.m4s\n#EXT-X-ENDLIST\n", "http://h/live/x", 1,
                   &p));
    CHECK(p.fragmented_mp4 && p.ended && p.count == 1 && strcmp(p.map, "http://h/live/init.mp4") == 0);
    hlsFree(&p);
    CHECK(hlsParse("#EXTM3U\n#EXT-X-MAP:URI=\"/dyseg/map/init.mp4\"\n#EXTINF:2,\na.m4s\n", "https://h/live/x", 1, &p));
    CHECK(strcmp(p.map, "https://h/dyseg/map/init.mp4") == 0);
    hlsFree(&p);
    // A byte range of a file isn't fetched; nor is a URI too long to keep whole.
    CHECK(hlsParse("#EXTM3U\n#EXT-X-MAP:URI=\"all.mp4\",BYTERANGE=\"720@0\"\n#EXTINF:2,\na.m4s\n", "http://h/x", 1, &p));
    CHECK(p.fragmented_mp4 && p.map[0] == '\0');
    hlsFree(&p);
    char *long_map = malloc(LINK_URL_MAX + 64);
    if (long_map) {
        snprintf(long_map, LINK_URL_MAX + 64, "#EXTM3U\n#EXT-X-MAP:URI=\"%0*d\"\n#EXTINF:2,\na.m4s\n", LINK_URL_MAX, 0);
        CHECK(hlsParse(long_map, "http://h/x", 1, &p) && p.fragmented_mp4 && p.map[0] == '\0');
        hlsFree(&p);
        free(long_map);
    }
    CHECK(hlsParse("#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k\"\n#EXTINF:2,\na.ts\n", "http://h/x", 1, &p) && p.encrypted);
    hlsFree(&p);
    CHECK(hlsParse("#EXTM3U\n#EXT-X-KEY:METHOD=NONE\n#EXTINF:2,\na.ts\n", "http://h/x", 1, &p) && !p.encrypted);
    hlsFree(&p);
    CHECK(!hlsParse("[playlist]\nFile1=x\n", "http://h/x", 1, &p));
    // A master playlist whose variants all fail to resolve is no use.
    CHECK(!hlsParse("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nrel\n", "nohost", 1, &p));
}
