#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <curl/curl.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

#include "lang.h"

#define CONNECT_TIMEOUT_S 10L
#define TRANSFER_TIMEOUT_S 30L
#define MAX_REDIRECTS 5L
#define DOWNLOAD_STALL_S 30L  // A download with nothing coming this long has failed.

bool netInit(void)
{
#ifdef __SWITCH__
    if (R_FAILED(socketInitializeDefault()))
        return false;
#endif
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
#ifdef __SWITCH__
        socketExit();
#endif
        return false;
    }
    return true;
}

void netExit(void)
{
    curl_global_cleanup();
#ifdef __SWITCH__
    socketExit();
#endif
}

typedef struct {
    NetResponse *out;
    size_t max;
    bool too_big;
} Sink;

static size_t onData(char *data, size_t one, size_t count, void *user)
{
    Sink *sink = user;
    size_t n = one * count;
    if (sink->out->size + n > sink->max) {
        sink->too_big = true;
        return 0;
    }
    char *grown = realloc(sink->out->data, sink->out->size + n + 1);
    if (!grown)
        return 0;
    memcpy(grown + sink->out->size, data, n);
    sink->out->size += n;
    grown[sink->out->size] = '\0';
    sink->out->data = grown;
    return n;
}

static int onProgress(void *user, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
    (void)a, (void)b, (void)c, (void)d;
    const volatile bool *cancel = user;
    return cancel && *cancel ? 1 : 0;
}

// What every request sets: the address, only http and https (logos may be http; an https
// address, as the directory's and the weather's, may not be redirected to plain http),
// certificates checked, who asks, redirects and the connect timeout.
static void setCommon(CURL *curl, const char *url, char *error)
{
    curl_easy_setopt(curl, CURLOPT_URL, url);
    long redirects = strncasecmp(url, "https://", 8) == 0 ? CURLPROTO_HTTPS : CURLPROTO_HTTP | CURLPROTO_HTTPS;
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, redirects);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, NET_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, MAX_REDIRECTS);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
}

bool netGet(const char *url, size_t max_bytes, const volatile bool *cancel, NetResponse *out)
{
    memset(out, 0, sizeof(*out));
    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(out->error, sizeof(out->error), "%s", T("Out of memory"));
        return false;
    }
    char error[CURL_ERROR_SIZE] = "";
    Sink sink = {out, max_bytes, false};
    setCommon(curl, url, error);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, TRANSFER_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)cancel);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out->status);
    curl_easy_cleanup(curl);

    bool ok = rc == CURLE_OK && out->status >= 200 && out->status < 300;
    if (!ok) {
        if (sink.too_big)
            snprintf(out->error, sizeof(out->error), "%s", T("The answer is too large"));
        else if (rc != CURLE_OK)
            snprintf(out->error, sizeof(out->error), "%s", error[0] ? error : curl_easy_strerror(rc));
        else
            snprintf(out->error, sizeof(out->error), T("The server answered %ld"), out->status);
        netResponseFree(out);
    }
    return ok;
}

void netResponseFree(NetResponse *r)
{
    free(r->data);
    r->data = NULL;
    r->size = 0;
}

typedef struct {
    FILE *file;
    size_t max, written;
    bool too_big;
} FileSink;

static size_t onFileData(char *data, size_t one, size_t count, void *user)
{
    FileSink *sink = user;
    size_t n = one * count;
    if (sink->written + n > sink->max) {
        sink->too_big = true;
        return 0;
    }
    if (fwrite(data, 1, n, sink->file) != n)
        return 0;
    sink->written += n;
    return n;
}

typedef struct {
    const volatile bool *cancel;
    volatile float *progress;
} Watch;

static int onFileProgress(void *user, curl_off_t total, curl_off_t now, curl_off_t c, curl_off_t d)
{
    (void)c, (void)d;
    Watch *watch = user;
    if (watch->progress && total > 0)
        *watch->progress = (float)now / (float)total;
    return watch->cancel && *watch->cancel ? 1 : 0;
}

bool netDownloadFile(const char *url, const char *path, size_t max_bytes, const volatile bool *cancel,
                     volatile float *progress, NetResponse *out)
{
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "wb");
    CURL *curl = f ? curl_easy_init() : NULL;
    if (!curl) {
        snprintf(out->error, sizeof(out->error), "%s", T("It couldn't be written to the card."));
        if (f)
            fclose(f);
        remove(path);
        return false;
    }
    char error[CURL_ERROR_SIZE] = "";
    FileSink sink = {f, max_bytes, 0, false};
    Watch watch = {cancel, progress};
    setCommon(curl, url, error);
    // No limit on the whole transfer (a slow network may need minutes): it stops after
    // DOWNLOAD_STALL_S with nothing coming instead.
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, DOWNLOAD_STALL_S);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onFileData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onFileProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &watch);
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out->status);
    curl_easy_cleanup(curl);
    bool closed = fclose(f) == 0;
    out->size = sink.written;
    bool ok = rc == CURLE_OK && closed && out->status >= 200 && out->status < 300;
    if (!ok) {
        if (sink.too_big)
            snprintf(out->error, sizeof(out->error), "%s", T("The answer is too large"));
        else if (rc != CURLE_OK)
            snprintf(out->error, sizeof(out->error), "%s", error[0] ? error : curl_easy_strerror(rc));
        else if (!closed)
            snprintf(out->error, sizeof(out->error), "%s", T("It couldn't be written to the card."));
        else
            snprintf(out->error, sizeof(out->error), T("The server answered %ld"), out->status);
        remove(path);
    }
    return ok;
}
