#include "podread.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "link.h"
#include "net.h"

#define THREAD_STACK (512 * 1024)  // curl runs on it.
#define WAIT_MS 100
#define RETRY_PAUSE_US 1000000

struct PodReader {
    char url[LINK_URL_MAX];
    pthread_t thread;
    bool started;
    pthread_mutex_t lock;
    pthread_cond_t changed;  // Data came, room was made, or a seek moved things.
    uint8_t *buf;
    // The window held: bytes [base, base + len) of the file, at buf[offset % PODREAD_BUFFER].
    int64_t base;
    size_t len;
    int64_t pos;
    int64_t total;
    char content_type[64];
    bool ended, failed;
    char error[160];
    int retries;
    unsigned generation;     // Changes with each new request (a seek out of the window).
    volatile bool cancel;    // Stops the transfer running (a seek, or closing).
    volatile bool quit;
};

typedef struct {
    PodReader *r;
    unsigned generation;
    int64_t from;   // Where this request asked to start.
    int64_t skip;   // Bytes still to drop (a server that sent the whole file).
    bool first;
    int64_t got;    // Bytes this request put in.
} Transfer;

static void waitChanged(PodReader *r)
{
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += WAIT_MS * 1000000L;
    if (until.tv_nsec >= 1000000000L) {
        until.tv_sec++;
        until.tv_nsec -= 1000000000L;
    }
    pthread_cond_timedwait(&r->changed, &r->lock, &until);
}

// Copies bytes in at the window's end; waits for room, dropping what's
// behind the reader beyond PODREAD_KEEP_BACK. False if this request is no
// longer wanted. Called with the lock held.
static bool put(PodReader *r, const Transfer *t, const uint8_t *data, size_t size)
{
    while (size > 0) {
        if (r->quit || r->generation != t->generation)
            return false;
        size_t room = PODREAD_BUFFER - r->len;
        if (room == 0) {
            int64_t behind = r->pos - r->base - (int64_t)PODREAD_KEEP_BACK;
            if (behind > 0) {
                r->base += behind;
                r->len -= (size_t)behind;
                continue;
            }
            waitChanged(r);
            continue;
        }
        size_t n = size < room ? size : room;
        size_t at = (size_t)((r->base + (int64_t)r->len) % PODREAD_BUFFER);
        size_t first = PODREAD_BUFFER - at < n ? PODREAD_BUFFER - at : n;
        memcpy(r->buf + at, data, first);
        memcpy(r->buf, data + first, n - first);
        r->len += n;
        data += n;
        size -= n;
        pthread_cond_broadcast(&r->changed);
    }
    return true;
}

static bool onBytes(const NetStreamHead *head, const void *data, size_t size, void *user)
{
    Transfer *t = user;
    PodReader *r = t->r;
    pthread_mutex_lock(&r->lock);
    bool ok = !r->quit && r->generation == t->generation;
    if (ok && t->first) {
        t->first = false;
        if (head->total > 0)
            r->total = head->total;
        if (head->content_type[0])
            snprintf(r->content_type, sizeof(r->content_type), "%s", head->content_type);
        t->skip = head->start < t->from ? t->from - head->start : 0;
    }
    const uint8_t *bytes = data;
    if (ok && t->skip > 0) {
        size_t drop = (int64_t)size < t->skip ? size : (size_t)t->skip;
        t->skip -= (int64_t)drop;
        bytes += drop;
        size -= drop;
    }
    if (ok) {
        ok = put(r, t, bytes, size);
        t->got += (int64_t)size;
    }
    pthread_mutex_unlock(&r->lock);
    return ok;
}

// One request, from the window's end; then what it came to. Called with the lock held.
static void fetch(PodReader *r)
{
    Transfer t = {r, r->generation, r->base + (int64_t)r->len, 0, true, 0};
    r->cancel = false;
    pthread_mutex_unlock(&r->lock);
    char error[160];
    bool ok = netStream(r->url, t.from, &r->cancel, onBytes, &t, NULL, error, sizeof(error));
    pthread_mutex_lock(&r->lock);
    if (r->quit || r->generation != t.generation)
        return;  // A seek asked for another place: the loop starts it.
    bool short_answer = ok && r->total > 0 && r->base + (int64_t)r->len < r->total;
    if (ok && !short_answer) {
        r->ended = true;
        if (r->total < 0)
            r->total = r->base + (int64_t)r->len;
    } else if (r->retries < PODREAD_RETRIES && (short_answer || t.got > 0)) {
        r->retries++;  // The connection dropped after some came: picked up where it stopped.
        pthread_mutex_unlock(&r->lock);
        usleep(RETRY_PAUSE_US);
        pthread_mutex_lock(&r->lock);
    } else {
        r->failed = true;
        snprintf(r->error, sizeof(r->error), "%s", error[0] ? error : "The download stopped.");
    }
    pthread_cond_broadcast(&r->changed);
}

