#include "updatefetch.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "lang.h"
#include "net.h"
#include "updaterelease.h"

#define REPO "kairadio"
#define NRO_NAME "KaiRadio.nro"
#define RELEASE_URL "https://api.github.com/repos/" UPDATE_OWNER "/" REPO "/releases/latest"
#define ANSWER_MAX (512u * 1024)
#define THREAD_STACK (512 * 1024)  // curl runs on it.

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static UpdateStatus g_status;
static char g_nro[UPDATE_PATH_MAX];
static pthread_t g_thread;
static bool g_started;           // The thread is to be joined (main thread only).
static volatile bool g_cancel;   // KaiRadio is closing: the download stops.
static volatile float g_progress;  // The download's, 0 to 1.

static void setState(UpdateState state, const char *tag, const char *error)
{
    pthread_mutex_lock(&g_lock);
    g_status.state = state;
    if (tag)
        snprintf(g_status.tag, sizeof(g_status.tag), "%s", tag);
    snprintf(g_status.error, sizeof(g_status.error), "%s", error ? error : "");
    g_status.serial++;
    pthread_mutex_unlock(&g_lock);
}

void updateFetchInit(const char *nro_path)
{
    snprintf(g_nro, sizeof(g_nro), "%s", nro_path ? nro_path : "");
    pthread_mutex_lock(&g_lock);
    memset(&g_status, 0, sizeof(g_status));
    g_status.place = updatePlaceOf(g_nro);
    pthread_mutex_unlock(&g_lock);
}

// GitHub's latest release, read. False with why.
static bool askRelease(UpdateRelease *release, char *why, size_t why_size)
{
    NetResponse r;
    if (!netGet(RELEASE_URL, ANSWER_MAX, &g_cancel, &r)) {
        snprintf(why, why_size, "%s", r.error[0] ? r.error : T("GitHub couldn't be reached."));
        netResponseFree(&r);
        return false;
    }
    bool ok = updateReadRelease(r.data, REPO, NRO_NAME, release);
    netResponseFree(&r);
    if (!ok)
        snprintf(why, why_size, "%s", T("GitHub's answer about the latest version couldn't be used."));
    return ok;
}

static void *checkThread(void *arg)
{
    (void)arg;
    UpdateRelease release;
    char why[256];
    if (!askRelease(&release, why, sizeof(why)))
        setState(UPDATE_CHECK_FAILED, NULL, why);
    else if (updateIsNewer(release.tag, KAIRADIO_VERSION))
        setState(UPDATE_FOUND, release.tag, NULL);
    else
        setState(UPDATE_CURRENT, NULL, NULL);
    return NULL;
}

static void hex(const uint8_t *bytes, size_t count, char *out)
{
    for (size_t i = 0; i < count; i++)
        snprintf(out + 2 * i, 3, "%02x", bytes[i]);
}

// The SHA-256 of the file at path, in hex. False if it can't be read.
static bool fileSha256(const char *path, char out[UPDATE_HEX_SIZE])
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    Sha256Context ctx;
    sha256ContextCreate(&ctx);
    static uint8_t block[256 * 1024];  // One thread hashes at a time.
    size_t n;
    while ((n = fread(block, 1, sizeof(block), f)) > 0)
        sha256ContextUpdate(&ctx, block, n);
    bool ok = !ferror(f);
    fclose(f);
    uint8_t digest[32];
    sha256ContextGetHash(&ctx, digest);
    hex(digest, sizeof(digest), out);
    return ok;
}

// Downloads release's .nro next to KaiRadio's, checks its size and SHA-256
// against GitHub's, and puts it in its place. False with why (and nothing changed).
static bool install(const UpdateRelease *release, char *why, size_t why_size)
{
    char fresh[UPDATE_PATH_MAX], got[UPDATE_HEX_SIZE];
    NetResponse r;
    if (!updateNewPath(g_nro, fresh, sizeof(fresh))) {
        snprintf(why, why_size, "%s", T("The new version couldn't be written to the card: nothing was changed."));
        return false;
    }
    if (!netDownloadFile(release->url, fresh, (size_t)release->size, &g_cancel, &g_progress, &r)) {
        snprintf(why, why_size, "%s", r.error[0] ? r.error : T("The new version couldn't be downloaded."));
        return false;
    }
    if ((int64_t)r.size != release->size || !fileSha256(fresh, got) || strcmp(got, release->sha256) != 0) {
        remove(fresh);
        snprintf(why, why_size, "%s", T("The download didn't match GitHub's checksum: nothing was changed."));
        return false;
    }
    if (!updateSwap(g_nro, fresh)) {
        remove(fresh);
        snprintf(why, why_size, "%s", T("The new version couldn't be written to the card: nothing was changed."));
        return false;
    }
    return true;
}

static void *installThread(void *arg)
{
    (void)arg;
    UpdateRelease release;
    char why[256];
    // Asked again: the address and checksum come fresh from GitHub, not from a check made hours ago.
    if (!askRelease(&release, why, sizeof(why)))
        setState(UPDATE_INSTALL_FAILED, NULL, why);
    else if (!updateIsNewer(release.tag, KAIRADIO_VERSION))
        setState(UPDATE_CURRENT, NULL, NULL);
    else if (install(&release, why, sizeof(why)))
        setState(UPDATE_INSTALLED, release.tag, NULL);
    else
        setState(UPDATE_INSTALL_FAILED, NULL, g_cancel ? "" : why);
    return NULL;
}

static bool busy(void)
{
    pthread_mutex_lock(&g_lock);
    bool running = g_status.state == UPDATE_CHECKING || g_status.state == UPDATE_DOWNLOADING;
    pthread_mutex_unlock(&g_lock);
    return running;
}

static bool start(void *(*run)(void *), UpdateState state)
{
    if (busy())
        return false;
    if (g_started) {  // The last one ended: join it before the next.
        pthread_join(g_thread, NULL);
        g_started = false;
    }
    setState(state, NULL, NULL);
    g_progress = 0.0f;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, THREAD_STACK);
    g_started = pthread_create(&g_thread, &attr, run, NULL) == 0;
    pthread_attr_destroy(&attr);
    if (!g_started)
        setState(state == UPDATE_CHECKING ? UPDATE_CHECK_FAILED : UPDATE_INSTALL_FAILED, NULL,
                 T("The network can't be used."));
    return g_started;
}

bool updateFetchCheck(void)
{
    return start(checkThread, UPDATE_CHECKING);
}

bool updateFetchInstall(void)
{
    pthread_mutex_lock(&g_lock);
    bool can = g_status.state == UPDATE_FOUND && g_status.place == UPDATE_PLACE_LOOSE;
    pthread_mutex_unlock(&g_lock);
    return can && start(installThread, UPDATE_DOWNLOADING);
}

void updateFetchStatus(UpdateStatus *out)
{
    pthread_mutex_lock(&g_lock);
    *out = g_status;
    pthread_mutex_unlock(&g_lock);
    out->progress = g_progress;
}

void updateFetchExit(void)
{
    g_cancel = true;
    if (g_started) {
        pthread_join(g_thread, NULL);
        g_started = false;
    }
}
