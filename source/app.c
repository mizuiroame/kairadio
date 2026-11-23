#include "app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "buttons.h"
#include "keyboard_system.h"
#include "lang.h"
#include "settings.h"
#include "ui.h"
#include "ui_hit.h"
#include "weatherfetch.h"
#include "worldmap.h"

#define GLOBE_TURN_RATE 6.0f        // Turning to a country: this share of the way left, each second.
#define GLOBE_IDLE_NS 8000000000ull // Untouched this long, the globe turns by itself,
#define GLOBE_IDLE_DEG_S 2.5f       // this many degrees a second.
#define GLOBE_TILT_HOME 18.0f       // Turning by itself, it leans back to show the north a little.
#define RANDOM_MIN_STATIONS 5       // A random country has at least this many stations.
#define WEATHER_SETTLE_NS 600000000ull  // A country picked this long gets its weather asked for (not each one passed).


static const DirectoryQuery CATEGORY_LISTS[SEARCH_MODES] = {DIR_SEARCH, DIR_GENRES, DIR_COUNTRIES, DIR_LANGUAGES};

static StationList *listOf(App *app, View view)
{
    switch (view) {
    case VIEW_POPULAR: return &app->popular;
    case VIEW_SEARCH:
        if (app->search_mode == SEARCH_NAME)
            return &app->name_results;
        return app->category_open ? &app->category_results : NULL;
    case VIEW_FAVORITES: return &app->library.favorites;
    case VIEW_RECENT: return &app->library.recent;
    default: return NULL;
    }
}

const StationList *appList(const App *app, View view)
{
    return listOf((App *)app, view);
}

bool appBrowsingCategories(const App *app)
{
    return app->view == VIEW_SEARCH && app->search_mode != SEARCH_NAME && !app->category_open;
}

// Whether a request for target is what the view shows now.
static bool targetShown(const App *app, Target target, SearchMode mode)
{
    switch (target) {
    case TARGET_POPULAR: return app->view == VIEW_POPULAR;
    case TARGET_NAME_RESULTS: return app->view == VIEW_SEARCH && app->search_mode == SEARCH_NAME;
    case TARGET_CATEGORY_RESULTS:
        return app->view == VIEW_SEARCH && app->search_mode != SEARCH_NAME && app->category_open;
    case TARGET_CATEGORIES:
        return (appBrowsingCategories(app) && app->search_mode == mode) ||
               (app->view == VIEW_WORLD && mode == SEARCH_COUNTRY);
    case TARGET_RANDOM: return false;  // Said with toasts, over any view.
    }
    return false;
}

bool appLoading(const App *app)
{
    return app->waiting_active && targetShown(app, app->waiting_target, app->waiting_mode);
}

bool appFailed(const App *app)
{
    return !app->waiting_active && app->directory.state == DIR_FAILED && app->directory.generation == app->waiting &&
           targetShown(app, app->waiting_target, app->waiting_mode);
}

void appInit(App *app, const char *config_folder)
{
    memset(app, 0, sizeof(*app));
    snprintf(app->config_folder, sizeof(app->config_folder), "%s", config_folder);
    configLoad(&app->config, config_folder);
    libraryLoad(&app->library, config_folder);
    app->globe_view = (GlobeView){-30.0f, GLOBE_TILT_HOME};  // The Atlantic, the Americas and Europe in view.
    // Start where Settings say; by default where the stations are: the
    // favorites if there are some, else the world.
    static const View START_VIEWS[START_COUNT] = {VIEW_WORLD, VIEW_WORLD, VIEW_POPULAR, VIEW_FAVORITES, VIEW_RECENT};
    app->view = START_VIEWS[app->config.start_view];
    if (app->config.start_view == START_AUTO && app->library.favorites.count > 0)
        app->view = VIEW_FAVORITES;
    app->list_view = app->view;
}

void appFree(App *app)
{
    stationListFree(&app->popular);
    stationListFree(&app->name_results);
    stationListFree(&app->category_results);
    stationListFree(&app->random_results);
    for (int i = 0; i < SEARCH_MODES; i++)
        categoryListFree(&app->categories[i]);
    stationListFree(&app->play_list);
    libraryFree(&app->library);
}

void appNotify(App *app, NoticeKind kind, const char *message, uint64_t now_ns)
{
    noticePush(&app->notices, kind, message, now_ns);
}

void appToast(App *app, const char *message, uint64_t now_ns)
{
    appNotify(app, NOTICE_INFO, message, now_ns);
}

