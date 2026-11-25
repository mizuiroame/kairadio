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
#include "buttons.h"
#include "globe.h"
#include "font.h"
#include "lang.h"
#include "gfx.h"
#include "settings.h"
#include "stubs.h"
#include "ui.h"
#include "ui_hit.h"
#include "ui_map.h"

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
    static const char *const CODES[][2] = {{"Norway", "NO"}, {"Mexico", "MX"},          {"Germany", "DE"},
                                           {"Colombia", "CO"}, {"Japan", "JP"},         {"United Kingdom", "GB"},
                                           {"France", "FR"},   {"United States", "US"}};
    for (size_t c = 0; c < sizeof(CODES) / sizeof(CODES[0]); c++)
        if (strcmp(CODES[c][0], s.country) == 0)
            snprintf(s.countrycode, sizeof(s.countrycode), "%s", CODES[c][1]);
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
    SearchMode search_mode;
    bool category_open;
    size_t category_cursor[SEARCH_MODES];
    size_t map_cursor;
    CategorySort category_sort[SEARCH_MODES];
    Overlay overlay;
    int menu_cursor, settings_page, settings_cursor, sleep_minutes, passport_row;
    Config config;
    char toast[224];
    uint64_t notice_length;  // A tap on the notice sends it away sooner.
    PodPage pod_page;
    size_t pod_cursor[POD_PAGES];
    size_t pod_subs;
    long pod_marks;          // The episodes' marks, summed: a tap on "heard" changes it.
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
    s.search_mode = app->search_mode;
    s.category_open = app->category_open;
    memcpy(s.category_cursor, app->category_cursor, sizeof(s.category_cursor));
    s.map_cursor = app->map_cursor;
    memcpy(s.category_sort, app->category_sort, sizeof(s.category_sort));
    s.overlay = app->overlay;
    s.menu_cursor = app->menu_cursor;
    s.settings_page = app->settings_page;
    s.settings_cursor = app->settings_cursor;
    s.sleep_minutes = app->sleep_minutes;
    s.passport_row = app->passport_row;
    s.config = app->config;
    snprintf(s.toast, sizeof(s.toast), "%s", app->notices.count ? app->notices.items[app->notices.count - 1].text : "");
    s.notice_length = 0;  // Any bubble's: a tap on any of them sends it away sooner.
    for (int i = 0; i < app->notices.count; i++)
        s.notice_length += app->notices.items[i].length_ns + (app->notices.items[i].pushed_out ? 1 : 0);
    s.pod_page = app->pod_page;
    memcpy(s.pod_cursor, app->pod_cursor, sizeof(s.pod_cursor));
    s.pod_subs = app->pods.sub_count;
    for (size_t i = 0; i < app->pods.mark_count; i++)
        s.pod_marks += app->pods.marks[i].played * 1000003L + app->pods.marks[i].position + (long)i;
    return s;
}

static void render(const char *dir, const char *name, App *app);
static void expect(bool ok, const char *what);

// --- Podcasts (made-up names) ---

static PodcastResult podcastNamed(const char *title, const char *author, const char *genre, int episodes)
{
    PodcastResult r;
    memset(&r, 0, sizeof(r));
    snprintf(r.title, sizeof(r.title), "%s", title);
    snprintf(r.author, sizeof(r.author), "%s", author);
    snprintf(r.genre, sizeof(r.genre), "%s", genre);
    snprintf(r.feed, sizeof(r.feed), "https://feeds.example/%c%zu", title[0], strlen(title));
    r.episodes = episodes;
    return r;
}

