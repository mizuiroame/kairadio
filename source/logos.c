#include "logos.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "link.h"
#include "net.h"

#define QUEUE_MAX 24

typedef enum {
    SLOT_EMPTY,
    SLOT_QUEUED,
    SLOT_LOADING,
    SLOT_READY,
    SLOT_FAILED,  // Not an image, or not reachable: not asked for again.
} SlotState;

typedef struct {
    SlotState state;
    char url[LINK_URL_MAX];
    uint32_t *pixels;     // LOGO_SIZE x LOGO_SIZE when READY.
    unsigned last_used;   // Frame number.
    unsigned queued_at;   // For newest-first loading.
} Slot;

static Slot g_slots[LOGO_CACHE];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;
static pthread_t g_thread;
static bool g_started;
static volatile bool g_quit;
static unsigned g_frame;
static unsigned g_queue_clock;

static Slot *find(const char *url)
{
    for (int i = 0; i < LOGO_CACHE; i++)
        if (g_slots[i].state != SLOT_EMPTY && strcmp(g_slots[i].url, url) == 0)
            return &g_slots[i];
    return NULL;
}

// A slot for a new url: an empty one, or the least recently used that isn't
// loading or used this frame. NULL if none.
static Slot *reuse(void)
{
    Slot *best = NULL;
    for (int i = 0; i < LOGO_CACHE; i++) {
        Slot *s = &g_slots[i];
        if (s->state == SLOT_EMPTY)
            return s;
        if (s->state == SLOT_LOADING || s->last_used == g_frame)
            continue;
        if (!best || s->last_used < best->last_used)
            best = s;
    }
    if (best && best->pixels) {
        gfxImageChanged(best->pixels);
        free(best->pixels);
        best->pixels = NULL;
    }
    return best;
}

static Slot *nextQueued(void)
{
    Slot *best = NULL;
    for (int i = 0; i < LOGO_CACHE; i++) {
        Slot *s = &g_slots[i];
        if (s->state == SLOT_QUEUED && (!best || s->queued_at > best->queued_at))
            best = s;
    }
    return best;
}

static void *worker(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g_lock);
    while (!g_quit) {
        Slot *s = nextQueued();
        if (!s) {
            pthread_cond_wait(&g_wake, &g_lock);
            continue;
        }
        s->state = SLOT_LOADING;
        char url[LINK_URL_MAX];
        snprintf(url, sizeof(url), "%s", s->url);
        pthread_mutex_unlock(&g_lock);

        uint32_t *pixels = malloc(LOGO_SIZE * LOGO_SIZE * sizeof(uint32_t));
        NetResponse r;
        bool ok = pixels && netGet(url, LOGO_FILE_MAX, &g_quit, &r);
        if (ok) {
            ok = logoDecode((const uint8_t *)r.data, r.size, pixels);
            netResponseFree(&r);
        }
        if (!ok) {
            free(pixels);
            pixels = NULL;
        }

        pthread_mutex_lock(&g_lock);
        s->pixels = pixels;
        s->state = ok ? SLOT_READY : SLOT_FAILED;
    }
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

bool logosInit(void)
{
    g_quit = false;
    g_started = pthread_create(&g_thread, NULL, worker, NULL) == 0;
    return g_started;
}

void logosExit(void)
{
    if (g_started) {
        pthread_mutex_lock(&g_lock);
        g_quit = true;
        pthread_cond_signal(&g_wake);
        pthread_mutex_unlock(&g_lock);
        pthread_join(g_thread, NULL);
        g_started = false;
    }
    for (int i = 0; i < LOGO_CACHE; i++) {
        free(g_slots[i].pixels);
        memset(&g_slots[i], 0, sizeof(Slot));
    }
}

const uint32_t *logosGet(const char *url)
{
    if (!url || !url[0] || strlen(url) >= LINK_URL_MAX)
        return NULL;
    pthread_mutex_lock(&g_lock);
    Slot *s = find(url);
    if (!s) {
        int queued = 0;
        for (int i = 0; i < LOGO_CACHE; i++)
            queued += g_slots[i].state == SLOT_QUEUED;
        s = queued < QUEUE_MAX ? reuse() : NULL;
        if (s) {
            snprintf(s->url, sizeof(s->url), "%s", url);
            s->state = SLOT_QUEUED;
            s->queued_at = ++g_queue_clock;
            pthread_cond_signal(&g_wake);
        }
    }
    const uint32_t *pixels = NULL;
    if (s) {
        s->last_used = g_frame;
        if (s->state == SLOT_QUEUED)
            s->queued_at = ++g_queue_clock;  // Still wanted: load it sooner.
        if (s->state == SLOT_READY)
            pixels = s->pixels;
    }
    pthread_mutex_unlock(&g_lock);
    return pixels;
}

void logosEndFrame(void)
{
    pthread_mutex_lock(&g_lock);
    g_frame++;
    // Logos queued but not asked for again are dropped from the queue.
    for (int i = 0; i < LOGO_CACHE; i++) {
        Slot *s = &g_slots[i];
        if (s->state == SLOT_QUEUED && g_frame - s->last_used > 2)
            s->state = SLOT_EMPTY;
    }
    pthread_mutex_unlock(&g_lock);
}