// The request waited for is about to be replaced: what it was for is asked again later.
static void markStale(App *app)
{
    if (!app->waiting_active)
        return;
    switch (app->waiting_target) {
    case TARGET_POPULAR: app->stale_popular = true; break;
    case TARGET_CATEGORIES: app->stale_categories[app->waiting_mode] = true; break;
    case TARGET_CATEGORY_RESULTS: app->stale_category_results = true; break;
    case TARGET_NAME_RESULTS: app->stale_name_results = true; break;
    default: break;  // A random station: said lost with the request.
    }
}

static void ask(App *app, Target target, DirectoryQuery query, const char *text)
{
    markStale(app);
    directoryRequest(query, text);
    DirectoryStatus status;
    directoryGetStatus(&status);
    app->waiting = status.generation;
    app->waiting_target = target;
    app->waiting_mode = app->search_mode;
    app->waiting_active = true;
}

static void askPopular(App *app)
{
    app->popular_asked = true;
    app->cursor[VIEW_POPULAR] = 0;
    ask(app, TARGET_POPULAR, DIR_POPULAR, NULL);
}

// The genres, countries or languages of mode (the World view asks for the countries).
static void askCategoriesOf(App *app, SearchMode mode)
{
    app->categories_asked[mode] = true;
    ask(app, TARGET_CATEGORIES, CATEGORY_LISTS[mode], NULL);
    app->waiting_mode = mode;
}

static void askCategories(App *app)
{
    askCategoriesOf(app, app->search_mode);
}

static void openCategory(App *app, size_t index)
{
    const CategoryList *list = &app->categories[app->search_mode];
    if (index >= list->count)
        return;
    app->category_cursor[app->search_mode] = index;
    app->open_category = list->items[index];
    app->category_open = true;
    app->category_from_map = false;
    app->cursor[VIEW_SEARCH] = 0;
    stationListFree(&app->category_results);
    const Category *c = &app->open_category;
    switch (app->search_mode) {
    case SEARCH_GENRE: ask(app, TARGET_CATEGORY_RESULTS, DIR_TAG, c->name); break;
    case SEARCH_COUNTRY: ask(app, TARGET_CATEGORY_RESULTS, DIR_COUNTRY, c->code); break;
    default: ask(app, TARGET_CATEGORY_RESULTS, DIR_LANGUAGE, c->name); break;
    }
}

static void closeCategory(App *app)
{
    app->category_open = false;
    stationListFree(&app->category_results);
}

// Back from a category's stations: to the list of them, or to the map it was opened from.
static void backFromCategory(App *app)
{
    bool to_map = app->category_from_map;
    closeCategory(app);
    app->category_from_map = false;
    if (to_map)
        appSetView(app, VIEW_WORLD);
}

void appSetSearchMode(App *app, SearchMode mode)
{
    if (mode == app->search_mode)
        return;
    app->search_mode = mode;
    closeCategory(app);
    app->cursor[VIEW_SEARCH] = 0;
    if (mode != SEARCH_NAME && !app->categories_asked[mode])
        askCategories(app);
}

// Where the category named like c is in list now, or fallback if it isn't.
static size_t findCategory(const CategoryList *list, const Category *c, size_t fallback)
{
    for (size_t i = 0; i < list->count; i++)
        if (strcmp(list->items[i].name, c->name) == 0 && strcmp(list->items[i].code, c->code) == 0)
            return i;
    return fallback;
}

void appCycleSort(App *app, uint64_t now_ns)
{
    // Say what changed, as the pill alone is easy to miss: "Genres sorted A to Z".
    static const char *const SAID[SEARCH_MODES][SORT_COUNT] = {
        {"", "", "", ""},
        {N_("Genres sorted with the most stations first"), N_("Genres sorted with the fewest stations first"),
         N_("Genres sorted A to Z"), N_("Genres sorted Z to A")},
        {N_("Countries sorted with the most stations first"), N_("Countries sorted with the fewest stations first"),
         N_("Countries sorted A to Z"), N_("Countries sorted Z to A")},
        {N_("Languages sorted with the most stations first"), N_("Languages sorted with the fewest stations first"),
         N_("Languages sorted A to Z"), N_("Languages sorted Z to A")},
    };
    SearchMode mode = app->search_mode;
    CategoryList *list = &app->categories[mode];
    if (mode == SEARCH_NAME)
        return;
    app->category_sort[mode] = (CategorySort)((app->category_sort[mode] + 1) % SORT_COUNT);
    appToast(app, T(SAID[mode][app->category_sort[mode]]), now_ns);
    if (list->count == 0)
        return;  // The list comes sorted this way when it arrives.
    size_t *cursor = &app->category_cursor[mode];
    Category selected = list->items[*cursor < list->count ? *cursor : 0];
    Category on_map = list->items[app->map_cursor < list->count ? app->map_cursor : 0];
    categorySort(list, app->category_sort[mode]);
    *cursor = findCategory(list, &selected, 0);
    if (mode == SEARCH_COUNTRY)  // The World view's pick is in the same list.
        app->map_cursor = findCategory(list, &on_map, 0);
}