static void *worker(void *arg)
{
    PodReader *r = arg;
    pthread_mutex_lock(&r->lock);
    while (!r->quit) {
        if (!r->ended && !r->failed)
            fetch(r);
        else
            waitChanged(r);
    }
    pthread_mutex_unlock(&r->lock);
    return NULL;
}

PodReader *podReaderOpen(const char *url)
{
    PodReader *r = calloc(1, sizeof(PodReader));
    if (!r)
        return NULL;
    r->buf = malloc(PODREAD_BUFFER);
    if (!r->buf || strlen(url) >= sizeof(r->url)) {
        free(r->buf);
        free(r);
        return NULL;
    }
    snprintf(r->url, sizeof(r->url), "%s", url);
    r->total = -1;
    pthread_mutex_init(&r->lock, NULL);
    pthread_cond_init(&r->changed, NULL);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, THREAD_STACK);
    r->started = pthread_create(&r->thread, &attr, worker, r) == 0;
    pthread_attr_destroy(&attr);
    if (!r->started) {
        podReaderClose(r);
        return NULL;
    }
    return r;
}

void podReaderClose(PodReader *r)
{
    if (!r)
        return;
    pthread_mutex_lock(&r->lock);
    r->quit = true;
    r->cancel = true;
    pthread_cond_broadcast(&r->changed);
    pthread_mutex_unlock(&r->lock);
    if (r->started)
        pthread_join(r->thread, NULL);
    pthread_cond_destroy(&r->changed);
    pthread_mutex_destroy(&r->lock);
    free(r->buf);
    free(r);
}

int podReaderRead(PodReader *r, void *buf, int size, const volatile bool *abort)
{
    if (size <= 0)
        return 0;
    pthread_mutex_lock(&r->lock);
    int result;
    for (;;) {
        int64_t end = r->base + (int64_t)r->len;
        if (r->pos >= r->base && r->pos < end) {
            size_t have = (size_t)(end - r->pos), n = have < (size_t)size ? have : (size_t)size;
            size_t at = (size_t)(r->pos % PODREAD_BUFFER);
            size_t first = PODREAD_BUFFER - at < n ? PODREAD_BUFFER - at : n;
            memcpy(buf, r->buf + at, first);
            memcpy((uint8_t *)buf + first, r->buf, n - first);
            r->pos += (int64_t)n;
            r->retries = 0;
            pthread_cond_broadcast(&r->changed);  // Room may be made behind it.
            result = (int)n;
            break;
        }
        if (r->ended && r->pos >= end) {
            result = 0;
            break;
        }
        if (r->failed) {
            result = -1;
            break;
        }
        if (abort && *abort) {
            result = -2;
            break;
        }
        waitChanged(r);
    }
    pthread_mutex_unlock(&r->lock);
    return result;
}

int64_t podReaderSeek(PodReader *r, int64_t offset)
{
    pthread_mutex_lock(&r->lock);
    if (offset < 0)
        offset = 0;
    if (r->total > 0 && offset > r->total)
        offset = r->total;
    int64_t end = r->base + (int64_t)r->len;
    if (offset >= r->base && offset <= end + (int64_t)PODREAD_REACH && !(r->failed && offset >= end)) {
        r->pos = offset;  // Held, or about to come.
    } else {
        r->generation++;  // A new request from there.
        r->base = offset;
        r->len = 0;
        r->pos = offset;
        r->ended = r->total > 0 && offset >= r->total;
        r->failed = false;
        r->error[0] = '\0';
        r->retries = 0;
        r->cancel = true;
    }
    pthread_cond_broadcast(&r->changed);
    pthread_mutex_unlock(&r->lock);
    return offset;
}

int64_t podReaderSize(PodReader *r)
{
    pthread_mutex_lock(&r->lock);
    int64_t total = r->total;
    pthread_mutex_unlock(&r->lock);
    return total;
}

void podReaderInfo(PodReader *r, PodReaderInfo *out)
{
    pthread_mutex_lock(&r->lock);
    out->total = r->total;
    out->position = r->pos;
    int64_t end = r->base + (int64_t)r->len;
    out->ahead = end > r->pos ? end - r->pos : 0;
    out->ended = r->ended;
    out->failed = r->failed;
    snprintf(out->content_type, sizeof(out->content_type), "%s", r->content_type);
    snprintf(out->error, sizeof(out->error), "%s", r->error);
    pthread_mutex_unlock(&r->lock);
}
