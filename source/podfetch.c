#include "podfetch.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lang.h"
#include "net.h"

#define THREAD_STACK (512 * 1024)  // curl and the feed reader run on it.
#define SEARCH_MAX_BYTES (2u << 20)
#define WORDS_MAX 200
#define QUEUE_MAX 64

typedef struct FeedDone {
    char url[LINK_URL_MAX];
    Feed feed;
    char error[256];
    struct FeedDone *next;
} FeedDone;

static pthread_t g_thread;
static bool g_started;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;
static volatile bool g_quit;
static volatile bool g_cancel_search;  // A newer search came.

static bool g_search_pending;
static char g_words[WORDS_MAX];
static bool g_chart;      // The request is a chart (g_cc, g_genre), not words.
static char g_cc[3];
static int g_genre;
static PodSearchStatus g_status;
static PodcastResults g_results;

static char g_queue[QUEUE_MAX][LINK_URL_MAX];  // Feeds waiting.
static int g_queued;
static char g_reading[LINK_URL_MAX];           // The one being read.
static FeedDone *g_done, *g_done_last;          // Read, in order, not taken yet.
static int g_done_count;

// A chart: its ids, then a lookup of them for their feeds, in the chart's order.
static bool runChart(const char *cc, int genre, PodcastResults *found, NetResponse *r)
{
    char url[LINK_URL_MAX];
    long long ids[PODCAST_CHART_MAX];
    if (!podcastChartUrl(cc, genre, url, sizeof(url)) || !netGet(url, SEARCH_MAX_BYTES, &g_cancel_search, r))
        return false;
    int count = podcastParseChartIds(r->data, ids, PODCAST_CHART_MAX);
    netResponseFree(r);
    if (count == 0)
        return true;  // An empty chart.
    if (!podcastLookupUrl(ids, count, url, sizeof(url)) || !netGet(url, SEARCH_MAX_BYTES, &g_cancel_search, r) ||
        !podcastParseSearch(r->data, found))
        return false;
    podcastOrderByIds(found, ids, count);
    return true;
}

static void runSearch(const char *words, bool chart, const char *cc, int genre, unsigned generation)
{
    char url[LINK_URL_MAX];
    NetResponse r;
    memset(&r, 0, sizeof(r));
    PodcastResults found = {0};
    bool ok = chart ? runChart(cc, genre, &found, &r)
                    : podcastSearchUrl(words, url, sizeof(url)) && netGet(url, SEARCH_MAX_BYTES, &g_cancel_search, &r) &&
                          podcastParseSearch(r.data, &found);
    pthread_mutex_lock(&g_lock);
    if (generation == g_status.generation) {
        podcastResultsFree(&g_results);
        if (ok) {
            g_results = found;
            g_status.state = POD_SEARCH_READY;
        } else {
            podcastResultsFree(&found);
            g_status.state = POD_SEARCH_FAILED;
            snprintf(g_status.error, sizeof(g_status.error), "%s",
                     r.error[0] ? r.error : T("The podcast search didn't answer as expected."));
        }
    } else {
        podcastResultsFree(&found);
    }
    pthread_mutex_unlock(&g_lock);
    netResponseFree(&r);
}

static void readFeed(const char *url)
{
    FeedDone *done = calloc(1, sizeof(FeedDone));
    if (!done)
        return;
    snprintf(done->url, sizeof(done->url), "%s", url);
    NetResponse r;
    memset(&r, 0, sizeof(r));
    if (!netGet(url, PODCAST_FEED_MAX, &g_quit, &r))  // Closing KaiRadio stops it.
        snprintf(done->error, sizeof(done->error), "%s", r.error[0] ? r.error : T("The podcast couldn't be read."));
    else if (!podcastParseFeed(r.data, r.size, &done->feed))
        snprintf(done->error, sizeof(done->error), "%s", T("That address isn't a podcast's feed."));
    netResponseFree(&r);
    pthread_mutex_lock(&g_lock);
    if (g_done_last)
        g_done_last->next = done;
    else
        g_done = done;
    g_done_last = done;
    g_done_count++;
    g_reading[0] = '\0';
    pthread_mutex_unlock(&g_lock);
}