// Episodes for a podcast: newest first, a week apart, from 2026-11-20.
static void giveEpisodes(App *app, int sub, int count)
{
    static const char *const TITLES[] = {
        "The Lighthouse Keepers of the North Atlantic",
        "Why Bread Rises: A Kitchen Science Special",
        "Listener Questions, Part 3",
        "Interview: The Cartographer Who Mapped Imaginary Cities",
        "How Night Trains Came Back",
        "A Short History of the Paperclip",
        "Live From the Harbor Festival",
        "Episode 1: Hello, World",
    };
    Feed f = {0};
    f.episodes = calloc((size_t)count, sizeof(Episode));
    f.count = (size_t)count;
    for (int i = 0; i < count; i++) {
        Episode *e = &f.episodes[i];
        snprintf(e->guid, sizeof(e->guid), "ep-%d", count - i);
        snprintf(e->title, sizeof(e->title), "%s", TITLES[i % 8]);
        snprintf(e->url, sizeof(e->url), "https://cdn.example/%d.mp3", count - i);
        snprintf(e->type, sizeof(e->type), "%s", "audio/mpeg");
        e->published = 1795132800 - (int64_t)i * 7 * 86400;
        e->duration = 1800 + i * 731;
    }
    podStoreTakeFeed(&app->pods, sub, &f, 1795132800);
    podcastFeedFree(&f);
}