void appSetView(App *app, View view)
{
    if (view != VIEW_NOW_PLAYING)
        app->list_view = view;
    app->view = view;
    if (view == VIEW_POPULAR && !app->popular_asked)
        askPopular(app);
    if (view == VIEW_WORLD && !app->categories_asked[SEARCH_COUNTRY])
        askCategoriesOf(app, SEARCH_COUNTRY);
}

void appOpenMapCountry(App *app, size_t index)
{
    if (index >= app->categories[SEARCH_COUNTRY].count)
        return;
    app->map_cursor = index;
    appSetSearchMode(app, SEARCH_COUNTRY);
    appSetView(app, VIEW_SEARCH);
    openCategory(app, index);
    app->category_from_map = true;
}

static void playFrom(App *app, const StationList *list, size_t index);
static void turnGlobeToPick(App *app, uint64_t now_ns);
static void playRandom(App *app, uint64_t now_ns);

// A small random number (xorshift), seeded from the clock the first time.
static unsigned nextRandom(App *app, uint64_t now_ns)
{
    if (!app->random_seed)
        app->random_seed = (unsigned)(now_ns ^ (now_ns >> 29)) | 1u;
    unsigned x = app->random_seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    app->random_seed = x;
    return x;
}

void appRandomStation(App *app, uint64_t now_ns)
{
    const CategoryList *countries = &app->categories[SEARCH_COUNTRY];
    size_t candidates = 0;
    for (size_t i = 0; i < countries->count; i++)
        candidates += countries->items[i].count >= RANDOM_MIN_STATIONS && mapFindCountry(countries->items[i].code);
    if (!candidates) {
        appToast(app, T("The countries are still loading."), now_ns);
        return;
    }
    size_t pick = nextRandom(app, now_ns) % candidates;
    for (size_t i = 0; i < countries->count; i++) {
        if (countries->items[i].count < RANDOM_MIN_STATIONS || !mapFindCountry(countries->items[i].code))
            continue;
        if (pick-- == 0) {
            app->map_cursor = i;
            break;
        }
    }
    turnGlobeToPick(app, now_ns);
    char label[CATEGORY_NAME_MAX], line[NOTICE_TEXT_MAX];
    categoryLabel(&countries->items[app->map_cursor], label, sizeof(label));
    snprintf(line, sizeof(line), T("Somewhere unexpected: %s\xE2\x80\xA6"), label);
    appToast(app, line, now_ns);
    ask(app, TARGET_RANDOM, DIR_COUNTRY, countries->items[app->map_cursor].code);
}

// The random country's stations came: one of them plays.
static void playRandom(App *app, uint64_t now_ns)
{
    if (app->random_results.count == 0) {
        appNotify(app, NOTICE_ERROR, T("That country's stations can't be had right now: try again."), now_ns);
        return;
    }
    size_t index = nextRandom(app, now_ns) % app->random_results.count;
    playFrom(app, &app->random_results, index);
    const Station *s = &app->random_results.items[index];
    char line[NOTICE_TEXT_MAX];
    if (s->country[0])
        snprintf(line, sizeof(line), "%.120s \xC2\xB7 %.80s", s->name, s->country);
    else
        snprintf(line, sizeof(line), T("%s \xC2\xB7 on the air"), s->name);
    appToast(app, line, now_ns);
}

// What the view shows, if its request was replaced before it came: asked again.
static void askStale(App *app)
{
    if (app->waiting_active)
        return;
    SearchMode mode = app->search_mode;
    if (app->view == VIEW_POPULAR && app->stale_popular) {
        app->stale_popular = false;
        askPopular(app);
    } else if (app->view == VIEW_WORLD && app->stale_categories[SEARCH_COUNTRY]) {
        app->stale_categories[SEARCH_COUNTRY] = false;
        askCategoriesOf(app, SEARCH_COUNTRY);
    } else if (appBrowsingCategories(app) && app->stale_categories[mode]) {
        app->stale_categories[mode] = false;
        askCategories(app);
    } else if (app->view == VIEW_SEARCH && app->category_open && app->stale_category_results) {
        app->stale_category_results = false;
        bool from_map = app->category_from_map;
        openCategory(app, app->category_cursor[mode]);
        app->category_from_map = from_map;
    } else if (app->view == VIEW_SEARCH && mode == SEARCH_NAME && app->stale_name_results && app->query[0]) {
        app->stale_name_results = false;
        ask(app, TARGET_NAME_RESULTS, DIR_SEARCH, app->query);
    }
}

