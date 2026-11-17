#include "stream.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>

#include "hls.h"
#include "icy.h"
#include "link.h"
#include "net.h"

#define CONNECT_TIMEOUT_S 15L
#define STALL_TIMEOUT_S 30L      // No data for this long drops the connection.
#define MAX_REDIRECTS 8L
#define PLAYLIST_MAX (256 * 1024)
#define HEAD_MAX 4096            // Bytes looked at to tell audio from a playlist.
#define MAX_PLAYLIST_HOPS 4      // .pls -> .m3u -> stream.
#define MAX_RETRIES 6
#define HLS_MAX_BANDWIDTH 320000L
#define HLS_MIN_WAIT_MS 1000
#define SLEEP_STEP_MS 100

static pthread_t g_thread;
static bool g_running_thread;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile bool g_stop;

// Guarded by g_lock.
static StreamInfo g_info;

// The thread's own.
static Ring *g_ring;
static char g_url[LINK_URL_MAX];

static void setState(StreamState state, const char *error)
{
    pthread_mutex_lock(&g_lock);
    g_info.state = state;
    if (error)
        snprintf(g_info.error, sizeof(g_info.error), "%s", error);
    pthread_mutex_unlock(&g_lock);
}

static void sleepMs(unsigned ms)
{
    for (unsigned waited = 0; waited < ms && !g_stop; waited += SLEEP_STEP_MS)
        usleep(SLEEP_STEP_MS * 1000);
}

// One HTTP request: what it found out and what it collected.
typedef struct {
    char content_type[64];
    long metaint;
    int bitrate;
    bool decided;     // The kind is known (from the first bytes).
    LinkKind kind;
    bool audio_seen;  // Audio bytes went to the ring.
    char *body;       // A playlist's text.
    size_t body_size;
    bool body_too_big;
    IcyParser icy;
    uint8_t audio[CURL_MAX_WRITE_SIZE];
    bool audio_only;  // HLS segments: everything is audio, no ICY.
} Request;

static void onIcyBlock(const char *block, void *user)
{
    (void)user;
    char title[sizeof(g_info.title)];
    if (!icyStreamTitle(block, title, sizeof(title)))
        title[0] = '\0';
    pthread_mutex_lock(&g_lock);
    if (strcmp(title, g_info.title) != 0) {
        snprintf(g_info.title, sizeof(g_info.title), "%s", title);
        g_info.title_changes++;
    }
    pthread_mutex_unlock(&g_lock);
}

static size_t onHeader(char *line, size_t one, size_t count, void *user)
{
    Request *r = user;
    size_t n = one * count;
    char text[256];
    size_t len = n < sizeof(text) - 1 ? n : sizeof(text) - 1;
    memcpy(text, line, len);
    text[len] = '\0';
    text[strcspn(text, "\r\n")] = '\0';
    // A redirect starts a new set of headers.
    if (strncmp(text, "HTTP/", 5) == 0 || strncmp(text, "ICY ", 4) == 0) {
        r->content_type[0] = '\0';
        r->metaint = 0;
    } else if (strncasecmp(text, "content-type:", 13) == 0) {
        snprintf(r->content_type, sizeof(r->content_type), "%s", text + 13 + strspn(text + 13, " \t"));
    } else if (strncasecmp(text, "icy-metaint:", 12) == 0) {
        r->metaint = atol(text + 12);
        if (r->metaint < 0 || r->metaint > 1 << 20)
            r->metaint = 0;
    } else if (strncasecmp(text, "icy-br:", 7) == 0) {
        r->bitrate = atoi(text + 7);
    }
    return n;
}

static bool keepBody(Request *r, const char *data, size_t n)
{
    if (r->body_size + n > PLAYLIST_MAX) {
        r->body_too_big = true;
        return false;
    }
    char *grown = realloc(r->body, r->body_size + n + 1);
    if (!grown)
        return false;
    memcpy(grown + r->body_size, data, n);
    r->body_size += n;
    grown[r->body_size] = '\0';
    r->body = grown;
    return true;
}

static size_t onData(char *data, size_t one, size_t count, void *user)
{
    Request *r = user;
    size_t n = one * count;
    if (g_stop)
        return 0;
    if (!r->decided) {
        if (r->audio_only) {
            r->kind = LINK_AUDIO;
        } else {
            size_t head = n < HEAD_MAX ? n : HEAD_MAX;
            r->kind = linkKind(g_url, r->content_type, data, head);
            if (r->kind == LINK_AUDIO) {
                icyInit(&r->icy, (size_t)r->metaint);
                pthread_mutex_lock(&g_lock);
                snprintf(g_info.content_type, sizeof(g_info.content_type), "%s", r->content_type);
                if (r->bitrate > 0)
                    g_info.bitrate = r->bitrate;
                g_info.state = STREAM_RECEIVING;
                pthread_mutex_unlock(&g_lock);
            }
        }
        r->decided = true;
    }
    if (r->kind != LINK_AUDIO)
        return keepBody(r, data, n) ? n : 0;
    for (size_t done = 0; done < n;) {
        size_t chunk = n - done < sizeof(r->audio) ? n - done : sizeof(r->audio);
        size_t audio = r->audio_only ? (memcpy(r->audio, data + done, chunk), chunk)
                                     : icyFeed(&r->icy, (const uint8_t *)data + done, chunk, r->audio, onIcyBlock, NULL);
        if (audio && !ringWrite(g_ring, r->audio, audio))
            return 0;
        r->audio_seen |= audio > 0;
        done += chunk;
    }
    return n;
}

