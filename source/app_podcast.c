// The Podcasts tab: the podcasts subscribed to, a search, a podcast's
// episodes; and the episode playing: where it was left, jumps and pause.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "buttons.h"
#include "keyboard_system.h"
#include "lang.h"
#include "podfetch.h"
#include "ui.h"
#include "ui_hit.h"

#define SAVE_EVERY_NS 10000000000ull  // The playing episode's place is kept this often.
#define FRESH_FEED_S 600              // A feed read this recently isn't read again on opening.

static int64_t nowSeconds(void)
{
    return (int64_t)time(NULL);
}

static PodcastResult resultOf(const Subscription *sub)
{
    PodcastResult r;
    memset(&r, 0, sizeof(r));
    snprintf(r.title, sizeof(r.title), "%s", sub->title);
    snprintf(r.author, sizeof(r.author), "%s", sub->author);
    snprintf(r.feed, sizeof(r.feed), "%s", sub->feed);
    snprintf(r.image, sizeof(r.image), "%s", sub->image);
    return r;
}

static void saveSubs(App *app, uint64_t now_ns)
{
    if (!podStoreSaveSubs(&app->pods))
        appNotify(app, NOTICE_ERROR, T("It couldn't be written to the card."), now_ns);
}

// --- The log: what was kept and read, to find out where a mark goes ---

static FILE *g_log;