void appUpdate(App *app, uint64_t now_ns)
{
    appUpdatesTick(app, now_ns);
    appPodcastsUpdate(app, now_ns);
    app->notice = noticeShown(&app->notices, now_ns, &app->notice_slide);
    askStale(app);
    directoryGetStatus(&app->directory);
    if (!app->waiting_active || app->directory.generation != app->waiting)
        return;
    if (app->directory.state == DIR_READY) {
        if (app->waiting_target == TARGET_CATEGORIES) {
            CategoryList list;
            if (directoryTakeCategories(app->waiting, &list)) {
                categoryListFree(&app->categories[app->waiting_mode]);
                categorySort(&list, app->category_sort[app->waiting_mode]);
                app->categories[app->waiting_mode] = list;
            }
        } else {
            StationList list;
            if (directoryTake(app->waiting, &list)) {
                StationList *target = app->waiting_target == TARGET_POPULAR        ? &app->popular
                                      : app->waiting_target == TARGET_NAME_RESULTS ? &app->name_results
                                      : app->waiting_target == TARGET_RANDOM       ? &app->random_results
                                                                                   : &app->category_results;
                stationListFree(target);
                *target = list;
            }
        }
        app->waiting_active = false;
        if (app->waiting_target == TARGET_RANDOM)
            playRandom(app, now_ns);
    } else if (app->directory.state == DIR_FAILED) {
        app->waiting_active = false;
        if (app->waiting_target == TARGET_RANDOM)
            appNotify(app, NOTICE_ERROR, T("The station directory can't be reached."), now_ns);
        // Ask again next time rather than keep an empty list.
        if (app->waiting_target == TARGET_CATEGORIES)
            app->categories_asked[app->waiting_mode] = false;
    }
}

// --- Playing ---

static void playFrom(App *app, const StationList *list, size_t index)
{
    if (index >= list->count)
        return;
    Station station = list->items[index];
    // Keep a copy of the list for previous and next: the one shown may change.
    if (list != &app->play_list) {
        StationList copy = {0};
        for (size_t i = 0; i < list->count; i++)
            if (!stationListAppend(&copy, &list->items[i]))
                break;
        stationListFree(&app->play_list);
        app->play_list = copy;
    }
    app->play_index = index;
    radioPlay(&station);
    directoryCountClick(station.uuid);
    libraryPlayed(&app->library, &station);
}

void appPlayFrom(App *app, const StationList *list, size_t index)
{
    playFrom(app, list, index);
}

static void playSelected(App *app)
{
    const StationList *list = listOf(app, app->view);
    if (list && app->cursor[app->view] < list->count)
        playFrom(app, list, app->cursor[app->view]);
}

static void playAdjacent(App *app, int delta)
{
    if (appEpisodeActive()) {  // An episode: L and R jump in it instead.
        appEpisodeJump(app, delta < 0 ? -EPISODE_BACK_S : EPISODE_AHEAD_S);
        return;
    }
    if (app->play_list.count == 0)
        return;
    size_t count = app->play_list.count;
    size_t index = (app->play_index + count + (size_t)(delta < 0 ? count - 1 : 1)) % count;
    playFrom(app, &app->play_list, index);
}

static void togglePlay(App *app)
{
    RadioStatus status;
    radioGetStatus(&status);
    if (status.has_episode)
        appEpisodeToggle(app);  // Pause and go on, rather than stop.
    else if (status.state != RADIO_STOPPED && status.state != RADIO_FAILED)
        radioStop();
    else if (status.has_station)
        radioPlay(&status.station);
    else
        playSelected(app);
}

static void toggleFavorite(App *app, const Station *station, uint64_t now_ns)
{
    bool now = libraryToggleFavorite(&app->library, station);
    char message[NOTICE_TEXT_MAX];
    snprintf(message, sizeof(message), now ? T("Added %s to favorites") : T("Removed %s from favorites"),
             station->name);
    appNotify(app, NOTICE_DONE, message, now_ns);
    // The Favorites list changed under its cursor.
    size_t count = app->library.favorites.count;
    if (app->cursor[VIEW_FAVORITES] >= count)
        app->cursor[VIEW_FAVORITES] = count ? count - 1 : 0;
}

static void favoriteSelected(App *app, uint64_t now_ns)
{
    const StationList *list = listOf(app, app->view);
    if (list && app->cursor[app->view] < list->count) {
        Station station = list->items[app->cursor[app->view]];
        toggleFavorite(app, &station, now_ns);
    }
}

