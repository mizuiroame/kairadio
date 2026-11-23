// The episode reader (podread.c) against the fake network's file.
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "fake_net.h"
#include "podread.h"

#define FILE_SIZE (10 * 1024 * 1024 + 123)
#define READ_SIZE (100 * 1024)

// Reads from where r is to the end (or count bytes); false at the first wrong byte.
static bool readChecked(PodReader *r, int64_t from, int64_t count, int64_t *got)
{
    static uint8_t buf[READ_SIZE];
    *got = 0;
    while (count < 0 || *got < count) {
        int want = count < 0 || count - *got > READ_SIZE ? READ_SIZE : (int)(count - *got);
        int n = podReaderRead(r, buf, want, NULL);
        if (n <= 0)
            return n == 0;
        for (int i = 0; i < n; i++)
            if (buf[i] != fakeNetFileByte(from + *got + i))
                return false;
        *got += n;
    }
    return true;
}

static void testStraight(void)
{
    fakeNetFile("cdn.example/ep.mp3", FILE_SIZE, true, -1);
    PodReader *r = podReaderOpen("https://cdn.example/ep.mp3");
    CHECK(r != NULL);
    if (!r)
        return;
    int64_t got;
    CHECK(readChecked(r, 0, -1, &got) && got == FILE_SIZE);  // More than the buffer holds.
    CHECK(podReaderSize(r) == FILE_SIZE && fakeNetStreams() == 1);
    PodReaderInfo info;
    podReaderInfo(r, &info);
    CHECK(info.ended && !info.failed && info.position == FILE_SIZE && !strcmp(info.content_type, "audio/mpeg"));
    podReaderClose(r);
}

static void testSeeks(bool ranges)
{
    fakeNetFile("cdn.example/ep.mp3", FILE_SIZE, ranges, -1);
    PodReader *r = podReaderOpen("https://cdn.example/ep.mp3");
    if (!r)
        return;
    int64_t got;
    CHECK(readChecked(r, 0, 300000, &got) && got == 300000);
    int streams = fakeNetStreams();
    CHECK(podReaderSeek(r, 100000) == 100000);  // Back, within what's kept: no new request.
    CHECK(readChecked(r, 100000, 50000, &got) && got == 50000 && fakeNetStreams() == streams);
    CHECK(podReaderSeek(r, 8000000) == 8000000);  // Far ahead: a new request from there.
    CHECK(readChecked(r, 8000000, 200000, &got) && got == 200000 && fakeNetStreams() == streams + 1);
    CHECK(podReaderSeek(r, 50) == 50);  // Back past what's kept: asked again.
    CHECK(readChecked(r, 50, 1000, &got) && got == 1000 && fakeNetStreams() == streams + 2);
    CHECK(podReaderSeek(r, FILE_SIZE + 999) == FILE_SIZE);  // Kept within the file.
    CHECK(readChecked(r, FILE_SIZE, -1, &got) && got == 0);
    CHECK(podReaderSeek(r, FILE_SIZE - 10) == FILE_SIZE - 10);
    CHECK(readChecked(r, FILE_SIZE - 10, -1, &got) && got == 10);
    podReaderClose(r);
}

static void testDropAndMissing(void)
{
    fakeNetFile("cdn.example/ep.mp3", FILE_SIZE, true, 3 * 1024 * 1024);
    PodReader *r = podReaderOpen("https://cdn.example/ep.mp3");
    if (r) {
        int64_t got;
        CHECK(readChecked(r, 0, -1, &got) && got == FILE_SIZE);  // Picked up where it dropped.
        CHECK(fakeNetStreams() == 2);
        podReaderClose(r);
    }
    int before = fakeNetStreams();
    r = podReaderOpen("https://elsewhere.example/missing.mp3");
    if (r) {
        char buf[16];
        CHECK(podReaderRead(r, buf, sizeof(buf), NULL) == -1);
        PodReaderInfo info;
        podReaderInfo(r, &info);
        CHECK(info.failed && strstr(info.error, "404") != NULL && fakeNetStreams() == before + 1);  // No retries.
        podReaderClose(r);
    }
    // Closing while it downloads stops it at once.
    fakeNetFile("cdn.example/ep.mp3", FILE_SIZE, true, -1);
    r = podReaderOpen("https://cdn.example/ep.mp3");
    if (r) {
        char buf[16];
        CHECK(podReaderRead(r, buf, sizeof(buf), NULL) == (int)sizeof(buf));
        podReaderClose(r);
    }
}

void testPodRead(void)
{
    testStraight();
    testSeeks(true);
    testSeeks(false);  // A server that always sends the whole file.
    testDropAndMissing();
}
