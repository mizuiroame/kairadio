#include "directory.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "net.h"
#include "util.h"

#define ANSWER_MAX (4u << 20)
#define SERVERS_URL "https://all.api.radio-browser.info/json/servers"
#define TEXT_MAX 128
#define SERVER_MAX 96
#define ESCAPED_MAX (TEXT_MAX * 3 + 1)

// Used when the server list can't be fetched.
static const char *const FALLBACK_SERVERS[] = {
    "de1.api.radio-browser.info",
    "de2.api.radio-browser.info",
    "fi1.api.radio-browser.info",
};
#define FALLBACK_COUNT (sizeof(FALLBACK_SERVERS) / sizeof(FALLBACK_SERVERS[0]))

static pthread_t g_thread;
static bool g_started;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;
static volatile bool g_quit;
static volatile bool g_cancel;  // Stops the running request for a newer one.

// Guarded by g_lock.
static bool g_pending;
static DirectoryQuery g_query;
static char g_text[TEXT_MAX];
static char g_click[STATION_UUID_MAX];
static DirectoryStatus g_status;
static StationList g_result;

// The worker's own.
static char g_server[SERVER_MAX];

bool directoryPath(DirectoryQuery query, const char *text, char *out, size_t size)
{
    static const char ORDER[] = "&hidebroken=true&order=clickcount&reverse=true";
    char escaped[ESCAPED_MAX];
    if (query != DIR_POPULAR && (!text || !text[0] || !utilUrlEscape(text, escaped, sizeof(escaped))))
        return false;
    int n;
    switch (query) {
    case DIR_POPULAR:
        n = snprintf(out, size, "/json/stations/topclick/%d?hidebroken=true", DIRECTORY_LIMIT);
        break;
    case DIR_SEARCH:
        n = snprintf(out, size, "/json/stations/search?name=%s&limit=%d%s", escaped, DIRECTORY_LIMIT, ORDER);
        break;
    case DIR_COUNTRY:
        n = snprintf(out, size, "/json/stations/search?countrycode=%s&limit=%d%s", escaped, DIRECTORY_LIMIT, ORDER);
        break;
    case DIR_TAG:
        n = snprintf(out, size, "/json/stations/search?tag=%s&tagExact=true&limit=%d%s", escaped, DIRECTORY_LIMIT,
                     ORDER);
        break;
    default:
        return false;
    }
    return n > 0 && (size_t)n < size;
}

// Picks one of the directory's servers at random, as it asks clients to.
static void chooseServer(void)
{
    NetResponse r;
    if (netGet(SERVERS_URL, 64 * 1024, &g_quit, &r)) {
        cJSON *root = cJSON_Parse(r.data);
        int count = cJSON_IsArray(root) ? cJSON_GetArraySize(root) : 0;
        if (count > 0) {
            const cJSON *pick = cJSON_GetArrayItem(root, rand() % count);
            const cJSON *name = cJSON_GetObjectItemCaseSensitive(pick, "name");
            if (cJSON_IsString(name) && strlen(name->valuestring) < sizeof(g_server))
                snprintf(g_server, sizeof(g_server), "%s", name->valuestring);
        }
        cJSON_Delete(root);
        netResponseFree(&r);
    }
    if (!g_server[0])
        snprintf(g_server, sizeof(g_server), "%s", FALLBACK_SERVERS[rand() % FALLBACK_COUNT]);
}

// GETs path from the chosen server; on failure, once more from another one.
static bool fetch(const char *path, NetResponse *r)
{
    char url[512];
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!g_server[0] || attempt > 0) {
            g_server[0] = '\0';
            if (attempt > 0)
                snprintf(g_server, sizeof(g_server), "%s", FALLBACK_SERVERS[rand() % FALLBACK_COUNT]);
            else
                chooseServer();
        }
        snprintf(url, sizeof(url), "https://%s%s", g_server, path);
        if (netGet(url, ANSWER_MAX, &g_cancel, r))
            return true;
        if (g_cancel || g_quit)
            return false;
    }
    return false;
}