static void favoritePlaying(App *app, uint64_t now_ns)
{
    RadioStatus status;
    radioGetStatus(&status);
    if (status.has_station)
        toggleFavorite(app, &status.station, now_ns);
}

static void typeSearch(App *app, uint64_t now_ns)
{
    char text[SEARCH_MAX];
    SystemKeyboardResult r = systemKeyboardInput(T("Search stations"), app->query, text, sizeof(text), SEARCH_MAX - 1);
    if (r == SYSTEM_KEYBOARD_UNAVAILABLE) {
        appNotify(app, NOTICE_ERROR, T("The keyboard couldn't open."), now_ns);
        return;
    }
    if (r != SYSTEM_KEYBOARD_OK || !text[0])
        return;
    snprintf(app->query, sizeof(app->query), "%s", text);
    stationListFree(&app->name_results);
    app->cursor[VIEW_SEARCH] = 0;
    ask(app, TARGET_NAME_RESULTS, DIR_SEARCH, app->query);
}

static void retry(App *app)
{
    if (app->view == VIEW_PODCASTS) {
        appPodcastsRetry(app);
    } else if (app->view == VIEW_WORLD) {
        askCategoriesOf(app, SEARCH_COUNTRY);
    } else if (app->view == VIEW_POPULAR) {
        askPopular(app);
    } else if (app->view == VIEW_SEARCH) {
        if (app->search_mode == SEARCH_NAME && app->query[0])
            ask(app, TARGET_NAME_RESULTS, DIR_SEARCH, app->query);
        else if (app->category_open)
            openCategory(app, app->category_cursor[app->search_mode]);
        else if (app->search_mode != SEARCH_NAME)
            askCategories(app);
    }
}

// --- Buttons ---

static void moveCursor(App *app, long delta)
{
    size_t *cursor;
    size_t count;
    if (app->view == VIEW_PODCASTS) {
        appPodMove(app, delta);
        return;
    }
    if (appBrowsingCategories(app)) {
        cursor = &app->category_cursor[app->search_mode];
        count = app->categories[app->search_mode].count;
    } else {
        const StationList *list = listOf(app, app->view);
        if (!list)
            return;
        cursor = &app->cursor[app->view];
        count = list->count;
    }
    if (count == 0)
        return;
    long next = (long)*cursor + delta;
    if (next < 0) next = 0;
    if (next >= (long)count) next = (long)count - 1;
    *cursor = (size_t)next;
}

static void switchView(App *app, int delta)
{
    appSetView(app, (View)((app->view + VIEW_COUNT + delta) % VIEW_COUNT));
}

static void handleSearch(App *app, uint64_t pressed, uint64_t now_ns)
{
    // Left and right change how to search.
    if (pressed & (HidNpadButton_Left | HidNpadButton_StickLLeft))
        appSetSearchMode(app, (SearchMode)((app->search_mode + SEARCH_MODES - 1) % SEARCH_MODES));
    if (pressed & (HidNpadButton_Right | HidNpadButton_StickLRight))
        appSetSearchMode(app, (SearchMode)((app->search_mode + 1) % SEARCH_MODES));
    if (appBrowsingCategories(app)) {
        if (pressed & HidNpadButton_A)
            openCategory(app, app->category_cursor[app->search_mode]);
        if (pressed & HidNpadButton_Y)
            appCycleSort(app, now_ns);
        return;
    }
    if (pressed & HidNpadButton_A) playSelected(app);
    if (pressed & HidNpadButton_Y) favoriteSelected(app, now_ns);
    if ((pressed & HidNpadButton_X) && app->search_mode == SEARCH_NAME) typeSearch(app, now_ns);
    if ((pressed & HidNpadButton_B) && app->category_open) backFromCategory(app);
}

// The globe turns to show the chosen country in its middle.
static void turnGlobeToPick(App *app, uint64_t now_ns)
{
    const CategoryList *countries = &app->categories[SEARCH_COUNTRY];
    if (app->map_cursor >= countries->count)
        return;
    const MapCountry *c = mapFindCountry(countries->items[app->map_cursor].code);
    if (!c)
        return;
    GeoPoint place = globeMapPlace(c->label);
    app->globe_target = (GlobeView){place.lon, place.lat > GLOBE_TILT_MAX ? GLOBE_TILT_MAX : place.lat};
    app->globe_turning = true;
    kineticStop(&app->globe_spin);
    app->globe_touched_ns = now_ns;
}

