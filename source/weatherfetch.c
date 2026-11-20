#include "weatherfetch.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "net.h"

#define CACHE_SIZE 48
#define KEY_MAX 8
#define ANSWER_MAX (16 * 1024)

typedef struct {
    char key[KEY_MAX];
    Weather weather;
    WeatherState state;
    uint64_t asked_ns;
} Entry;

static struct {
    pthread_t thread;
    bool started;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    volatile bool quit;
    Entry cache[CACHE_SIZE];
    int next;                // The entry a new key takes, round the cache.
    bool pending;
    char pending_key[KEY_MAX];
    float pending_lat, pending_lon;
} g = {.lock = PTHREAD_MUTEX_INITIALIZER, .wake = PTHREAD_COND_INITIALIZER};

static Entry *find(const char *key)
{
    for (int i = 0; i < CACHE_SIZE; i++)
        if (g.cache[i].state != WEATHER_NONE && !strcmp(g.cache[i].key, key))
            return &g.cache[i];
    return NULL;
}

static void *fetchThread(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g.lock);
    while (!g.quit) {
        if (!g.pending) {
            pthread_cond_wait(&g.wake, &g.lock);
            continue;
        }
        char key[KEY_MAX], url[WEATHER_URL_MAX];
        snprintf(key, sizeof(key), "%s", g.pending_key);
        weatherUrl(g.pending_lat, g.pending_lon, url, sizeof(url));
        g.pending = false;
        pthread_mutex_unlock(&g.lock);
        NetResponse r;
        Weather w;
        bool ok = netGet(url, ANSWER_MAX, &g.quit, &r) && weatherParse(r.data, &w);
        netResponseFree(&r);
        pthread_mutex_lock(&g.lock);
        Entry *e = find(key);
        if (e) {
            e->state = ok ? WEATHER_READY : WEATHER_FAILED;
            if (ok)
                e->weather = w;
        }
    }
    pthread_mutex_unlock(&g.lock);
    return NULL;
}

bool weatherFetchStart(void)
{
    if (g.started)
        return true;
    g.quit = false;
    g.started = pthread_create(&g.thread, NULL, fetchThread, NULL) == 0;
    return g.started;
}

void weatherFetchStop(void)
{
    if (!g.started)
        return;
    pthread_mutex_lock(&g.lock);
    g.quit = true;
    pthread_cond_broadcast(&g.wake);
    pthread_mutex_unlock(&g.lock);
    pthread_join(g.thread, NULL);
    g.started = false;
}

void weatherFetchAsk(const char *key, float lat, float lon, uint64_t now_ns)
{
    if (!g.started || !key[0])
        return;
    pthread_mutex_lock(&g.lock);
    Entry *e = find(key);
    bool fresh = e && (e->state == WEATHER_WAITING || now_ns - e->asked_ns < WEATHER_FRESH_NS);
    if (!fresh) {
        if (!e) {
            e = &g.cache[g.next];
            g.next = (g.next + 1) % CACHE_SIZE;
            memset(e, 0, sizeof(*e));
            snprintf(e->key, sizeof(e->key), "%s", key);
        }
        if (e->state != WEATHER_READY)  // A stale answer still shows while the new one comes.
            e->state = WEATHER_WAITING;
        e->asked_ns = now_ns;
        Entry *replaced = g.pending ? find(g.pending_key) : NULL;
        if (replaced && replaced != e && replaced->state == WEATHER_WAITING)
            replaced->state = WEATHER_NONE;  // Never asked after all: asked again when picked again.
        snprintf(g.pending_key, sizeof(g.pending_key), "%s", key);
        g.pending_lat = lat;
        g.pending_lon = lon;
        g.pending = true;
        pthread_cond_broadcast(&g.wake);
    }
    pthread_mutex_unlock(&g.lock);
}

WeatherState weatherFetchGet(const char *key, Weather *out)
{
    pthread_mutex_lock(&g.lock);
    Entry *e = find(key);
    WeatherState state = e ? e->state : WEATHER_NONE;
    if (state == WEATHER_READY)
        *out = e->weather;
    pthread_mutex_unlock(&g.lock);
    return state;
}
