// net.c for the host tests: answers from a table the tests fill, so the
// directory can be tested without a network.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_net.h"
#include "net.h"

#define FAKE_MAX 16

static struct {
    char url_part[128];
    char *body;
    long status;
} g_answers[FAKE_MAX];
static int g_count;
static int g_requests;

void fakeNetAnswer(const char *url_part, const char *body, long status)
{
    if (g_count < FAKE_MAX) {
        snprintf(g_answers[g_count].url_part, sizeof(g_answers[0].url_part), "%s", url_part);
        g_answers[g_count].body = strdup(body);
        g_answers[g_count].status = status;
        g_count++;
    }
}

void fakeNetReset(void)
{
    for (int i = 0; i < g_count; i++)
        free(g_answers[i].body);
    g_count = 0;
    g_requests = 0;
}

int fakeNetRequests(void)
{
    return g_requests;
}

bool netInit(void)
{
    return true;
}

void netExit(void)
{
}

bool netGet(const char *url, size_t max_bytes, const volatile bool *cancel, NetResponse *out)
{
    (void)cancel;
    memset(out, 0, sizeof(*out));
    g_requests++;
    for (int i = 0; i < g_count; i++) {
        if (!strstr(url, g_answers[i].url_part))
            continue;
        out->status = g_answers[i].status;
        if (out->status < 200 || out->status >= 300 || strlen(g_answers[i].body) > max_bytes) {
            snprintf(out->error, sizeof(out->error), "The server answered %ld", out->status);
            return false;
        }
        out->data = strdup(g_answers[i].body);
        out->size = strlen(out->data);
        return true;
    }
    snprintf(out->error, sizeof(out->error), "Couldn't resolve host name");
    return false;
}

void netResponseFree(NetResponse *r)
{
    free(r->data);
    r->data = NULL;
    r->size = 0;
}

// --- A file streamed in pieces (podread.c) ---

#define FAKE_CHUNK 65536

static struct {
    char url_part[128];
    int64_t size;
    bool ranges;
    int64_t drop_after;  // Once: the connection drops after this many bytes; -1 never.
    int streams;
} g_file;

uint8_t fakeNetFileByte(int64_t offset)
{
    return (uint8_t)(offset * 7 + offset / 251);
}

void fakeNetFile(const char *url_part, int64_t size, bool ranges, int64_t drop_after)
{
    snprintf(g_file.url_part, sizeof(g_file.url_part), "%s", url_part);
    g_file.size = size;
    g_file.ranges = ranges;
    g_file.drop_after = drop_after;
    g_file.streams = 0;
}

int fakeNetStreams(void)
{
    return g_file.streams;
}

bool netStream(const char *url, int64_t from, const volatile bool *cancel, NetStreamSink sink, void *user,
               NetStreamHead *head_out, char *error, size_t error_size)
{
    error[0] = '\0';
    g_file.streams++;
    if (!g_file.url_part[0] || !strstr(url, g_file.url_part)) {
        snprintf(error, error_size, "The server answered 404");
        return false;
    }
    NetStreamHead head = {.total = g_file.size};
    snprintf(head.content_type, sizeof(head.content_type), "audio/mpeg");
    int64_t at = g_file.ranges ? from : 0;
    head.status = g_file.ranges && from > 0 ? 206 : 200;
    head.start = at;
    if (head_out)
        *head_out = head;
    uint8_t chunk[FAKE_CHUNK];
    int64_t sent = 0;
    while (at < g_file.size) {
        if (cancel && *cancel)
            return false;
        if (g_file.drop_after >= 0 && sent >= g_file.drop_after) {
            g_file.drop_after = -1;
            snprintf(error, error_size, "Connection reset");
            return false;
        }
        size_t n = g_file.size - at < FAKE_CHUNK ? (size_t)(g_file.size - at) : FAKE_CHUNK;
        for (size_t i = 0; i < n; i++)
            chunk[i] = fakeNetFileByte(at + (int64_t)i);
        if (!sink(&head, chunk, n, user))
            return false;
        at += (int64_t)n;
        sent += (int64_t)n;
    }
    return true;
}