static void handleWorld(App *app, uint64_t pressed, uint64_t now_ns)
{
    const CategoryList *countries = &app->categories[SEARCH_COUNTRY];
    int dx = (pressed & BUTTONS_RIGHT) ? 1 : (pressed & BUTTONS_LEFT) ? -1 : 0;
    int dy = (pressed & BUTTONS_DOWN) ? 1 : (pressed & BUTTONS_UP) ? -1 : 0;
    if (dx || dy) {
        // One way at a time: a diagonal of the stick takes the up or down.
        int next = mapStep(countries, app->map_cursor, dy ? 0 : dx, dy);
        if (next >= 0) {
            app->map_cursor = (size_t)next;
            turnGlobeToPick(app, now_ns);
        }
    }
    if (pressed & HidNpadButton_A) appOpenMapCountry(app, app->map_cursor);
    if (pressed & HidNpadButton_X) appSetView(app, VIEW_NOW_PLAYING);
    if (pressed & HidNpadButton_Y) appRandomStation(app, now_ns);
}

// The picked country's time and weather, once it has stayed picked a moment.
static void askWeather(App *app, uint64_t now_ns)
{
    const CategoryList *countries = &app->categories[SEARCH_COUNTRY];
    if (app->view != VIEW_WORLD || app->map_cursor >= countries->count)
        return;
    const char *code = countries->items[app->map_cursor].code;
    if (strcmp(code, app->weather_code)) {  // Another country: wait until it stays.
        snprintf(app->weather_code, sizeof(app->weather_code), "%.3s", code);
        app->weather_picked_ns = now_ns;
        return;
    }
    const MapCountry *c = mapFindCountry(code);
    if (c && now_ns - app->weather_picked_ns >= WEATHER_SETTLE_NS) {
        GeoPoint place = globeMapPlace(c->label);
        weatherFetchAsk(code, place.lat, place.lon, now_ns);
    }
}

void appGlobeTick(App *app, uint64_t now_ns)
{
    askWeather(app, now_ns);
    float seconds = app->globe_frame_ns && now_ns > app->globe_frame_ns ? (now_ns - app->globe_frame_ns) / 1e9f : 0.0f;
    app->globe_frame_ns = now_ns;
    if (seconds > 0.1f)
        seconds = 0.1f;
    if (app->view != VIEW_WORLD || !app->config.world_globe || app->globe_dragging)
        return;
    int px = kineticStep(&app->globe_spin, now_ns);
    if (px) {  // Gliding on after a flick.
        app->globe_view = globeDrag(app->globe_view, (float)px, 0.0f);
        app->globe_touched_ns = now_ns;
    } else if (app->globe_turning) {
        float t = 1.0f - expf(-GLOBE_TURN_RATE * seconds);
        app->globe_view = globeToward(app->globe_view, app->globe_target, t);
        float off = fabsf(globeWrapLon(app->globe_target.lon - app->globe_view.lon)) +
                    fabsf(app->globe_target.lat - app->globe_view.lat);
        app->globe_turning = off > 0.05f;
        app->globe_touched_ns = now_ns;
    } else if (now_ns - app->globe_touched_ns > GLOBE_IDLE_NS) {  // Left alone: it turns slowly by itself.
        app->globe_view.lon = globeWrapLon(app->globe_view.lon + GLOBE_IDLE_DEG_S * seconds);
        app->globe_view.lat += (GLOBE_TILT_HOME - app->globe_view.lat) * (1.0f - expf(-0.5f * seconds));
    }
}

void appHandleButtons(App *app, uint64_t pressed, uint64_t now_ns)
{
    if (app->overlay != OVERLAY_NONE) {
        appHandleOverlay(app, pressed, now_ns);
        return;
    }
    if (pressed & HidNpadButton_Plus) {
        appOpenMenu(app);
        return;
    }
    if (pressed & HidNpadButton_ZL) switchView(app, -1);
    if (pressed & HidNpadButton_ZR) switchView(app, 1);
    if (pressed & HidNpadButton_L) playAdjacent(app, -1);
    if (pressed & HidNpadButton_R) playAdjacent(app, 1);
    if (pressed & HidNpadButton_Minus) togglePlay(app);

    if (app->view == VIEW_NOW_PLAYING) {
        if (pressed & HidNpadButton_A) togglePlay(app);
        if (pressed & HidNpadButton_Y) favoritePlaying(app, now_ns);
        if (pressed & (HidNpadButton_B | HidNpadButton_X)) appSetView(app, app->list_view);
        return;
    }
    if (app->view == VIEW_WORLD) {
        handleWorld(app, pressed, now_ns);
        return;
    }
    if (pressed & (BUTTONS_UP | BUTTONS_DOWN | BUTTONS_PAGE_UP | BUTTONS_PAGE_DOWN | HidNpadButton_StickRUp |
                   HidNpadButton_StickRDown)) {
        uiListRelease();  // The buttons take over from the finger.
        kineticStop(&app->kinetic);
    }
    if (pressed & BUTTONS_UP) moveCursor(app, -1);
    if (pressed & BUTTONS_DOWN) moveCursor(app, 1);
    if (app->view == VIEW_PODCASTS) {
        appPodcastsButtons(app, pressed, now_ns);
        return;
    }
    if (app->view == VIEW_SEARCH) {
        if (pressed & HidNpadButton_StickRUp) moveCursor(app, -UI_LIST_ROWS);
        if (pressed & HidNpadButton_StickRDown) moveCursor(app, UI_LIST_ROWS);
        handleSearch(app, pressed, now_ns);
        return;
    }
    if (pressed & BUTTONS_PAGE_UP) moveCursor(app, -UI_LIST_ROWS);
    if (pressed & BUTTONS_PAGE_DOWN) moveCursor(app, UI_LIST_ROWS);
    if (pressed & HidNpadButton_A) playSelected(app);
    if (pressed & HidNpadButton_Y) favoriteSelected(app, now_ns);
    if (pressed & HidNpadButton_X) appSetView(app, VIEW_NOW_PLAYING);
}

