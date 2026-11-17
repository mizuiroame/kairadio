#include "ring.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

bool ringInit(Ring *r, size_t capacity)
{
    memset(r, 0, sizeof(*r));
    r->data = malloc(capacity);
    if (!r->data)
        return false;
    r->capacity = capacity;
    pthread_mutex_init(&r->lock, NULL);
    pthread_cond_init(&r->changed, NULL);
    return true;
}

void ringFree(Ring *r)
{
    if (!r->data)
        return;
    pthread_cond_destroy(&r->changed);
    pthread_mutex_destroy(&r->lock);
    free(r->data);
    r->data = NULL;
}

void ringReset(Ring *r)
{
    pthread_mutex_lock(&r->lock);
    r->head = 0;
    r->fill = 0;
    r->ended = false;
    r->cancelled = false;
    pthread_cond_broadcast(&r->changed);
    pthread_mutex_unlock(&r->lock);
}

bool ringWrite(Ring *r, const uint8_t *data, size_t size)
{
    pthread_mutex_lock(&r->lock);
    while (size > 0) {
        while (r->fill == r->capacity && !r->cancelled)
            pthread_cond_wait(&r->changed, &r->lock);
        if (r->cancelled)
            break;
        size_t tail = (r->head + r->fill) % r->capacity;
        size_t room = r->capacity - r->fill;
        size_t run = r->capacity - tail;  // Up to the end of the buffer.
        size_t n = size < room ? size : room;
        if (n > run)
            n = run;
        memcpy(r->data + tail, data, n);
        r->fill += n;
        data += n;
        size -= n;
        pthread_cond_broadcast(&r->changed);
    }
    bool ok = !r->cancelled;
    pthread_mutex_unlock(&r->lock);
    return ok;
}

static void deadlineIn(struct timespec *ts, unsigned ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

size_t ringRead(Ring *r, uint8_t *out, size_t size, unsigned timeout_ms)
{
    struct timespec deadline;
    deadlineIn(&deadline, timeout_ms);
    pthread_mutex_lock(&r->lock);
    while (r->fill == 0 && !r->ended && !r->cancelled) {
        if (pthread_cond_timedwait(&r->changed, &r->lock, &deadline) == ETIMEDOUT)
            break;
    }
    size_t total = 0;
    if (!r->cancelled) {
        while (total < size && r->fill > 0) {
            size_t run = r->capacity - r->head;
            size_t n = size - total;
            if (n > r->fill)
                n = r->fill;
            if (n > run)
                n = run;
            memcpy(out + total, r->data + r->head, n);
            r->head = (r->head + n) % r->capacity;
            r->fill -= n;
            total += n;
        }
        if (total)
            pthread_cond_broadcast(&r->changed);
    }
    pthread_mutex_unlock(&r->lock);
    return total;
}

void ringEnd(Ring *r)
{
    pthread_mutex_lock(&r->lock);
    r->ended = true;
    pthread_cond_broadcast(&r->changed);
    pthread_mutex_unlock(&r->lock);
}

void ringCancel(Ring *r)
{
    pthread_mutex_lock(&r->lock);
    r->cancelled = true;
    pthread_cond_broadcast(&r->changed);
    pthread_mutex_unlock(&r->lock);
}

bool ringFinished(Ring *r)
{
    pthread_mutex_lock(&r->lock);
    bool done = r->cancelled || (r->ended && r->fill == 0);
    pthread_mutex_unlock(&r->lock);
    return done;
}

size_t ringFill(Ring *r)
{
    pthread_mutex_lock(&r->lock);
    size_t fill = r->fill;
    pthread_mutex_unlock(&r->lock);
    return fill;
}
