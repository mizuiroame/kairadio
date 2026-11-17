#include "app.h"

#include <stdio.h>
#include <string.h>

#include "buttons.h"
#include "keyboard_system.h"
#include "ui.h"
#include "ui_hit.h"
#include "worldmap.h"

#define TOAST_NS 3000000000ull

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
    libraryLoad(&app->library, config_folder);
    // Start where the stations are: the favorites if there are some, else the world.
    app->view = app->library.favorites.count > 0 ? VIEW_FAVORITES : VIEW_WORLD;
    app->list_view = app->view;
}

void appFree(App *app)
{
    stationListFree(&app->popular);
    stationListFree(&app->name_results);
    stationListFree(&app->category_results);
    for (int i = 0; i < SEARCH_MODES; i++)
        categoryListFree(&app->categories[i]);
    stationListFree(&app->play_list);
    libraryFree(&app->library);
}

void appToast(App *app, const char *message, uint64_t now_ns)
{
    snprintf(app->toast, sizeof(app->toast), "%s", message);
    app->toast_until_ns = now_ns + TOAST_NS;
}

static void ask(App *app, Target target, DirectoryQuery query, const char *text)
{
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

void appUpdate(App *app, uint64_t now_ns)
{
    (void)now_ns;
    directoryGetStatus(&app->directory);
    if (!app->waiting_active || app->directory.generation != app->waiting)
        return;
    if (app->directory.state == DIR_READY) {
        if (app->waiting_target == TARGET_CATEGORIES) {
            CategoryList list;
            if (directoryTakeCategories(app->waiting, &list)) {
                categoryListFree(&app->categories[app->waiting_mode]);
                app->categories[app->waiting_mode] = list;
            }
        } else {
            StationList list;
            if (directoryTake(app->waiting, &list)) {
                StationList *target = app->waiting_target == TARGET_POPULAR        ? &app->popular
                                      : app->waiting_target == TARGET_NAME_RESULTS ? &app->name_results
                                                                                   : &app->category_results;
                stationListFree(target);
                *target = list;
            }
        }
        app->waiting_active = false;
    } else if (app->directory.state == DIR_FAILED) {
        app->waiting_active = false;
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

static void playSelected(App *app)
{
    const StationList *list = listOf(app, app->view);
    if (list && app->cursor[app->view] < list->count)
        playFrom(app, list, app->cursor[app->view]);
}

static void playAdjacent(App *app, int delta)
{
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
    if (status.state != RADIO_STOPPED && status.state != RADIO_FAILED)
        radioStop();
    else if (status.has_station)
        radioPlay(&status.station);
    else
        playSelected(app);
}

static void toggleFavorite(App *app, const Station *station, uint64_t now_ns)
{
    bool now = libraryToggleFavorite(&app->library, station);
    char message[STATION_NAME_MAX + 48];
    snprintf(message, sizeof(message), now ? "Added %s to favorites" : "Removed %s from favorites", station->name);
    appToast(app, message, now_ns);
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
    SystemKeyboardResult r = systemKeyboardInput("Search stations", app->query, text, sizeof(text), SEARCH_MAX - 1);
    if (r == SYSTEM_KEYBOARD_UNAVAILABLE) {
        appToast(app, "The keyboard couldn't open.", now_ns);
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
    if (app->view == VIEW_WORLD) {
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
        return;
    }
    if (pressed & HidNpadButton_A) playSelected(app);
    if (pressed & HidNpadButton_Y) favoriteSelected(app, now_ns);
    if ((pressed & HidNpadButton_X) && app->search_mode == SEARCH_NAME) typeSearch(app, now_ns);
    if ((pressed & HidNpadButton_B) && app->category_open) backFromCategory(app);
}

static void handleWorld(App *app, uint64_t pressed)
{
    const CategoryList *countries = &app->categories[SEARCH_COUNTRY];
    int dx = (pressed & BUTTONS_RIGHT) ? 1 : (pressed & BUTTONS_LEFT) ? -1 : 0;
    int dy = (pressed & BUTTONS_DOWN) ? 1 : (pressed & BUTTONS_UP) ? -1 : 0;
    if (dx || dy) {
        // One way at a time: a diagonal of the stick takes the up or down.
        int next = mapStep(countries, app->map_cursor, dy ? 0 : dx, dy);
        if (next >= 0)
            app->map_cursor = (size_t)next;
    }
    if (pressed & HidNpadButton_A) appOpenMapCountry(app, app->map_cursor);
    if (pressed & HidNpadButton_X) appSetView(app, VIEW_NOW_PLAYING);
}

void appHandleButtons(App *app, uint64_t pressed, uint64_t now_ns)
{
    if (pressed & HidNpadButton_Plus) {
        app->quit = true;
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
        handleWorld(app, pressed);
        return;
    }
    if (pressed & BUTTONS_UP) moveCursor(app, -1);
    if (pressed & BUTTONS_DOWN) moveCursor(app, 1);
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
        int nearest = mapNearest(&app->categories[SEARCH_COUNTRY], (float)x, (float)y);
        if (nearest < 0)
            break;
        if ((size_t)nearest == app->map_cursor)
            appOpenMapCountry(app, app->map_cursor);  // A second tap on it opens it.
        else
            app->map_cursor = (size_t)nearest;
        break;
    }
    case HIT_MAP_OPEN: appOpenMapCountry(app, app->map_cursor); break;
    }
    return true;
}

void appHandleTouch(App *app, bool touching, int x, int y, uint64_t now_ns)
{
    // Touch coordinates are in the 1280x720 layout.
    TouchEvent e = touchUpdate(&app->touch, touching, x, y, now_ns);
    if (e.type == TOUCH_TAP) {
        appTap(app, e.x, e.y, now_ns);
    } else if (e.type == TOUCH_DRAG) {
        const HitRegion *start = uiHitFind(e.start_x, e.start_y);
        bool on_list = start && (start->kind == HIT_LIST || start->kind == HIT_ROW ||
                                 start->kind == HIT_FAVORITE || start->kind == HIT_CATEGORY);
        if (!on_list)
            return;
        // Dragging up shows the rows below, as on a phone.
        app->drag_rows -= e.dy;
        while (app->drag_rows >= UI_ROW_H) {
            moveCursor(app, 1);
            app->drag_rows -= UI_ROW_H;
        }
        while (app->drag_rows <= -UI_ROW_H) {
            moveCursor(app, -1);
            app->drag_rows += UI_ROW_H;
        }
    } else if (e.type == TOUCH_DRAG_END) {
        app->drag_rows = 0;
    }
}