// --- Touch ---

bool appTap(App *app, int x, int y, uint64_t now_ns)
{
    const HitRegion *hit = uiHitFind(x, y);
    if (!hit)
        return false;
    const StationList *list = listOf(app, app->view);
    switch (hit->kind) {
    case HIT_NOTICE: noticeDismiss(&app->notices, now_ns); break;
    case HIT_TAB:
        appSetView(app, (View)hit->arg);
        break;
    case HIT_ROW:
        if (list && (size_t)hit->arg < list->count) {
            app->cursor[app->view] = (size_t)hit->arg;
            playSelected(app);
        }
        break;
    case HIT_CATEGORY: openCategory(app, (size_t)hit->arg); break;
    case HIT_SEARCH_MODE: appSetSearchMode(app, (SearchMode)hit->arg); break;
    case HIT_FAVORITE:
        if (hit->arg < 0)
            favoritePlaying(app, now_ns);
        else if (list && (size_t)hit->arg < list->count) {
            app->cursor[app->view] = (size_t)hit->arg;
            favoriteSelected(app, now_ns);
        }
        break;
    case HIT_MINI_PLAYER: appSetView(app, VIEW_NOW_PLAYING); break;
    case HIT_PLAY: togglePlay(app); break;
    case HIT_PREV: playAdjacent(app, -1); break;
    case HIT_NEXT: playAdjacent(app, 1); break;
    case HIT_BACK:
        if (app->view == VIEW_SEARCH)
            backFromCategory(app);
        else
            appSetView(app, app->list_view);
        break;
    case HIT_SEARCH: typeSearch(app, now_ns); break;
    case HIT_RETRY: retry(app); break;
    case HIT_EXIT: app->quit = true; break;
    case HIT_LIST: break;  // Only for drags.
    case HIT_MAP: {
        const CategoryList *countries = &app->categories[SEARCH_COUNTRY];
        int nearest = app->config.world_globe ? globeNearest(countries, app->globe_view, (float)x, (float)y, 48.0f)
                                              : mapNearest(countries, (float)x, (float)y);
        if (nearest < 0)
            break;
        if ((size_t)nearest == app->map_cursor) {
            appOpenMapCountry(app, app->map_cursor);  // A second tap on it opens it.
        } else {
            app->map_cursor = (size_t)nearest;
            turnGlobeToPick(app, now_ns);
        }
        break;
    }
    case HIT_MAP_OPEN: appOpenMapCountry(app, app->map_cursor); break;
    case HIT_RANDOM: appRandomStation(app, now_ns); break;
    case HIT_SORT: appCycleSort(app, now_ns); break;
    case HIT_MENU_BUTTON: appOpenMenu(app); break;
    case HIT_MENU_ITEM: appChooseMenuItem(app, hit->arg); break;
    case HIT_SETTING:
        if (!settingsRowEditable(hit->arg)) {  // A page or About: opens at once.
            app->settings_cursor = hit->arg;
            appActivateSetting(app, hit->arg, now_ns);
        } else if (app->settings_cursor == hit->arg) {
            appActivateSetting(app, hit->arg, now_ns);
        } else {
            app->settings_cursor = hit->arg;
        }
        break;
    case HIT_SETTINGS_BACK: appSettingsBack(app); break;
    case HIT_SETTING_STEP:
        app->settings_cursor = hit->arg / 2;
        appChangeSetting(app, hit->arg / 2, hit->arg % 2 ? 1 : -1, now_ns);
        break;
    case HIT_OVERLAY_OUT: app->overlay = app->overlay == OVERLAY_ABOUT ? OVERLAY_SETTINGS : OVERLAY_NONE; break;
    case HIT_POD_ROW:
    case HIT_POD_SUBSCRIBE:
    case HIT_POD_SEARCH:
    case HIT_POD_BACK:
    case HIT_POD_HEARD:
    case HIT_SEEK:
    case HIT_JUMP:
    case HIT_POD_MODE:
    case HIT_POD_REGION: appPodcastsTap(app, hit->kind, hit->arg, x, y, now_ns); break;
    }
    return true;
}

