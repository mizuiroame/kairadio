// KaiRadio network test: checks that HTTPS, the Radio Browser directory and
// radio streams (HTTP and HTTPS, with ICY metadata) work on the console.
// Prints each result and writes them to sdmc:/kairadio-probe.txt.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>
#include <curl/curl.h>

#define LOG_PATH      "sdmc:/kairadio-probe.txt"
#define USER_AGENT    "KaiRadio-probe/0.1"
#define API           "https://de1.api.radio-browser.info"
#define TIMEOUT_S     20L
#define STREAM_BYTES  (384 * 1024)
#define SHOW_CHARS    300

static FILE* g_log;

static void say(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    if (g_log) {
        va_start(args, fmt);
        vfprintf(g_log, fmt, args);
        va_end(args);
        fflush(g_log);
    }
    consoleUpdate(NULL);
}

typedef struct {
    char text[8192];
    size_t size;      // Bytes received, including those not kept.
    size_t limit;     // Stop after this many bytes; 0 for no limit.
    char metaint[32];
    char type[64];
    char name[96];
} Body;

static size_t onBody(char* data, size_t one, size_t count, void* user) {
    Body* body = user;
    size_t n = one * count;
    size_t used = strlen(body->text);
    size_t room = sizeof(body->text) - 1 - used;
    if (room && !body->limit) {
        size_t keep = n < room ? n : room;
        memcpy(body->text + used, data, keep);
    }
    body->size += n;
    if (body->limit && body->size >= body->limit)
        return 0; // Enough of the stream: stop here.
    return n;
}

static void copyHeader(char* out, size_t size, const char* line, size_t n, const char* name) {
    size_t len = strlen(name);
    if (n <= len || strncasecmp(line, name, len) != 0)
        return;
    line += len;
    n -= len;
    while (n && (*line == ' ' || *line == '\t')) { line++; n--; }
    while (n && (line[n - 1] == '\r' || line[n - 1] == '\n')) n--;
    if (n >= size) n = size - 1;
    memcpy(out, line, n);
    out[n] = 0;
}

static size_t onHeader(char* line, size_t one, size_t count, void* user) {
    Body* body = user;
    size_t n = one * count;
    copyHeader(body->metaint, sizeof(body->metaint), line, n, "icy-metaint:");
    copyHeader(body->type, sizeof(body->type), line, n, "content-type:");
    copyHeader(body->name, sizeof(body->name), line, n, "icy-name:");
    return n;
}

// Fetches url; a stream stops after STREAM_BYTES. Returns whether it worked.
static bool fetch(const char* title, const char* url, bool stream, bool verify) {
    say("\n== %s\n   %s\n", title, url);
    static Body body;
    memset(&body, 0, sizeof(body));
    body.limit = stream ? STREAM_BYTES : 0;

    CURL* curl = curl_easy_init();
    if (!curl) {
        say("   curl_easy_init failed\n");
        return false;
    }
    char error[CURL_ERROR_SIZE] = "";
    struct curl_slist* headers = stream ? curl_slist_append(NULL, "Icy-MetaData: 1") : NULL;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, onHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify ? 2L : 0L);

    u64 start = armGetSystemTick();
    CURLcode rc = curl_easy_perform(curl);
    double seconds = armTicksToNs(armGetSystemTick() - start) / 1e9;
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // A stream stopped on purpose ends with CURLE_WRITE_ERROR.
    bool ok = (rc == CURLE_OK || (stream && rc == CURLE_WRITE_ERROR && body.size >= STREAM_BYTES)) && code < 400;
    say("   %s  HTTP %ld  %zu bytes in %.2f s", ok ? "OK" : "FAILED", code, body.size, seconds);
    if (stream && seconds > 0)
        say("  (%.0f kbit/s)", body.size * 8 / 1000.0 / seconds);
    say("\n");
    if (!ok)
        say("   curl %d: %s\n", rc, error[0] ? error : curl_easy_strerror(rc));
    if (body.type[0]) say("   type: %s\n", body.type);
    if (body.name[0]) say("   icy-name: %s\n", body.name);
    if (stream) say("   icy-metaint: %s\n", body.metaint[0] ? body.metaint : "(none)");
    if (!stream && body.text[0]) {
        body.text[SHOW_CHARS] = 0;
        say("   %s%s\n", body.text, body.size > SHOW_CHARS ? " ..." : "");
    }
    return ok;
}

int main(int argc, char** argv) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    g_log = fopen(LOG_PATH, "w");
    say("KaiRadio network test. Results also go to %s\n", LOG_PATH);

    Result rc = socketInitializeDefault();
    say("socketInitializeDefault: 0x%x\n", rc);
    CURLcode crc = curl_global_init(CURL_GLOBAL_DEFAULT);
    say("curl %s, init %d\n", curl_version(), crc);

    if (R_SUCCEEDED(rc) && crc == CURLE_OK) {
        bool https = fetch("Directory over HTTPS", API "/json/stats", false, true);
        if (!https)
            fetch("Directory over HTTPS, without checking the certificate", API "/json/stats", false, false);
        fetch("Directory search", API "/json/stations/search?name=fip&limit=2&hidebroken=true", false, https);
        fetch("MP3 stream over HTTP", "http://icecast.radiofrance.fr/fip-midfi.mp3", true, true);
        fetch("AAC stream over HTTPS", "https://icecast.radiofrance.fr/fip-hifi.aac", true, https);
        curl_global_cleanup();
    }
    socketExit();

    say("\nDone. Press + to exit.\n");
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(NULL);
    }
    if (g_log)
        fclose(g_log);
    consoleExit(NULL);
    return 0;
}