static void podcastScenes(const char *dir, App *app)
{
    appSetView(app, VIEW_PODCASTS);
    app->pod_page = POD_PAGE_SUBS;
    render(dir, "podcasts_empty", app);

    // Top: loading, then the region's top podcasts; the genres; a genre's; the regions.
    app->pod_page = POD_PAGE_TOP;
    app->pod_searching = true;
    app->pod_wait = POD_WAIT_TOP;
    snprintf(app->pod_top_region, sizeof(app->pod_top_region), "%s", appPodRegion(app));
    render(dir, "podcasts_top_loading", app);
    app->pod_searching = false;
    app->pod_wait = POD_WAIT_NONE;
    PodcastResult tops[] = {
        podcastNamed("Midnight Case Files", "Lantern Studio", "True Crime", 310),
        podcastNamed("The Morning Brief", "Harbor News", "News", 2200),
        podcastNamed("Laugh Track Diaries", "Two Chairs", "Comedy", 145),
        podcastNamed("Paperclip Histories", "Harbor Lights Audio", "History", 212),
        podcastNamed("Science on a Napkin", "Mizu Studio", "Science", 88),
        podcastNamed("Ninety Minutes", "Valley Radio", "Sports", 512),
        podcastNamed("Bedtime Lighthouses", "Small Hours", "Kids & Family", 64),
        podcastNamed("Frames Per Second", "Cinema Club", "TV & Film", 230),
    };
    app->pod_top.items = calloc(8, sizeof(PodcastResult));
    memcpy(app->pod_top.items, tops, sizeof(tops));
    app->pod_top.count = 8;
    render(dir, "podcasts_top", app);
    app->pod_page = POD_PAGE_GENRES;
    app->pod_cursor[POD_PAGE_GENRES] = 2;
    render(dir, "podcasts_genres", app);
    appHandleButtons(app, HidNpadButton_A, 0);
    expect(app->pod_page == POD_PAGE_RESULTS && app->pod_genre == 2 && app->pod_results_from == POD_PAGE_GENRES,
           "A on a genre opens its top podcasts");
    app->pod_searching = false;
    app->pod_results.items = calloc(3, sizeof(PodcastResult));
    memcpy(app->pod_results.items, tops, 3 * sizeof(PodcastResult));
    app->pod_results.count = 3;
    render(dir, "podcasts_genre", app);
    appHandleButtons(app, HidNpadButton_B, 0);
    expect(app->pod_page == POD_PAGE_GENRES, "B from a genre's podcasts goes back to the genres");
    podcastResultsFree(&app->pod_results);
    appHandleButtons(app, HidNpadButton_Y, 0);
    expect(app->pod_page == POD_PAGE_REGIONS, "Y on the genres opens the regions");
    render(dir, "podcasts_regions", app);
    app->pod_cursor[POD_PAGE_REGIONS] = 2;  // España.
    appHandleButtons(app, HidNpadButton_A, 0);
    expect(app->pod_page == POD_PAGE_GENRES && !strcmp(appPodRegion(app), "es"), "A on a region uses it");
    app->config.podcast_region[0] = '\0';
    appHandleButtons(app, HidNpadButton_Left, 0);
    expect(app->pod_page == POD_PAGE_TOP, "left goes from Genres to Top");
    app->pod_page = POD_PAGE_SUBS;

    // A search: looking, then its answers (one subscribed).
    snprintf(app->pod_query, sizeof(app->pod_query), "%s", "history");
    app->pod_page = POD_PAGE_RESULTS;
    app->pod_searching = true;
    render(dir, "podcasts_searching", app);
    app->pod_searching = false;
    PodcastResult found[] = {
        podcastNamed("Paperclip Histories", "Harbor Lights Audio", "History", 212),
        podcastNamed("The Long Afternoon", "Mizu Studio", "Society & Culture", 48),
        podcastNamed("Maps of Nowhere", "Cartography Club", "Arts", 97),
        podcastNamed("Small Town, Big Weather", "Valley Radio", "News", 1310),
    };
    app->pod_results.items = calloc(4, sizeof(PodcastResult));
    memcpy(app->pod_results.items, found, sizeof(found));
    app->pod_results.count = 4;
    podStoreSubscribe(&app->pods, &found[2], 1795000000);
    app->pod_cursor[POD_PAGE_RESULTS] = 1;
    render(dir, "podcasts_results", app);
    app->pod_search_failed = true;
    snprintf(app->pod_error, sizeof(app->pod_error), "%s", "Could not resolve host: itunes.apple.com");
    podcastResultsFree(&app->pod_results);
    render(dir, "podcasts_search_failed", app);
    app->pod_search_failed = false;

    // Your podcasts: three, one with new episodes.
    podStoreSubscribe(&app->pods, &found[0], 1795000000);
    podStoreSubscribe(&app->pods, &found[3], 1795000000);
    for (int i = 0; i < 3; i++)
        giveEpisodes(app, i, 8);
    app->pods.subs[1].seen = 1796000000;  // Seen after its newest: nothing new.
    app->pods.subs[2].seen = 1796000000;
    app->pod_page = POD_PAGE_SUBS;
    render(dir, "podcasts", app);

    // A podcast's episodes: one heard, one started, one playing.
    PodcastResult open = found[0];
    app->pod_open = open;
    app->pod_page = POD_PAGE_EPISODES;
    app->pod_from = POD_PAGE_SUBS;
    app->pod_cursor[POD_PAGE_EPISODES] = 2;
    podStoreSetPlayed(&app->pods, open.feed, "ep-6", true, 1795200000);
    podStoreSetPosition(&app->pods, open.feed, "ep-5", 900, 4724, 1795200000);
    app->pod_loading = true;
    int index = podStoreFind(&app->pods, open.feed);
    size_t saved = app->pods.subs[index].count;
    app->pods.subs[index].count = 0;
    render(dir, "podcast_loading", app);
    app->pods.subs[index].count = saved;
    app->pod_loading = false;
    const Episode *e = &app->pods.subs[index].episodes[1];
    g_stub_radio = (RadioStatus){0};
    g_stub_radio.has_episode = true;
    g_stub_radio.state = RADIO_PLAYING;
    snprintf(g_stub_radio.episode.feed, sizeof(g_stub_radio.episode.feed), "%s", open.feed);
    snprintf(g_stub_radio.episode.guid, sizeof(g_stub_radio.episode.guid), "%s", e->guid);
    snprintf(g_stub_radio.episode.title, sizeof(g_stub_radio.episode.title), "%s", e->title);
    snprintf(g_stub_radio.episode.podcast, sizeof(g_stub_radio.episode.podcast), "%s", open.title);
    g_stub_radio.position = 1234;
    g_stub_radio.duration = e->duration;
    snprintf(g_stub_radio.codec, sizeof(g_stub_radio.codec), "%s", "MP3");
    g_stub_radio.bitrate = 128;
    g_stub_radio.sample_rate = 44100;
    render(dir, "podcast_episodes", app);

    // The episode in Now Playing: playing, then paused.
    appSetView(app, VIEW_NOW_PLAYING);
    render(dir, "now_playing_episode", app);
    g_stub_radio.paused = true;
    render(dir, "now_playing_episode_paused", app);

    // The flow: A on a podcast opens it, B goes back, A on an episode plays it.
    appSetView(app, VIEW_PODCASTS);
    int paperclip = podStoreFind(&app->pods, open.feed);  // Each subscription went on top: it's not the first.
    app->pod_page = POD_PAGE_SUBS;
    app->pod_cursor[POD_PAGE_SUBS] = (size_t)paperclip;
    appHandleButtons(app, HidNpadButton_A, 0);
    expect(app->pod_page == POD_PAGE_EPISODES && !strcmp(app->pod_open.feed, open.feed),
           "A on a podcast opens its episodes");
    app->pod_cursor[POD_PAGE_EPISODES] = 3;
    appHandleButtons(app, HidNpadButton_A, 0);
    expect(g_stub_radio.has_episode && !strcmp(g_stub_radio.episode.guid, "ep-5") && g_stub_radio.episode.start == 900,
           "A on a started episode plays it from where it was left");
    appHandleButtons(app, HidNpadButton_B, 0);
    expect(app->pod_page == POD_PAGE_SUBS, "B goes back to the podcasts");

    // X marks an episode heard, the playing one too; closing and opening KaiRadio keeps both.
    appHandleButtons(app, HidNpadButton_A, 0);
    app->pod_cursor[POD_PAGE_EPISODES] = 3;  // ep-5: playing (from above).
    appHandleButtons(app, HidNpadButton_X, 0);
    app->pod_cursor[POD_PAGE_EPISODES] = 0;  // ep-8.
    appHandleButtons(app, HidNpadButton_X, 0);
    g_stub_radio.state = RADIO_PLAYING;
    g_stub_radio.position = 1500;
    for (int i = 1; i <= 3; i++)
        appPodcastsUpdate(app, (uint64_t)i * 11000000000ull);  // Its place kept every 10 s while it plays.
    appPodcastsStop(app);
    appPodcastsStart(app);
    int position;
    bool heard8 = false, heard5 = false;
    podStoreMark(&app->pods, open.feed, "ep-8", &position, &heard8);
    podStoreMark(&app->pods, open.feed, "ep-5", &position, &heard5);
    expect(heard8, "an episode marked heard with X is still heard after closing and opening KaiRadio");
    expect(heard5, "the playing episode marked heard with X is still heard after closing and opening KaiRadio");
    g_stub_radio = (RadioStatus){0};
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

// A country's light on the globe, not the one picked: where a tap picks it.
static bool globeLight(const App *app, int *x, int *y)
{
    const CategoryList *countries = &app->categories[SEARCH_COUNTRY];
    for (size_t i = 0; i < countries->count; i++) {
        const MapCountry *c = mapFindCountry(countries->items[i].code);
        float px, py, depth;
        if (i != app->map_cursor && c && globeProject(app->globe_view, globeMapPlace(c->label), &px, &py, &depth) &&
            depth > 0.5f) {
            *x = (int)px;
            *y = (int)py;
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
        if (r->kind == HIT_SEARCH_MODE && r->arg == (int)app->search_mode)
            continue;  // The same for the way of searching.
        int x, y;
        if (r->kind == HIT_MAP && app->config.world_globe && globeLight(app, &x, &y)) {
            // On the globe: a tap on another country's light picks it.
        } else if (!reachablePoint(i, &x, &y)) {
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

static uint32_t g_pixels[GFX_FB_WIDTH * GFX_FB_HEIGHT];

// Draws a frame, which sets the touch targets.
static void draw(const App *app)
{
    gfxSetTarget(g_pixels, GFX_FB_WIDTH);
    uiDraw(app, &g_stub_radio, 1234567890ull);
    gfxSetTarget(NULL, 0);
}

static void render(const char *dir, const char *name, App *app)
{
    char path[512];
    draw(app);
    checkTouch(name, app);
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    if (!writePpm(path, g_pixels))
        fprintf(stderr, "cannot write %s\n", path);
}

// Taps the last frame's target of this kind and arg. False if there is none
// or it's covered.
static bool tapTarget(App *app, HitKind kind, int arg)
{
    for (int i = uiHitCount() - 1; i >= 0; i--) {
        const HitRegion *r = uiHitAt(i);
        int x, y;
        if (r->kind == kind && r->arg == arg && reachablePoint(i, &x, &y))
            return appTap(app, x, y, 0);
    }
    return false;
}

// A step of a flow (buttons or taps) that didn't do what it should.
static void expect(bool ok, const char *what)
{
    if (!ok) {
        printf("FLOW: %s\n", what);
        g_problems++;
    }
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

// KAI_LANG_FILE names a language file: the screens are drawn in it (the
// Makefile draws every language in data/, to see what no longer fits).
static void useLanguageFile(void)
{
    const char *path = getenv("KAI_LANG_FILE");
    if (!path)
        return;
    FILE *f = fopen(path, "rb");
    static char text[1 << 20];
    size_t size = f ? fread(text, 1, sizeof(text), f) : 0;
    if (f)
        fclose(f);
    if (!size || !langLoadText(text, size))
        fprintf(stderr, "cannot read the language in %s\n", path);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    useLanguageFile();
    if (!fontInit()) {
        fprintf(stderr, "fonts failed\n");
        return 1;
    }
    system("rm -rf " CONFIG);
    static App app;
    appInit(&app, CONFIG);
    appPodcastsStart(&app);  // As main.c does: the podcasts kept in CONFIG.
    app.sys = (SysInfo){true, 21, 47, true, 82, true};
    uiMapSetClock(1795543200);  // 2026-11-24 18:00 UTC: night over Asia, dusk over Europe, day over the Americas.

    // World: loading, then the countries lit (about as many stations as the directory has), then one picked.
    appSetView(&app, VIEW_WORLD);
    render(dir, "world_loading", &app);
    // Leaving the world while it lights up, and back: its countries are asked for again.
    appSetView(&app, VIEW_POPULAR);
    g_stub_directory.state = DIR_READY;  // Popular's answer came.
    appUpdate(&app, 0);
    appSetView(&app, VIEW_WORLD);
    appUpdate(&app, 0);
    expect(app.waiting_active && app.waiting_target == TARGET_CATEGORIES && app.waiting_mode == SEARCH_COUNTRY,
           "back on World after leaving it while it lit up, its countries are asked for again");
    app.popular_asked = false;  // As before, for Popular's shots below.
    static const struct {
        const char *name, *code;
        int count;
    } WORLD[] = {
        {"The United States Of America", "US", 7100}, {"Germany", "DE", 3300}, {"France", "FR", 1600},
        {"The United Kingdom Of Great Britain And Northern Ireland", "GB", 1500}, {"Brazil", "BR", 1300},
        {"Mexico", "MX", 1100}, {"Italy", "IT", 1000}, {"Spain", "ES", 900}, {"Canada", "CA", 900},
        {"The Netherlands", "NL", 800}, {"Greece", "GR", 700}, {"Poland", "PL", 600}, {"Argentina", "AR", 600},
        {"Australia", "AU", 600}, {"India", "IN", 500}, {"Turkey", "TR", 500}, {"Colombia", "CO", 400},
        {"Indonesia", "ID", 400}, {"Ukraine", "UA", 400}, {"Chile", "CL", 350}, {"Philippines", "PH", 300},
        {"Sweden", "SE", 300}, {"Portugal", "PT", 300}, {"Peru", "PE", 250}, {"Japan", "JP", 200},
        {"South Africa", "ZA", 200}, {"Thailand", "TH", 200}, {"Venezuela", "VE", 200}, {"New Zealand", "NZ", 150},
        {"Norway", "NO", 150}, {"Malaysia", "MY", 150}, {"Republic Of Korea", "KR", 150}, {"Morocco", "MA", 100},
        {"Vietnam", "VN", 100}, {"Nigeria", "NG", 80}, {"Kenya", "KE", 60}, {"Egypt", "EG", 50},
        {"Saudi Arabia", "SA", 40}, {"Singapore", "SG", 30}, {"Holy See", "VA", 2},
    };
    size_t world_count = sizeof(WORLD) / sizeof(WORLD[0]);
    CategoryList *world = &app.categories[SEARCH_COUNTRY];
    world->items = calloc(world_count, sizeof(Category));
    for (size_t i = 0; i < world_count; i++) {
        snprintf(world->items[i].name, sizeof(world->items[i].name), "%s", WORLD[i].name);
        snprintf(world->items[i].code, sizeof(world->items[i].code), "%s", WORLD[i].code);
        world->items[i].count = WORLD[i].count;
    }
    world->count = world_count;
    app.waiting_active = false;
    render(dir, "world", &app);
    app.map_cursor = 5;  // Mexico.
    render(dir, "world_picked", &app);
    // Zoomed in on Europe: the globe at its closest, facing Germany, and the flat map 3x.
    app.map_cursor = 1;
    GlobeView home = app.globe_view;
    app.globe_view = (GlobeView){10.0f, 50.0f, GLOBE_ZOOM_MAX};
    render(dir, "world_globe_zoom", &app);
    app.globe_view = (GlobeView){-20.0f, 20.0f, 1.6f};  // Halfway in: the globe past the header and the bar.
    render(dir, "world_globe_zoom_mid", &app);
    app.globe_view = home;
    app.config.world_globe = false;
    render(dir, "world_flat", &app);
    mapZoomBy(&app.map_zoom, 3.0f, MAP_X + MAP_W * 0.53f, MAP_Y + MAP_H * 0.28f);
    render(dir, "world_flat_zoom", &app);
    mapZoomReset(&app.map_zoom);
    app.config.world_globe = true;

    // The passport: the left stick's press opens it, empty at first; B closes it.
    appHandleButtons(&app, HidNpadButton_StickL, 0);
    expect(app.overlay == OVERLAY_PASSPORT, "the left stick's press on World opens the passport");
    render(dir, "passport_empty", &app);
    appHandleButtons(&app, HidNpadButton_B, 0);
    expect(app.overlay == OVERLAY_NONE, "B closes the passport");
    // With stamps: more countries than show at once, a row further down with the stick.
    static const char *STAMPS[] = {"DE", "US", "JP", "MX", "FR", "BR", "GB", "IT", "ES", "CA",
                                   "NL", "GR", "PL", "AR", "AU", "IN", "TR", "CO", "KR", "ZZ"};
    for (int i = 0; i < (int)(sizeof(STAMPS) / sizeof(STAMPS[0])); i++)
        for (int j = 0; j <= i % 3; j++) {
            Station s;
            memset(&s, 0, sizeof(s));
            snprintf(s.countrycode, sizeof(s.countrycode), "%s", STAMPS[i]);
            snprintf(s.uuid, sizeof(s.uuid), "stamp-%d-%d", i, j);
            passportStamp(&app.passport, &s, 1793000000 + (int64_t)i * 3 * 86400);
        }
    render(dir, "world_passport", &app);  // Its button counts them.
    expect(tapTarget(&app, HIT_PASSPORT, 0) && app.overlay == OVERLAY_PASSPORT, "a tap on Passport opens it");
    render(dir, "passport", &app);
    appHandleButtons(&app, HidNpadButton_Down, 0);
    expect(app.passport_row == 1, "down shows the passport's next row of stamps");
    render(dir, "passport_more", &app);
    appHandleButtons(&app, HidNpadButton_Down, 0);
    expect(app.passport_row == 1, "the passport scrolls no further than its last row");
    app.overlay = OVERLAY_NONE;
    categoryListFree(world);
    app.categories_asked[SEARCH_COUNTRY] = false;
    app.map_cursor = 0;

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
    fillList(&app.name_results, 3);
    render(dir, "search_results", &app);

    // Search by genre: loading, the list, then one open.
    appSetSearchMode(&app, SEARCH_GENRE);
    render(dir, "search_genres_loading", &app);
    static const char *GENRES[] = {"pop", "rock", "news", "jazz", "classical", "hip hop", "electronic", "80s", "talk"};
    CategoryList *genres = &app.categories[SEARCH_GENRE];
    genres->items = calloc(9, sizeof(Category));
    for (int i = 0; i < 9; i++) {
        snprintf(genres->items[i].name, sizeof(genres->items[i].name), "%s", GENRES[i]);
        genres->items[i].count = 5000 / (i + 1);
    }
    genres->count = 9;
    g_stub_directory.state = DIR_READY;
    app.waiting_active = false;
    app.category_cursor[SEARCH_GENRE] = 3;
    render(dir, "search_genres", &app);
    appCycleSort(&app, 0);
    appCycleSort(&app, 0);  // A to Z, the jazz row still selected.
    render(dir, "search_genres_az", &app);
    appCycleSort(&app, 0);
    appCycleSort(&app, 0);  // Back to most stations first, for the shots below.
    app.category_open = true;
    app.open_category = genres->items[3];
    fillList(&app.category_results, 6);
    render(dir, "search_genre_open", &app);

    // Countries, with their codes.
    appSetSearchMode(&app, SEARCH_COUNTRY);
    CategoryList *countries = &app.categories[SEARCH_COUNTRY];
    static const char *COUNTRIES[][2] = {{"Germany", "DE"}, {"Mexico", "MX"}, {"Japan", "JP"}, {"Brazil", "BR"}};
    countries->items = calloc(4, sizeof(Category));
    for (int i = 0; i < 4; i++) {
        snprintf(countries->items[i].name, sizeof(countries->items[i].name), "%s", COUNTRIES[i][0]);
        snprintf(countries->items[i].code, sizeof(countries->items[i].code), "%s", COUNTRIES[i][1]);
        countries->items[i].count = 900 - i * 100;
    }
    countries->count = 4;
    app.waiting_active = false;
    render(dir, "search_countries", &app);

    // Languages, with letters in their own script (or their code, where no font has them).
    appSetSearchMode(&app, SEARCH_LANGUAGE);
    CategoryList *languages = &app.categories[SEARCH_LANGUAGE];
    static const char *LANGUAGES[][2] = {{"english", "en"}, {"spanish", "es"}, {"german", "de"},
                                         {"russian", "ru"}, {"greek", "el"},   {"japanese", "ja"},
                                         {"arabic", "ar"}};
    languages->items = calloc(7, sizeof(Category));
    for (int i = 0; i < 7; i++) {
        snprintf(languages->items[i].name, sizeof(languages->items[i].name), "%s", LANGUAGES[i][0]);
        snprintf(languages->items[i].code, sizeof(languages->items[i].code), "%s", LANGUAGES[i][1]);
        languages->items[i].count = 9000 / (i + 1);
    }
    languages->count = 7;
    app.waiting_active = false;
    render(dir, "search_languages", &app);
    appSetSearchMode(&app, SEARCH_NAME);

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
    snprintf(g_stub_radio.error, sizeof(g_stub_radio.error), "%s", "This station's stream is encrypted.");
    render(dir, "now_playing_failed", &app);
    g_stub_radio = (RadioStatus){0};
    render(dir, "now_playing_empty", &app);
    podcastScenes(dir, &app);

    // The + menu and its exit question, Settings' pages (pink, with the sleep
    // timer on) and About, over Popular; and the way through them.
    appSetView(&app, VIEW_POPULAR);
    app.notices = (Notices){0};  // The sort's message from earlier: not part of these shots.
    app.notice_count = 0;
    appOpenMenu(&app);
    render(dir, "menu", &app);
    appHandleOverlay(&app, HidNpadButton_Plus, 0);
    expect(app.overlay == OVERLAY_EXIT && app.menu_cursor == EXIT_CONFIRM && !app.quit, "+ in the menu asks first");
    render(dir, "menu_exit", &app);
    appHandleOverlay(&app, HidNpadButton_B, 0);
    expect(app.overlay == OVERLAY_NONE && !app.quit, "B cancels the exit question");
    appOpenMenu(&app);
    draw(&app);
    expect(tapTarget(&app, HIT_MENU_ITEM, MENU_SETTINGS) && app.overlay == OVERLAY_SETTINGS &&
               app.settings_page == SETTINGS_PAGE_MAIN && app.settings_cursor == SETTING_OPEN_PLAYBACK,
           "a tap on Settings opens its main page");
    app.config.accent = 1;
    app.sleep_minutes = 45;
    app.sleep_until_ns = 1234567890ull + 45ull * 60000000000ull;
    render(dir, "settings", &app);
    expect(tapTarget(&app, HIT_SETTING, SETTING_OPEN_PLAYBACK) && app.settings_page == SETTINGS_PAGE_PLAYBACK &&
               app.settings_cursor == SETTING_SLEEP,
           "a tap on Playback opens its page");
    render(dir, "settings_playback", &app);
    expect(tapTarget(&app, HIT_SETTINGS_BACK, 0) && app.settings_page == SETTINGS_PAGE_MAIN &&
               app.settings_cursor == SETTING_OPEN_PLAYBACK,
           "Back goes to the main page, on Playback");
    draw(&app);
    expect(tapTarget(&app, HIT_SETTING, SETTING_OPEN_DISPLAY) && app.settings_page == SETTINGS_PAGE_DISPLAY &&
               app.settings_cursor == SETTING_LANGUAGE,
           "a tap on Display opens its page");
    app.settings_cursor = SETTING_BACKGROUND;  // A long help, on two lines.
    render(dir, "settings_display", &app);
    appHandleOverlay(&app, HidNpadButton_B, 0);
    expect(app.overlay == OVERLAY_SETTINGS && app.settings_page == SETTINGS_PAGE_MAIN &&
               app.settings_cursor == SETTING_OPEN_DISPLAY,
           "B goes back a page, on Display");
    appHandleOverlay(&app, HidNpadButton_Right, 0);
    expect(app.settings_page == SETTINGS_PAGE_DISPLAY, "right opens a page");
    appHandleOverlay(&app, HidNpadButton_B, 0);
    appHandleOverlay(&app, HidNpadButton_Down, 0);
    appHandleOverlay(&app, HidNpadButton_A, 0);
    expect(app.overlay == OVERLAY_SETTINGS && app.settings_page == SETTINGS_PAGE_ABOUT &&
               app.settings_cursor == SETTING_CREDITS,
           "A on About opens its page");
    app.settings_cursor = SETTING_UPDATE_NOW;
    render(dir, "settings_about", &app);
    app.settings_cursor = SETTING_CREDITS;
    appHandleOverlay(&app, HidNpadButton_A, 0);
    expect(app.overlay == OVERLAY_ABOUT, "A on Credits and licenses opens the panel");
    render(dir, "about", &app);
    appHandleOverlay(&app, HidNpadButton_B, 0);
    expect(app.overlay == OVERLAY_SETTINGS && app.settings_cursor == SETTING_CREDITS, "B goes back from the panel");
    appHandleOverlay(&app, HidNpadButton_B, 0);
    expect(app.settings_page == SETTINGS_PAGE_MAIN && app.settings_cursor == SETTING_ABOUT,
           "B goes back to the main page, on About");
    appHandleOverlay(&app, HidNpadButton_B, 0);
    expect(app.overlay == OVERLAY_NONE, "B on the main page closes Settings");
    app.config.accent = 0;
    app.sleep_minutes = 0;
    app.sleep_until_ns = 0;

    // Notices: bubbles at the top, over the view (and over Settings). Each
    // comes at 1 ns and is fully in once NOTICE_SLIDE_NS has gone by.
    const uint64_t in = 1 + NOTICE_SLIDE_NS;
    noticePush(&app.notices, NOTICE_INFO, T("KaiRadio 1.3.0 is out: Settings > About installs it."), 1);
    app.notice_count = noticeVisible(&app.notices, in, app.notice_views);
    render(dir, "notice_info", &app);
    expect(tapTarget(&app, HIT_NOTICE, (int)app.notices.items[0].id), "a tap reaches the notice");
    app.notices = (Notices){0};
    noticePush(&app.notices, NOTICE_ERROR, T("The download didn't match GitHub's checksum: nothing was changed."), 1);
    noticePush(&app.notices, NOTICE_INFO, T("KaiRadio 1.3.0 is out: Settings > About installs it."), 2);
    app.notice_count = noticeVisible(&app.notices, in + NOTICE_MOVE_NS, app.notice_views);
    render(dir, "notice_error", &app);  // Two, stacked: the newest on top.
    app.notices = (Notices){0};
    noticePush(&app.notices, NOTICE_DONE, T("KaiRadio is up to date."), 1);
    app.notice_count = noticeVisible(&app.notices, in, app.notice_views);
    appOpenSettings(&app);
    render(dir, "notice_done_settings", &app);
    app.overlay = OVERLAY_NONE;
    app.notices = (Notices){0};
    app.notice_count = 0;

    appFree(&app);
    fontExit();
    printf("%d touch problem%s\n", g_problems, g_problems == 1 ? "" : "s");
    return g_problems ? 1 : 0;
}