// Moves the list in view by px as a finger does, and its cursor to stay in
// view. False at an end.
static bool scrollList(App *app, int px)
{
    bool moved = uiListScrollPx(px);
    long delta = (long)g_list_scroll.want_cursor - (long)g_list_scroll.cursor;
    if (delta) {
        moveCursor(app, delta);
        g_list_scroll.cursor = g_list_scroll.want_cursor;
    }
    return moved;
}

// A finger came down, let go, or nothing: a list glides on, or stops.
static void glide(App *app, TouchEvent *e, uint64_t now_ns)
{
    if (e->type == TOUCH_PRESS) {  // A press stops a glide, and isn't a tap on what it stopped over.
        app->swallow_tap = kineticGliding(&app->kinetic);
        kineticStop(&app->kinetic);
    } else if (e->type == TOUCH_TAP && app->swallow_tap) {
        e->type = TOUCH_NONE;
    }
    if (e->type != TOUCH_PRESS && e->type != TOUCH_DRAG)
        app->swallow_tap = false;
    if (e->type == TOUCH_DRAG_END && app->kinetic.dragging)
        kineticRelease(&app->kinetic, now_ns);
    if (app->overlay != OVERLAY_NONE || app->view == VIEW_WORLD || app->view == VIEW_NOW_PLAYING)
        kineticStop(&app->kinetic);
    int px = kineticStep(&app->kinetic, now_ns);
    if (px && !scrollList(app, px))
        kineticStop(&app->kinetic);  // An end.
}

// A finger on the globe turns it, the land following it; let go moving, it spins on.
static bool globeTouch(App *app, const TouchEvent *e, uint64_t now_ns)
{
    if (app->view != VIEW_WORLD || !app->config.world_globe || app->overlay != OVERLAY_NONE)
        return false;
    if (e->type == TOUCH_PRESS) {  // Touching it stops it.
        app->globe_turning = false;
        kineticStop(&app->globe_spin);
        app->globe_touched_ns = now_ns;
        return false;
    }
    if (e->type == TOUCH_DRAG_END && app->globe_dragging) {
        app->globe_dragging = false;
        kineticRelease(&app->globe_spin, now_ns);
        return true;
    }
    if (e->type != TOUCH_DRAG)
        return false;
    const HitRegion *start = uiHitFind(e->start_x, e->start_y);
    if (!app->globe_dragging && !(start && start->kind == HIT_MAP))
        return false;
    bool starting = !app->globe_dragging;
    app->globe_dragging = true;
    app->globe_turning = false;
    kineticDrag(&app->globe_spin, starting ? 0 : e->dx, now_ns);
    app->globe_view = globeDrag(app->globe_view, (float)e->dx, (float)e->dy);
    app->globe_touched_ns = now_ns;
    return true;
}

void appHandleTouch(App *app, bool touching, int x, int y, uint64_t now_ns)
{
    // Touch coordinates are in the 1280x720 layout.
    TouchEvent e = touchUpdate(&app->touch, touching, x, y, now_ns);
    if (globeTouch(app, &e, now_ns))
        return;
    glide(app, &e, now_ns);
    if (e.type == TOUCH_TAP) {
        appTap(app, e.x, e.y, now_ns);
    } else if (e.type == TOUCH_DRAG) {
        const HitRegion *start = uiHitFind(e.start_x, e.start_y);
        bool on_list = start && (start->kind == HIT_LIST || start->kind == HIT_ROW || start->kind == HIT_FAVORITE ||
                                 start->kind == HIT_CATEGORY || start->kind == HIT_POD_ROW ||
                                 start->kind == HIT_POD_HEARD || (start->kind == HIT_POD_SUBSCRIBE && start->arg >= 0));
        if (!on_list)
            return;
        // The list goes with the finger: dragging up shows the rows below, as on a phone.
        bool starting = !app->kinetic.dragging;
        kineticDrag(&app->kinetic, starting ? 0 : -e.dy, now_ns);
        scrollList(app, -e.dy);
    }
}