static int onProgress(void *user, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
    (void)user, (void)a, (void)b, (void)c, (void)d;
    return g_stop ? 1 : 0;
}

// Runs one GET of url. ICY metadata is asked for unless audio_only.
static CURLcode perform(const char *url, Request *r, char *error, size_t error_size)
{
    CURL *curl = curl_easy_init();
    if (!curl)
        return CURLE_OUT_OF_MEMORY;
    char curl_error[CURL_ERROR_SIZE] = "";
    struct curl_slist *headers = r->audio_only ? NULL : curl_slist_append(NULL, "Icy-MetaData: 1");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, NET_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, MAX_REDIRECTS);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, STALL_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, onHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, r);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, r);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK)
        snprintf(error, error_size, "%s", curl_error[0] ? curl_error : curl_easy_strerror(rc));
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return rc;
}

static void freeRequest(Request *r)
{
    free(r->body);
    r->body = NULL;
    r->body_size = 0;
}

// Fetches a playlist's text (HLS). False on failure, with error set.
static bool fetchText(const char *url, Request *r, char *error, size_t error_size)
{
    memset(r, 0, sizeof(*r));
    r->decided = true;
    r->kind = LINK_HLS;
    if (perform(url, r, error, error_size) != CURLE_OK) {
        freeRequest(r);
        return false;
    }
    if (!r->body) {
        snprintf(error, error_size, "The station's playlist is empty.");
        return false;
    }
    return true;
}

static void setHls(int bitrate)
{
    pthread_mutex_lock(&g_lock);
    g_info.hls = true;
    if (bitrate > 0)
        g_info.bitrate = bitrate;
    pthread_mutex_unlock(&g_lock);
}

// Plays an HLS stream whose playlist text is body. Returns when it ends,
// fails or is stopped.
static void playHls(const char *url, char *body)
{
    char error[160] = "";
    char playlist_url[LINK_URL_MAX];
    snprintf(playlist_url, sizeof(playlist_url), "%s", url);
    HlsPlaylist p;
    if (!hlsParse(body, playlist_url, HLS_MAX_BANDWIDTH, &p)) {
        setState(STREAM_FAILED, "The station's HLS playlist can't be read.");
        return;
    }
    if (p.master) {
        snprintf(playlist_url, sizeof(playlist_url), "%s", p.variant);
        setHls((int)(p.variant_bandwidth / 1000));
        Request r;
        if (!fetchText(playlist_url, &r, error, sizeof(error)) || !hlsParse(r.body, playlist_url, HLS_MAX_BANDWIDTH, &p) ||
            p.master) {
            freeRequest(&r);
            setState(STREAM_FAILED, error[0] ? error : "The station's HLS playlist can't be read.");
            return;
        }
        freeRequest(&r);
    } else {
        setHls(0);
    }

    long last = -1;
    int failures = 0;
    bool map_sent = false;  // fMP4: the init segment went first, once.
    while (!g_stop) {
        if (p.encrypted || (p.fragmented_mp4 && !p.map[0])) {
            setState(STREAM_FAILED, p.encrypted ? "This station's stream is encrypted." :
                                                  "This station's HLS format (fMP4 by byte range) isn't supported.");
            break;
        }
        if (p.fragmented_mp4 && !map_sent && hlsFirstSegment(&p, last) < p.count) {
            // The init segment describes the audio; the segments after it hold
            // it. Together they read as one fragmented MP4.
            Request init;
            memset(&init, 0, sizeof(init));
            init.audio_only = true;
            CURLcode rc = perform(p.map, &init, error, sizeof(error));
            freeRequest(&init);
            if (rc != CURLE_OK) {
                if (!g_stop)
                    setState(STREAM_FAILED, error[0] ? error : "The station's stream couldn't be read.");
                break;
            }
            map_sent = true;
        }
        for (size_t i = hlsFirstSegment(&p, last); i < p.count && !g_stop; i++) {
            Request seg;
            memset(&seg, 0, sizeof(seg));
            seg.audio_only = true;
            if (perform(p.segments[i].url, &seg, error, sizeof(error)) != CURLE_OK && !g_stop) {
                freeRequest(&seg);
                break;  // Try the playlist again.
            }
            freeRequest(&seg);
            last = p.segments[i].sequence;
            setState(STREAM_RECEIVING, NULL);
        }
        if (g_stop || p.ended)
            break;
        double wait = p.target_duration > 0 ? p.target_duration / 2 : 2;
        sleepMs(wait * 1000 < HLS_MIN_WAIT_MS ? HLS_MIN_WAIT_MS : (unsigned)(wait * 1000));
        hlsFree(&p);
        Request r;
        if (!fetchText(playlist_url, &r, error, sizeof(error)) || !hlsParse(r.body, playlist_url, HLS_MAX_BANDWIDTH, &p)) {
            freeRequest(&r);
            memset(&p, 0, sizeof(p));
            if (++failures > MAX_RETRIES) {
                setState(STREAM_FAILED, error[0] ? error : "The station stopped answering.");
                break;
            }
            setState(STREAM_RECONNECTING, NULL);
            sleepMs(1000u << (failures < 4 ? failures : 4));
            continue;
        }
        freeRequest(&r);
        failures = 0;
    }
    hlsFree(&p);
    if (!g_stop && p.ended)
        setState(STREAM_ENDED, NULL);
}