static void podLog(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void podLog(const char *format, ...)
{
    if (!g_log)
        return;
    time_t t = time(NULL);
    struct tm tm;
    gmtime_r(&t, &tm);
    fprintf(g_log, "%02d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);
    va_list args;
    va_start(args, format);
    vfprintf(g_log, format, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
}

static int heardCount(const PodStore *s)
{
    int n = 0;
    for (size_t i = 0; i < s->mark_count; i++)
        n += s->marks[i].played;
    return n;
}

static void saveMarks(App *app, uint64_t now_ns)
{
    bool ok = podStoreSaveMarks(&app->pods);
    podLog("marks saved: %s (%zu, %d heard)", ok ? "ok" : "FAILED", app->pods.mark_count, heardCount(&app->pods));
    if (!ok)
        appNotify(app, NOTICE_ERROR, T("It couldn't be written to the card."), now_ns);
}

// --- Genres and regions ---

// Apple's podcast genres; the words pick an icon of the radio genres'.
const PodGenre POD_GENRES[] = {
    {1489, N_("News"), "news"},           {1303, N_("Comedy"), "comedy"},
    {1488, N_("True Crime"), "talk"},     {1324, N_("Society & Culture"), "talk"},
    {1487, N_("History"), "world"},       {1545, N_("Sports"), "sport"},
    {1309, N_("TV & Film"), "film"},      {1310, N_("Music"), "music"},
    {1533, N_("Science"), "talk"},        {1318, N_("Technology"), "electro"},
    {1321, N_("Business"), "news"},       {1512, N_("Health & Fitness"), "relax"},
    {1304, N_("Education"), "talk"},      {1305, N_("Kids & Family"), "kids"},
    {1483, N_("Fiction"), "theater"},     {1301, N_("Arts"), "theater"},
    {1314, N_("Religion & Spirituality"), "religio"}, {1502, N_("Leisure"), "chill"},
};
const size_t POD_GENRE_COUNT = sizeof(POD_GENRES) / sizeof(POD_GENRES[0]);

// Apple's stores with podcasts, by their own names (no translation needed).
const PodRegion POD_REGIONS[] = {
    {"mx", "M\xC3\xA9xico"}, {"us", "United States"}, {"es", "Espa\xC3\xB1" "a"}, {"ar", "Argentina"},
    {"co", "Colombia"}, {"cl", "Chile"}, {"pe", "Per\xC3\xBA"}, {"ve", "Venezuela"}, {"ec", "Ecuador"},
    {"gt", "Guatemala"}, {"cr", "Costa Rica"}, {"do", "Rep\xC3\xBA" "blica Dominicana"}, {"uy", "Uruguay"},
    {"br", "Brasil"}, {"pt", "Portugal"}, {"gb", "United Kingdom"}, {"ie", "Ireland"}, {"ca", "Canada"},
    {"au", "Australia"}, {"nz", "New Zealand"}, {"in", "India"}, {"za", "South Africa"}, {"ph", "Philippines"},
    {"de", "Deutschland"}, {"at", "\xC3\x96sterreich"}, {"ch", "Schweiz"}, {"fr", "France"}, {"be", "Belgique"},
    {"it", "Italia"}, {"nl", "Nederland"}, {"se", "Sverige"}, {"no", "Norge"}, {"dk", "Danmark"},
    {"fi", "Suomi"}, {"pl", "Polska"}, {"jp", "\xE6\x97\xA5\xE6\x9C\xAC"}, {"kr", "\xEB\x8C\x80\xED\x95\x9C\xEB\xAF\xBC\xEA\xB5\xAD"},
    {"tw", "\xE5\x8F\xB0\xE7\x81\xA3"}, {"hk", "\xE9\xA6\x99\xE6\xB8\xAF"},
};
const size_t POD_REGION_COUNT = sizeof(POD_REGIONS) / sizeof(POD_REGIONS[0]);

const char *appPodRegionName(const char *code)
{
    for (size_t i = 0; i < POD_REGION_COUNT; i++)
        if (!strcmp(POD_REGIONS[i].code, code))
            return POD_REGIONS[i].name;
    return code;
}

const char *appPodRegion(const App *app)
{
    if (app->config.podcast_region[0])
        return app->config.podcast_region;
    static const char *const BY_LANGUAGE[][2] = {{"es", "mx"}, {"de", "de"}, {"fr", "fr"}, {"it", "it"},
                                                 {"ja", "jp"}, {"ko", "kr"}, {"pt", "br"}, {"zh", "tw"}};
    for (size_t i = 0; i < sizeof(BY_LANGUAGE) / sizeof(BY_LANGUAGE[0]); i++)
        if (!strncmp(app->config.language, BY_LANGUAGE[i][0], 2))
            return BY_LANGUAGE[i][1];
    return "us";
}

bool appPodTopLoading(const App *app)
{
    return app->pod_searching && app->pod_wait == POD_WAIT_TOP;
}

// The region's top podcasts, unless they're here or on their way.
static void askTop(App *app)
{
    const char *region = appPodRegion(app);
    bool here = app->pod_top.count > 0 && !strcmp(app->pod_top_region, region);
    if (here || appPodTopLoading(app))
        return;
    podcastResultsFree(&app->pod_top);
    snprintf(app->pod_top_region, sizeof(app->pod_top_region), "%s", region);
    app->pod_top_failed = false;
    podFetchChart(region, 0);
    PodSearchStatus st;
    podFetchSearchStatus(&st);
    app->pod_search = st.generation;
    app->pod_searching = true;
    app->pod_wait = POD_WAIT_TOP;
}

// --- Starting and stopping ---

void appPodcastsStart(App *app)
{
    char path[CONFIG_FOLDER_MAX + 32];
    snprintf(path, sizeof(path), "%s/podcast.log", app->config_folder);
    if (!g_log)
        g_log = fopen(path, "w");
    podStoreLoad(&app->pods, app->config_folder);
    podLog("start: %zu podcasts, %zu marks (%d heard) from %s", app->pods.sub_count, app->pods.mark_count,
           heardCount(&app->pods), app->pods.folder);
    app->pod_page = app->pods.sub_count ? POD_PAGE_SUBS : POD_PAGE_TOP;  // Nothing kept: something to pick.
    if (!podFetchInit())
        return;
    for (size_t i = 0; i < app->pods.sub_count; i++)
        podFetchFeed(app->pods.subs[i].feed);  // New episodes, looked for on each start.
}

// Keeps where the episode playing is. Returns false when there's none.
static bool keepPlace(App *app)
{
    RadioStatus s;
    radioGetStatus(&s);
    if (!s.has_episode || s.ended || s.position < 1)
        return false;
    podStoreSetPosition(&app->pods, s.episode.feed, s.episode.guid, (int)s.position, (int)s.duration, nowSeconds());
    return true;
}

void appPodcastsStop(App *app)
{
    bool kept = keepPlace(app);
    bool ok = !kept || podStoreSaveMarks(&app->pods);
    podLog("stop: %zu marks (%d heard)%s", app->pods.mark_count, heardCount(&app->pods),
           kept ? ok ? ", saved" : ", NOT saved" : "");
    podFetchExit();
    podStoreFree(&app->pods);
    podcastResultsFree(&app->pod_results);
    podcastResultsFree(&app->pod_top);
    podcastFeedFree(&app->pod_feed);
    if (g_log)
        fclose(g_log);
    g_log = NULL;
}

// --- Lists ---

int appPodOpenSub(const App *app)
{
    return app->pod_open.feed[0] ? podStoreFind(&app->pods, app->pod_open.feed) : -1;
}

const Episode *appPodEpisodes(const App *app, size_t *count)
{
    int sub = appPodOpenSub(app);
    if (sub >= 0) {
        *count = app->pods.subs[sub].count;
        return app->pods.subs[sub].episodes;
    }
    *count = app->pod_feed.count;
    return app->pod_feed.episodes;
}

size_t appPodCount(const App *app)
{
    size_t count;
    switch (app->pod_page) {
    case POD_PAGE_TOP: return app->pod_top.count;
    case POD_PAGE_GENRES: return POD_GENRE_COUNT;
    case POD_PAGE_RESULTS: return app->pod_results.count;
    case POD_PAGE_EPISODES: appPodEpisodes(app, &count); return count;
    case POD_PAGE_REGIONS: return POD_REGION_COUNT;
    default: return app->pods.sub_count;
    }
}

// The podcasts the page lists (Top or a search's), or NULL.
static PodcastResults *pageResults(App *app)
{
    return app->pod_page == POD_PAGE_TOP ? &app->pod_top : app->pod_page == POD_PAGE_RESULTS ? &app->pod_results : NULL;
}

void appPodMove(App *app, long delta)
{
    size_t count = appPodCount(app), *cursor = &app->pod_cursor[app->pod_page];
    if (count == 0)
        return;
    long next = (long)*cursor + delta;
    next = next < 0 ? 0 : next >= (long)count ? (long)count - 1 : next;
    *cursor = (size_t)next;
}

// --- Searches and feeds that came ---

static void takeSearch(App *app)
{
    if (!app->pod_searching)
        return;
    PodSearchStatus st;
    podFetchSearchStatus(&st);
    if (st.generation != app->pod_search)
        return;
    bool top = app->pod_wait == POD_WAIT_TOP;
    if (st.state == POD_SEARCH_READY) {
        PodcastResults results;
        if (podFetchTakeSearch(st.generation, &results)) {
            PodcastResults *target = top ? &app->pod_top : &app->pod_results;
            podcastResultsFree(target);
            *target = results;
        }
        app->pod_searching = false;
    } else if (st.state == POD_SEARCH_FAILED) {
        app->pod_searching = false;
        if (top)
            app->pod_top_failed = true;
        else
            app->pod_search_failed = true;
        snprintf(app->pod_error, sizeof(app->pod_error), "%s", st.error);
    }
}

// The open podcast's name and picture, when the search didn't have them.
static void fillOpen(App *app, const Feed *feed)
{
    if (!app->pod_open.title[0])
        snprintf(app->pod_open.title, sizeof(app->pod_open.title), "%s", feed->title);
    if (!app->pod_open.author[0])
        snprintf(app->pod_open.author, sizeof(app->pod_open.author), "%s", feed->author);
    if (!app->pod_open.image[0])
        snprintf(app->pod_open.image, sizeof(app->pod_open.image), "%s", feed->image);
}

static void takeFeeds(App *app, uint64_t now_ns)
{
    char url[LINK_URL_MAX], error[256];
    Feed feed;
    while (podFetchTakeFeed(url, sizeof(url), &feed, error, sizeof(error))) {
        bool open = !strcmp(url, app->pod_open.feed);
        int sub = podStoreFind(&app->pods, url);
        if (open) {
            app->pod_loading = false;
            app->pod_load_failed = error[0] != '\0';
            snprintf(app->pod_error, sizeof(app->pod_error), "%s", error);
        }
        if (error[0]) {
            podcastFeedFree(&feed);
            continue;
        }
        if (sub >= 0) {
            if (podStoreTakeFeed(&app->pods, sub, &feed, nowSeconds()) >= 0)
                saveSubs(app, now_ns);
        }
        if (open) {
            fillOpen(app, &feed);
            if (sub < 0) {  // Not subscribed: the page shows this feed.
                podcastFeedFree(&app->pod_feed);
                app->pod_feed = feed;
                continue;
            }
        }
        podcastFeedFree(&feed);
    }
}

// The episode playing: its place kept now and then, and heard once it ends.
static void followEpisode(App *app, uint64_t now_ns)
{
    RadioStatus s;
    radioGetStatus(&s);
    if (!s.has_episode)
        return;
    char key[sizeof(app->pod_marked)];
    snprintf(key, sizeof(key), "%s\n%s", s.episode.feed, s.episode.guid);
    if (s.ended) {
        if (strcmp(app->pod_marked, key)) {
            snprintf(app->pod_marked, sizeof(app->pod_marked), "%s", key);
            podStoreSetPlayed(&app->pods, s.episode.feed, s.episode.guid, true, nowSeconds());
            saveMarks(app, now_ns);
        }
        return;
    }
    if (strcmp(app->pod_marked, key))
        app->pod_marked[0] = '\0';
    bool running = !s.paused && s.state == RADIO_PLAYING;
    if (running && now_ns - app->pod_saved_ns >= SAVE_EVERY_NS) {
        app->pod_saved_ns = now_ns;
        if (keepPlace(app))
            podStoreSaveMarks(&app->pods);  // Quietly: it's said if the card fails elsewhere.
    }
}

void appPodcastsUpdate(App *app, uint64_t now_ns)
{
    takeSearch(app);
    if (app->view == VIEW_PODCASTS && app->pod_page == POD_PAGE_TOP && !app->pod_top_failed && !app->pod_searching)
        askTop(app);  // Shown, and not had yet (or for another region, or a search took its place).
    takeFeeds(app, now_ns);
    followEpisode(app, now_ns);
}

// --- Searching, opening, subscribing ---

// The results page, waiting for the request just made.
static void showResults(App *app)
{
    PodSearchStatus st;
    podFetchSearchStatus(&st);
    app->pod_search = st.generation;
    app->pod_searching = true;
    app->pod_wait = POD_WAIT_RESULTS;
    app->pod_search_failed = false;
    if (app->pod_page != POD_PAGE_RESULTS)
        app->pod_results_from = app->pod_page < POD_MODES ? app->pod_page : POD_PAGE_SUBS;
    app->pod_page = POD_PAGE_RESULTS;
    app->pod_cursor[POD_PAGE_RESULTS] = 0;
    uiListRelease();
}

static void typeSearch(App *app, uint64_t now_ns)
{
    char text[SEARCH_MAX];
    SystemKeyboardResult r =
        systemKeyboardInput(T("Find a podcast"), app->pod_query, text, sizeof(text), SEARCH_MAX - 1);
    if (r == SYSTEM_KEYBOARD_UNAVAILABLE) {
        appNotify(app, NOTICE_ERROR, T("The keyboard couldn't open."), now_ns);
        return;
    }
    if (r != SYSTEM_KEYBOARD_OK || !text[0])
        return;
    snprintf(app->pod_query, sizeof(app->pod_query), "%s", text);
    podcastResultsFree(&app->pod_results);
    podFetchSearch(app->pod_query);
    snprintf(app->pod_results_title, sizeof(app->pod_results_title), T("Podcasts for \xE2\x80\x9C%s\xE2\x80\x9D"),
             app->pod_query);
    app->pod_genre = -1;
    showResults(app);
}

// A genre's top podcasts in the region.
static void openGenre(App *app, size_t index)
{
    if (index >= POD_GENRE_COUNT)
        return;
    podcastResultsFree(&app->pod_results);
    podFetchChart(appPodRegion(app), POD_GENRES[index].id);
    snprintf(app->pod_results_title, sizeof(app->pod_results_title), "%s \xC2\xB7 %s", T(POD_GENRES[index].name),
             appPodRegionName(appPodRegion(app)));
    app->pod_genre = (int)index;
    showResults(app);
}

// To the regions; the one in use selected.
static void openRegions(App *app)
{
    if (app->pod_page != POD_PAGE_REGIONS)
        app->pod_regions_from = app->pod_page;
    app->pod_page = POD_PAGE_REGIONS;
    const char *region = appPodRegion(app);
    for (size_t i = 0; i < POD_REGION_COUNT; i++)
        if (!strcmp(POD_REGIONS[i].code, region))
            app->pod_cursor[POD_PAGE_REGIONS] = i;
    uiListRelease();
}

static void chooseRegion(App *app, size_t index, uint64_t now_ns)
{
    if (index >= POD_REGION_COUNT)
        return;
    snprintf(app->config.podcast_region, sizeof(app->config.podcast_region), "%s", POD_REGIONS[index].code);
    if (!configSave(&app->config, app->config_folder))
        appNotify(app, NOTICE_ERROR, T("The settings couldn't be saved on the card."), now_ns);
    app->pod_top_failed = false;  // The top podcasts there come next time Top shows.
    app->pod_page = app->pod_regions_from;
    app->pod_cursor[POD_PAGE_TOP] = 0;
    uiListRelease();
}

// To one of the three modes (Yours, Top, Genres).
static void setMode(App *app, PodPage page)
{
    app->pod_page = page;
    uiListRelease();
}

static void openPodcast(App *app, const PodcastResult *r, PodPage from)
{
    app->pod_open = *r;
    app->pod_from = from;
    app->pod_page = POD_PAGE_EPISODES;
    app->pod_cursor[POD_PAGE_EPISODES] = 0;
    app->pod_load_failed = false;
    podcastFeedFree(&app->pod_feed);
    uiListRelease();
    int sub = podStoreFind(&app->pods, r->feed);
    bool fresh = sub >= 0 && app->pods.subs[sub].count > 0 &&
                 nowSeconds() - app->pods.subs[sub].checked < FRESH_FEED_S;
    app->pod_loading = !fresh;
    if (!fresh)
        podFetchFeed(r->feed);
}

static void back(App *app, uint64_t now_ns)
{
    if (app->pod_page == POD_PAGE_EPISODES) {
        int sub = appPodOpenSub(app);
        if (sub >= 0) {  // Its episodes have been seen: none is "new" now.
            podStoreSeen(&app->pods, sub, nowSeconds());
            saveSubs(app, now_ns);
        }
        app->pod_page = app->pod_from;
    } else if (app->pod_page == POD_PAGE_RESULTS) {
        app->pod_page = app->pod_results_from;
    } else if (app->pod_page == POD_PAGE_REGIONS) {
        app->pod_page = app->pod_regions_from;
    }
    uiListRelease();
}

// The subscription's episodes, kept for the page after unsubscribing.
static void keepEpisodes(App *app, const Subscription *sub)
{
    podcastFeedFree(&app->pod_feed);
    if (!sub->count)
        return;
    app->pod_feed.episodes = malloc(sub->count * sizeof(Episode));
    if (!app->pod_feed.episodes)
        return;
    memcpy(app->pod_feed.episodes, sub->episodes, sub->count * sizeof(Episode));
    app->pod_feed.count = sub->count;
}

static void toggleSubscription(App *app, const PodcastResult *r, uint64_t now_ns)
{
    char line[NOTICE_TEXT_MAX];
    int sub = podStoreFind(&app->pods, r->feed);
    bool open = !strcmp(r->feed, app->pod_open.feed);
    if (sub >= 0) {
        if (open)
            keepEpisodes(app, &app->pods.subs[sub]);
        if (!podStoreUnsubscribe(&app->pods, sub)) {
            appNotify(app, NOTICE_ERROR, T("It couldn't be written to the card."), now_ns);
            return;
        }
        snprintf(line, sizeof(line), T("Unsubscribed from %s"), r->title);
        appNotify(app, NOTICE_DONE, line, now_ns);
        if (app->pod_cursor[POD_PAGE_SUBS] >= app->pods.sub_count && app->pod_cursor[POD_PAGE_SUBS] > 0)
            app->pod_cursor[POD_PAGE_SUBS]--;
        return;
    }
    sub = podStoreSubscribe(&app->pods, r, nowSeconds());
    if (sub < 0) {
        appNotify(app, NOTICE_ERROR,
                  app->pods.sub_count >= PODSTORE_SUBS_MAX ? T("That's as many podcasts as KaiRadio keeps.")
                                                            : T("It couldn't be written to the card."),
                  now_ns);
        return;
    }
    if (open && app->pod_feed.count) {  // Its episodes are here already.
        podStoreTakeFeed(&app->pods, sub, &app->pod_feed, nowSeconds());
        podcastFeedFree(&app->pod_feed);
        saveSubs(app, now_ns);
    } else {
        podFetchFeed(r->feed);
    }
    snprintf(line, sizeof(line), T("Subscribed to %s"), r->title);
    appNotify(app, NOTICE_DONE, line, now_ns);
}

static void subscribeOpen(App *app, uint64_t now_ns)
{
    PodcastResult r = app->pod_open;
    toggleSubscription(app, &r, now_ns);
}

static void subscribeResult(App *app, size_t index, uint64_t now_ns)
{
    const PodcastResults *list = pageResults(app);
    if (!list || index >= list->count)
        return;
    PodcastResult r = list->items[index];
    toggleSubscription(app, &r, now_ns);
}

// --- Episodes ---

static void playEpisode(App *app, size_t index)
{
    size_t count;
    const Episode *list = appPodEpisodes(app, &count);
    if (index >= count)
        return;
    const Episode *e = &list[index];
    RadioStatus s;
    radioGetStatus(&s);
    if (s.has_episode && !s.ended && s.state != RADIO_FAILED && s.state != RADIO_STOPPED &&
        !strcmp(s.episode.feed, app->pod_open.feed) && !strcmp(s.episode.guid, e->guid)) {
        appSetView(app, VIEW_NOW_PLAYING);  // It's the one playing.
        return;
    }
    if (keepPlace(app))
        podStoreSaveMarks(&app->pods);
    RadioEpisode r;
    memset(&r, 0, sizeof(r));
    snprintf(r.feed, sizeof(r.feed), "%s", app->pod_open.feed);
    snprintf(r.guid, sizeof(r.guid), "%s", e->guid);
    snprintf(r.title, sizeof(r.title), "%s", e->title);
    snprintf(r.podcast, sizeof(r.podcast), "%s", app->pod_open.title);
    snprintf(r.image, sizeof(r.image), "%s", app->pod_open.image);
    snprintf(r.url, sizeof(r.url), "%s", e->url);
    snprintf(r.type, sizeof(r.type), "%s", e->type);
    r.duration = e->duration;
    int position;
    bool heard;
    podStoreMark(&app->pods, r.feed, r.guid, &position, &heard);
    r.start = heard ? 0 : position;  // Heard: from the start again.
    app->pod_marked[0] = '\0';
    radioPlayEpisode(&r);
}

static void toggleHeard(App *app, size_t index, uint64_t now_ns)
{
    size_t count;
    const Episode *list = appPodEpisodes(app, &count);
    if (index >= count)
        return;
    int position;
    bool heard;
    podStoreMark(&app->pods, app->pod_open.feed, list[index].guid, &position, &heard);
    podStoreSetPlayed(&app->pods, app->pod_open.feed, list[index].guid, !heard, nowSeconds());
    podLog("%s: %.60s (%016llx)", heard ? "not heard" : "heard", list[index].title,
           (unsigned long long)podStoreKey(app->pod_open.feed, list[index].guid));
    saveMarks(app, now_ns);
}

bool appEpisodeActive(void)
{
    RadioStatus s;
    radioGetStatus(&s);
    return s.has_episode;
}

void appEpisodeJump(App *app, double seconds)
{
    (void)app;
    RadioStatus s;
    radioGetStatus(&s);
    if (!s.has_episode)
        return;
    double to = s.position + seconds;
    if (s.duration > 0 && to > s.duration - 1)
        to = s.duration - 1;
    radioSeek(to < 0 ? 0 : to);
}

void appEpisodeToggle(App *app)
{
    RadioStatus s;
    radioGetStatus(&s);
    if (!s.has_episode)
        return;
    if (s.ended) {
        radioSeek(0);  // Again from the start.
    } else if (s.state == RADIO_FAILED || s.state == RADIO_STOPPED) {
        RadioEpisode again = s.episode;
        again.start = (int)s.position;
        radioPlayEpisode(&again);
    } else {
        if (!s.paused && keepPlace(app))
            podStoreSaveMarks(&app->pods);
        radioPause(!s.paused);
    }
}

void appPodcastsRetry(App *app)
{
    if (app->pod_page == POD_PAGE_TOP) {
        app->pod_top_failed = false;
        podcastResultsFree(&app->pod_top);  // Asked again next frame.
    } else if (app->pod_page == POD_PAGE_RESULTS && app->pod_genre >= 0) {
        openGenre(app, (size_t)app->pod_genre);
    } else if (app->pod_page == POD_PAGE_RESULTS && app->pod_query[0]) {
        podFetchSearch(app->pod_query);
        showResults(app);
    } else if (app->pod_page == POD_PAGE_EPISODES && app->pod_open.feed[0]) {
        app->pod_loading = true;
        app->pod_load_failed = false;
        podFetchFeed(app->pod_open.feed);
    }
}

// --- Input ---

static void activate(App *app, uint64_t now_ns)
{
    size_t cursor = app->pod_cursor[app->pod_page];
    switch (app->pod_page) {
    case POD_PAGE_SUBS:
        if (cursor < app->pods.sub_count) {
            PodcastResult r = resultOf(&app->pods.subs[cursor]);
            openPodcast(app, &r, POD_PAGE_SUBS);
        }
        break;
    case POD_PAGE_TOP:
    case POD_PAGE_RESULTS: {
        const PodcastResults *list = pageResults(app);
        if (cursor < list->count) {
            PodcastResult r = list->items[cursor];
            openPodcast(app, &r, app->pod_page);
        }
        break;
    }
    case POD_PAGE_GENRES: openGenre(app, cursor); break;
    case POD_PAGE_EPISODES: playEpisode(app, cursor); break;
    case POD_PAGE_REGIONS: chooseRegion(app, cursor, now_ns); break;
    default: break;
    }
}

void appPodcastsButtons(App *app, uint64_t pressed, uint64_t now_ns)
{
    if (pressed & BUTTONS_PAGE_UP) appPodMove(app, -UI_LIST_ROWS);
    if (pressed & BUTTONS_PAGE_DOWN) appPodMove(app, UI_LIST_ROWS);
    if (pressed & HidNpadButton_A) {
        activate(app, now_ns);
        return;
    }
    if (pressed & HidNpadButton_B) {
        back(app, now_ns);
        return;
    }
    if (app->pod_page < POD_MODES) {  // Left and right: Yours, Top, Genres.
        if (pressed & (HidNpadButton_Left | HidNpadButton_StickLLeft))
            setMode(app, (PodPage)((app->pod_page + POD_MODES - 1) % POD_MODES));
        if (pressed & (HidNpadButton_Right | HidNpadButton_StickLRight))
            setMode(app, (PodPage)((app->pod_page + 1) % POD_MODES));
    }
    switch (app->pod_page) {
    case POD_PAGE_SUBS:
        if (pressed & HidNpadButton_X) typeSearch(app, now_ns);
        break;
    case POD_PAGE_TOP:
    case POD_PAGE_RESULTS:
        if (pressed & HidNpadButton_X) typeSearch(app, now_ns);
        if (pressed & HidNpadButton_Y) subscribeResult(app, app->pod_cursor[app->pod_page], now_ns);
        break;
    case POD_PAGE_GENRES:
        if (pressed & HidNpadButton_X) typeSearch(app, now_ns);
        if (pressed & HidNpadButton_Y) openRegions(app);
        break;
    case POD_PAGE_EPISODES:
        if (pressed & HidNpadButton_X) toggleHeard(app, app->pod_cursor[POD_PAGE_EPISODES], now_ns);
        if (pressed & HidNpadButton_Y) subscribeOpen(app, now_ns);
        break;
    default: break;
    }
}

// The progress bar tapped at x: the episode jumps there.
static void seekTo(int x, int y)
{
    const HitRegion *bar = uiHitFind(x, y);
    RadioStatus s;
    radioGetStatus(&s);
    if (!bar || !s.has_episode || s.duration <= 0)
        return;
    double fraction = (double)(x - (bar->x + 8)) / (double)(bar->w - 16);
    fraction = fraction < 0 ? 0 : fraction > 1 ? 1 : fraction;
    radioSeek(fraction * s.duration);
}

bool appPodcastsTap(App *app, int kind, int arg, int x, int y, uint64_t now_ns)
{
    switch (kind) {
    case HIT_POD_ROW:
        if (arg >= 0 && (size_t)arg < appPodCount(app)) {
            app->pod_cursor[app->pod_page] = (size_t)arg;
            activate(app, now_ns);
        }
        return true;
    case HIT_POD_SUBSCRIBE:
        if (arg < 0)
            subscribeOpen(app, now_ns);
        else
            subscribeResult(app, (size_t)arg, now_ns);
        return true;
    case HIT_POD_SEARCH: typeSearch(app, now_ns); return true;
    case HIT_POD_BACK: back(app, now_ns); return true;
    case HIT_POD_HEARD:
        if (arg >= 0)
            toggleHeard(app, (size_t)arg, now_ns);
        return true;
    case HIT_SEEK: seekTo(x, y); return true;
    case HIT_JUMP: appEpisodeJump(app, arg); return true;
    case HIT_POD_MODE: setMode(app, (PodPage)arg); return true;
    case HIT_POD_REGION: openRegions(app); return true;
    default: return false;
    }
}
