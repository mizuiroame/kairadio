// Renders KaiRadio's screens on the host, with made-up stations, and checks
// every touch target: it must be reachable (not covered by another one) and
// a tap on it must do something. Problems make the program fail.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "app.h"
#include "font.h"
#include "gfx.h"
#include "stubs.h"
#include "ui.h"
#include "ui_hit.h"

#define CONFIG "/tmp/kairadio_preview"

static int g_problems;

static const struct {
    const char *name, *country, *tags, *codec, *favicon;
    int bitrate;
    bool hls;
} STATIONS[] = {
    {"Aurora Jazz FM", "Norway", "jazz,smooth jazz,lounge", "AAC", "https://aurora.example/logo.png", 128, false},
    {"Caf\xC3\xA9 del R\xC3\xADo", "Mexico", "latin,bolero", "MP3", "", 192, false},
    {"Nordlicht Radio", "Germany", "pop,80s,90s", "MP3", "https://nordlicht.example/logo.png", 128, false},
    {"Ondas Tropicales 24/7 \xE2\x80\x94 Salsa, Cumbia y Merengue sin parar", "Colombia", "salsa,cumbia", "AAC+", "",
     64, false},
    {"Lo-Fi Harbor", "Japan", "lofi,chillhop,study", "OGG", "https://harbor.example/logo.png", 96, false},
    {"Classic Pulse", "United Kingdom", "classical,baroque", "AAC", "", 256, true},
    {"Radio M\xC3\xA9ridien", "France", "news,talk", "MP3", "", 128, false},
    {"Desert Highway", "United States", "rock,classic rock", "MP3", "https://desert.example/logo.png", 128, false},
    {"Sakura Waves", "Japan", "j-pop,anime", "AAC", "", 128, false},
};
#define STATION_COUNT (sizeof(STATIONS) / sizeof(STATIONS[0]))

static Station makeStation(size_t i)
{
    Station s;
    memset(&s, 0, sizeof(s));
    snprintf(s.uuid, sizeof(s.uuid), "uuid-%zu", i);
    snprintf(s.name, sizeof(s.name), "%s", STATIONS[i].name);
    snprintf(s.url, sizeof(s.url), "https://stream%zu.example/live", i);
    snprintf(s.favicon, sizeof(s.favicon), "%s", STATIONS[i].favicon);
    snprintf(s.country, sizeof(s.country), "%s", STATIONS[i].country);
    snprintf(s.tags, sizeof(s.tags), "%s", STATIONS[i].tags);
    snprintf(s.codec, sizeof(s.codec), "%s", STATIONS[i].codec);
    s.bitrate = STATIONS[i].bitrate;
    s.hls = STATIONS[i].hls;
    return s;
}

static void fillList(StationList *list, size_t count)
{
    stationListFree(list);
    for (size_t i = 0; i < count; i++) {
        Station s = makeStation(i % STATION_COUNT);
        snprintf(s.uuid, sizeof(s.uuid), "uuid-%zu", i);
        stationListAppend(list, &s);
    }
}

static bool writePpm(const char *path, const uint32_t *pixels)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    fprintf(f, "P6\n%d %d\n255\n", GFX_FB_WIDTH, GFX_FB_HEIGHT);
    for (int i = 0; i < GFX_FB_WIDTH * GFX_FB_HEIGHT; i++) {
        uint32_t p = pixels[i];
        unsigned char rgb[3] = {p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF};
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f) == 0;
}

// What a tap could change, to tell whether it did anything.
typedef struct {
    View view, list_view;
    size_t cursor[VIEW_COUNT];
    bool quit;
    int calls;
    size_t favorites;
    char query[SEARCH_MAX];
} Snapshot;

static Snapshot snapshot(const App *app)
{
    Snapshot s;
    memset(&s, 0, sizeof(s));
    s.view = app->view;
    s.list_view = app->list_view;
    memcpy(s.cursor, app->cursor, sizeof(s.cursor));
    s.quit = app->quit;
    s.calls = g_stub_calls;
    s.favorites = app->library.favorites.count;
    snprintf(s.query, sizeof(s.query), "%s", app->query);
    return s;
}

// A point of the region that a tap reaches (no later region over it), or false.
static bool reachablePoint(int index, int *px, int *py)
{
    const HitRegion *r = uiHitAt(index);
    for (int gy = 1; gy < 8; gy++)
        for (int gx = 1; gx < 8; gx++) {
            int x = r->x + r->w * gx / 8, y = r->y + r->h * gy / 8;
            if (uiHitFind(x, y) == r) {
                *px = x;
                *py = y;
                return true;
            }
        }
    return false;
}