static void runQuery(DirectoryQuery query, const char *text, unsigned generation)
{
    char path[512];
    StationList list = {0};
    char error[128] = "";
    NetResponse r;
    if (!directoryPath(query, text, path, sizeof(path))) {
        snprintf(error, sizeof(error), "That search is too long.");
    } else if (!fetch(path, &r)) {
        snprintf(error, sizeof(error), "%s", r.error[0] ? r.error : "The directory can't be reached.");
    } else {
        if (!stationsParse(r.data, &list))
            snprintf(error, sizeof(error), "The directory sent an answer KaiRadio can't read.");
        netResponseFree(&r);
    }

    pthread_mutex_lock(&g_lock);
    if (g_status.generation == generation && !g_cancel) {
        stationListFree(&g_result);
        g_result = list;
        list = (StationList){0};
        g_status.state = error[0] ? DIR_FAILED : DIR_READY;
        snprintf(g_status.error, sizeof(g_status.error), "%s", error);
    }
    pthread_mutex_unlock(&g_lock);
    stationListFree(&list);
}

static void sendClick(const char *uuid)
{
    char path[128];
    snprintf(path, sizeof(path), "/json/url/%s", uuid);
    NetResponse r;
    if (fetch(path, &r))
        netResponseFree(&r);
}

static void *worker(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g_lock);
    while (!g_quit) {
        if (g_pending) {
            g_pending = false;
            g_cancel = false;
            DirectoryQuery query = g_query;
            char text[TEXT_MAX];
            snprintf(text, sizeof(text), "%s", g_text);
            unsigned generation = g_status.generation;
            pthread_mutex_unlock(&g_lock);
            runQuery(query, text, generation);
            pthread_mutex_lock(&g_lock);
        } else if (g_click[0]) {
            char uuid[STATION_UUID_MAX];
            snprintf(uuid, sizeof(uuid), "%s", g_click);
            g_click[0] = '\0';
            pthread_mutex_unlock(&g_lock);
            sendClick(uuid);
            pthread_mutex_lock(&g_lock);
        } else {
            pthread_cond_wait(&g_wake, &g_lock);
        }
    }
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

bool directoryInit(void)
{
    g_quit = false;
    g_started = pthread_create(&g_thread, NULL, worker, NULL) == 0;
    return g_started;
}

void directoryExit(void)
{
    if (!g_started)
        return;
    pthread_mutex_lock(&g_lock);
    g_quit = true;
    g_cancel = true;
    pthread_cond_signal(&g_wake);
    pthread_mutex_unlock(&g_lock);
    pthread_join(g_thread, NULL);
    g_started = false;
    stationListFree(&g_result);
}

void directoryRequest(DirectoryQuery query, const char *text)
{
    pthread_mutex_lock(&g_lock);
    g_query = query;
    snprintf(g_text, sizeof(g_text), "%s", text ? text : "");
    g_pending = true;
    g_cancel = true;  // The worker clears it when it takes this request.
    g_status.generation++;
    g_status.state = DIR_LOADING;
    g_status.error[0] = '\0';
    stationListFree(&g_result);
    pthread_cond_signal(&g_wake);
    pthread_mutex_unlock(&g_lock);
}

void directoryGetStatus(DirectoryStatus *out)
{
    pthread_mutex_lock(&g_lock);
    *out = g_status;
    pthread_mutex_unlock(&g_lock);
}

bool directoryTake(unsigned generation, StationList *out)
{
    pthread_mutex_lock(&g_lock);
    bool ok = g_status.generation == generation && g_status.state == DIR_READY;
    if (ok) {
        *out = g_result;
        g_result = (StationList){0};
    }
    pthread_mutex_unlock(&g_lock);
    return ok;
}

void directoryCountClick(const char *uuid)
{
    if (!uuid || !uuid[0])
        return;
    pthread_mutex_lock(&g_lock);
    snprintf(g_click, sizeof(g_click), "%s", uuid);
    pthread_cond_signal(&g_wake);
    pthread_mutex_unlock(&g_lock);
}