static void *worker(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g_lock);
    while (!g_quit) {
        if (g_search_pending) {
            g_search_pending = false;
            g_cancel_search = false;
            char words[WORDS_MAX], cc[3];
            snprintf(words, sizeof(words), "%s", g_words);
            snprintf(cc, sizeof(cc), "%s", g_cc);
            bool chart = g_chart;
            int genre = g_genre;
            unsigned generation = g_status.generation;
            pthread_mutex_unlock(&g_lock);
            runSearch(words, chart, cc, genre, generation);
            pthread_mutex_lock(&g_lock);
        } else if (g_queued) {
            snprintf(g_reading, sizeof(g_reading), "%s", g_queue[0]);
            memmove(g_queue[0], g_queue[1], (size_t)(g_queued - 1) * sizeof(g_queue[0]));
            g_queued--;
            char url[LINK_URL_MAX];
            snprintf(url, sizeof(url), "%s", g_reading);
            pthread_mutex_unlock(&g_lock);
            readFeed(url);
            pthread_mutex_lock(&g_lock);
        } else {
            pthread_cond_wait(&g_wake, &g_lock);
        }
    }
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

bool podFetchInit(void)
{
    g_quit = false;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, THREAD_STACK);
    g_started = pthread_create(&g_thread, &attr, worker, NULL) == 0;
    pthread_attr_destroy(&attr);
    return g_started;
}

void podFetchExit(void)
{
    if (!g_started)
        return;
    pthread_mutex_lock(&g_lock);
    g_quit = true;
    g_cancel_search = true;
    pthread_cond_signal(&g_wake);
    pthread_mutex_unlock(&g_lock);
    pthread_join(g_thread, NULL);
    g_started = false;
    podcastResultsFree(&g_results);
    while (g_done) {
        FeedDone *next = g_done->next;
        podcastFeedFree(&g_done->feed);
        free(g_done);
        g_done = next;
    }
    g_done_last = NULL;
    g_done_count = 0;
    g_queued = 0;
}

static void ask(void)  // With the lock held: the request set, it replaces the one before.
{
    g_search_pending = true;
    g_cancel_search = true;  // The worker clears it when it takes this request.
    g_status.generation++;
    g_status.state = POD_SEARCH_LOADING;
    g_status.error[0] = '\0';
    podcastResultsFree(&g_results);
    pthread_cond_signal(&g_wake);
}

void podFetchChart(const char *cc, int genre)
{
    pthread_mutex_lock(&g_lock);
    g_chart = true;
    snprintf(g_cc, sizeof(g_cc), "%s", cc ? cc : "");
    g_genre = genre;
    ask();
    pthread_mutex_unlock(&g_lock);
}

void podFetchSearch(const char *words)
{
    pthread_mutex_lock(&g_lock);
    g_chart = false;
    snprintf(g_words, sizeof(g_words), "%s", words ? words : "");
    ask();
    pthread_mutex_unlock(&g_lock);
}

void podFetchSearchStatus(PodSearchStatus *out)
{
    pthread_mutex_lock(&g_lock);
    *out = g_status;
    pthread_mutex_unlock(&g_lock);
}

bool podFetchTakeSearch(unsigned generation, PodcastResults *out)
{
    pthread_mutex_lock(&g_lock);
    bool ok = generation == g_status.generation && g_status.state == POD_SEARCH_READY;
    if (ok) {
        *out = g_results;
        memset(&g_results, 0, sizeof(g_results));
        g_status.state = POD_SEARCH_IDLE;
    }
    pthread_mutex_unlock(&g_lock);
    return ok;
}

void podFetchFeed(const char *feed)
{
    if (!feed || !feed[0] || strlen(feed) >= LINK_URL_MAX)
        return;
    pthread_mutex_lock(&g_lock);
    bool there = !strcmp(g_reading, feed);
    for (int i = 0; i < g_queued && !there; i++)
        there = !strcmp(g_queue[i], feed);
    if (!there && g_queued < QUEUE_MAX) {
        snprintf(g_queue[g_queued++], LINK_URL_MAX, "%s", feed);
        pthread_cond_signal(&g_wake);
    }
    pthread_mutex_unlock(&g_lock);
}

bool podFetchTakeFeed(char *url, size_t url_size, Feed *out, char *error, size_t error_size)
{
    pthread_mutex_lock(&g_lock);
    FeedDone *done = g_done;
    if (done) {
        g_done = done->next;
        if (!g_done)
            g_done_last = NULL;
        g_done_count--;
    }
    pthread_mutex_unlock(&g_lock);
    if (!done)
        return false;
    snprintf(url, url_size, "%s", done->url);
    *out = done->feed;
    snprintf(error, error_size, "%s", done->error);
    free(done);
    return true;
}

int podFetchFeedsPending(void)
{
    pthread_mutex_lock(&g_lock);
    int n = g_queued + (g_reading[0] ? 1 : 0) + g_done_count;
    pthread_mutex_unlock(&g_lock);
    return n;
}