// Plays url: follows playlists, and reconnects a dropped stream.
static void run(void)
{
    char url[LINK_URL_MAX];
    snprintf(url, sizeof(url), "%s", g_url);
    int hops = 0, retries = 0;
    while (!g_stop) {
        snprintf(g_url, sizeof(g_url), "%s", url);
        Request *r = calloc(1, sizeof(Request));
        if (!r) {
            setState(STREAM_FAILED, "Out of memory.");
            return;
        }
        char error[160] = "";
        CURLcode rc = perform(url, r, error, sizeof(error));
        if (g_stop) {
            freeRequest(r);
            free(r);
            return;
        }
        LinkKind kind = r->decided ? r->kind : LINK_AUDIO;
        bool got_audio = r->audio_seen;
        if (kind == LINK_PLS || kind == LINK_M3U) {
            char next[LINK_URL_MAX];
            bool found = r->body && linkFirstUrl(kind, r->body, url, next, sizeof(next));
            freeRequest(r);
            free(r);
            if (!found || ++hops > MAX_PLAYLIST_HOPS) {
                setState(STREAM_FAILED, found ? "The station's playlists lead in circles." :
                                                "The station's playlist has no stream in it.");
                return;
            }
            snprintf(url, sizeof(url), "%s", next);
            continue;
        }
        if (kind == LINK_HLS) {
            char *body = r->body;
            r->body = NULL;
            free(r);
            if (body)
                playHls(url, body);
            else
                setState(STREAM_FAILED, error[0] ? error : "The station's playlist is empty.");
            free(body);
            return;
        }
        freeRequest(r);
        free(r);

        // Audio that stopped: reconnect, unless it never worked.
        if (got_audio)
            retries = 0;
        if (rc == CURLE_OK && !got_audio) {
            setState(STREAM_FAILED, "The station sent nothing.");
            return;
        }
        if (++retries > MAX_RETRIES || (!got_audio && retries > 2)) {
            setState(STREAM_FAILED, error[0] ? error : "The station stopped answering.");
            return;
        }
        setState(STREAM_RECONNECTING, NULL);
        sleepMs(500u << (retries < 4 ? retries : 4));
    }
}

static void *threadMain(void *arg)
{
    (void)arg;
    run();
    ringEnd(g_ring);
    return NULL;
}

bool streamInit(void)
{
    return true;
}

void streamExit(void)
{
    streamStop();
}

void streamStart(Ring *ring, const char *url)
{
    streamStop();
    ringReset(ring);
    g_ring = ring;
    snprintf(g_url, sizeof(g_url), "%s", url);
    pthread_mutex_lock(&g_lock);
    memset(&g_info, 0, sizeof(g_info));
    g_info.state = STREAM_CONNECTING;
    pthread_mutex_unlock(&g_lock);
    g_stop = false;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256 * 1024);
    g_running_thread = pthread_create(&g_thread, &attr, threadMain, NULL) == 0;
    pthread_attr_destroy(&attr);
    if (!g_running_thread)
        setState(STREAM_FAILED, "KaiRadio couldn't start the download.");
}

void streamStop(void)
{
    if (!g_running_thread)
        return;
    g_stop = true;
    ringCancel(g_ring);
    pthread_join(g_thread, NULL);
    g_running_thread = false;
    pthread_mutex_lock(&g_lock);
    g_info.state = STREAM_IDLE;
    pthread_mutex_unlock(&g_lock);
}

void streamGetInfo(StreamInfo *out)
{
    pthread_mutex_lock(&g_lock);
    *out = g_info;
    pthread_mutex_unlock(&g_lock);
}
