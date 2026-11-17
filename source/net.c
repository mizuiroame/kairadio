#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

#define CONNECT_TIMEOUT_S 10L
#define TRANSFER_TIMEOUT_S 30L
#define MAX_REDIRECTS 5L

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

bool netGet(const char *url, size_t max_bytes, const volatile bool *cancel, NetResponse *out)
{
    memset(out, 0, sizeof(*out));
    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(out->error, sizeof(out->error), "Out of memory");
        return false;
    }
    char error[CURL_ERROR_SIZE] = "";
    Sink sink = {out, max_bytes, false};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, NET_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, MAX_REDIRECTS);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, TRANSFER_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
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
            snprintf(out->error, sizeof(out->error), "The answer is too large");
        else if (rc != CURLE_OK)
            snprintf(out->error, sizeof(out->error), "%s", error[0] ? error : curl_easy_strerror(rc));
        else
            snprintf(out->error, sizeof(out->error), "The server answered %ld", out->status);
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