// Taps each target in a child process (so the app state stays as it is) and
// checks that the tap changed something.
static void checkTouch(const char *name, App *app)
{
    for (int i = 0; i < uiHitCount(); i++) {
        const HitRegion *r = uiHitAt(i);
        if (r->kind == HIT_LIST)
            continue;  // For drags only.
        if (r->kind == HIT_TAB && r->arg == (int)app->view)
            continue;  // The tab shown: a tap keeps it.
        int x, y;
        if (!reachablePoint(i, &x, &y)) {
            printf("TOUCH: %s: target kind %d at %d,%d is covered by others\n", name, r->kind, r->x, r->y);
            g_problems++;
            continue;
        }
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            Snapshot before = snapshot(app);
            bool hit = appTap(app, x, y, 0);
            Snapshot after = snapshot(app);
            _exit(hit && memcmp(&before, &after, sizeof(before)) != 0 ? 0 : 1);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            printf("TOUCH: %s: a tap on kind %d at %d,%d does nothing\n", name, r->kind, x, y);
            g_problems++;
        }
    }
}

static void render(const char *dir, const char *name, App *app)
{
    static uint32_t pixels[GFX_FB_WIDTH * GFX_FB_HEIGHT];
    char path[512];
    gfxSetTarget(pixels, GFX_FB_WIDTH);
    uiDraw(app, &g_stub_radio, 1234567890ull);
    gfxSetTarget(NULL, 0);
    checkTouch(name, app);
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    if (!writePpm(path, pixels))
        fprintf(stderr, "cannot write %s\n", path);
}

static void playing(App *app, size_t index, RadioState state, const char *title)
{
    g_stub_radio = (RadioStatus){0};
    g_stub_radio.has_station = true;
    g_stub_radio.station = app->popular.items[index];
    g_stub_radio.state = state;
    snprintf(g_stub_radio.title, sizeof(g_stub_radio.title), "%s", title);
    snprintf(g_stub_radio.codec, sizeof(g_stub_radio.codec), "%s", "HE-AAC");
    g_stub_radio.bitrate = 64;
    g_stub_radio.sample_rate = 44100;
    stationListFree(&app->play_list);
    fillList(&app->play_list, STATION_COUNT);
    app->play_index = index;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    if (!fontInit()) {
        fprintf(stderr, "fonts failed\n");
        return 1;
    }
    system("rm -rf " CONFIG);
    static App app;
    appInit(&app, CONFIG);
    app.sys = (SysInfo){true, 21, 47, true, 82, false};

    // Popular, loading and failed.
    appSetView(&app, VIEW_POPULAR);
    render(dir, "popular_loading", &app);
    g_stub_directory.state = DIR_FAILED;
    snprintf(g_stub_directory.error, sizeof(g_stub_directory.error), "Could not resolve host: de1.api.radio-browser.info");
    appUpdate(&app, 0);
    render(dir, "popular_failed", &app);

    // Popular with stations, one playing.
    fillList(&app.popular, 40);
    app.cursor[VIEW_POPULAR] = 2;
    libraryToggleFavorite(&app.library, &app.popular.items[0]);
    libraryToggleFavorite(&app.library, &app.popular.items[4]);
    render(dir, "popular", &app);
    playing(&app, 3, RADIO_PLAYING, "Celia Cruz \xE2\x80\x94 La Vida Es Un Carnaval");
    app.cursor[VIEW_POPULAR] = 12;
    render(dir, "popular_playing", &app);

    // Search: empty, then results.
    appSetView(&app, VIEW_SEARCH);
    render(dir, "search_empty", &app);
    snprintf(app.query, sizeof(app.query), "%s", "jazz");
    fillList(&app.results, 3);
    render(dir, "search_results", &app);

    // Favorites and Recent.
    appSetView(&app, VIEW_FAVORITES);
    render(dir, "favorites", &app);
    libraryPlayed(&app.library, &app.popular.items[3]);
    libraryPlayed(&app.library, &app.popular.items[7]);
    appSetView(&app, VIEW_RECENT);
    render(dir, "recent", &app);

    // Now Playing: playing with a song title, with a logo, and failed.
    appSetView(&app, VIEW_NOW_PLAYING);
    render(dir, "now_playing", &app);
    playing(&app, 4, RADIO_PLAYING, "Midnight Study Session (Rainy Window Mix) \xE2\x80\x94 Harbor Lights Collective");
    render(dir, "now_playing_logo", &app);
    playing(&app, 1, RADIO_BUFFERING, "");
    render(dir, "now_playing_buffering", &app);
    playing(&app, 6, RADIO_FAILED, "");
    snprintf(g_stub_radio.error, sizeof(g_stub_radio.error), "%s", "This station's HLS format (fMP4) isn't supported yet.");
    render(dir, "now_playing_failed", &app);
    g_stub_radio = (RadioStatus){0};
    render(dir, "now_playing_empty", &app);

    appFree(&app);
    fontExit();
    printf("%d touch problem%s\n", g_problems, g_problems == 1 ? "" : "s");
    return g_problems ? 1 : 0;
}
