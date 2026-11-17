#include "app.h"

#include <stdio.h>
#include <string.h>

#include "buttons.h"
#include "keyboard_system.h"
#include "ui.h"
#include "ui_hit.h"

#define TOAST_NS 3000000000ull

static StationList *listOf(App *app, View view)
{
    switch (view) {
    case VIEW_POPULAR: return &app->popular;
    case VIEW_SEARCH: return &app->results;
    case VIEW_FAVORITES: return &app->library.favorites;
    case VIEW_RECENT: return &app->library.recent;
    default: return NULL;
    }
}

const StationList *appList(const App *app, View view)
{
    return listOf((App *)app, view);
}

void appInit(App *app, const char *config_folder)
{
    memset(app, 0, sizeof(*app));
    libraryLoad(&app->library, config_folder);
    // Start where the stations are: the favorites if there are some.
    app->view = app->library.favorites.count > 0 ? VIEW_FAVORITES : VIEW_POPULAR;
    app->list_view = app->view;
}

void appFree(App *app)
{
    stationListFree(&app->popular);
    stationListFree(&app->results);
    stationListFree(&app->play_list);
    libraryFree(&app->library);
}

void appToast(App *app, const char *message, uint64_t now_ns)
{
    snprintf(app->toast, sizeof(app->toast), "%s", message);
    app->toast_until_ns = now_ns + TOAST_NS;
}

static void ask(App *app, View view, DirectoryQuery query, const char *text)
{
    directoryRequest(query, text);
    DirectoryStatus status;
    directoryGetStatus(&status);
    app->waiting = status.generation;
    app->waiting_view = view;
    app->waiting_active = true;
    app->cursor[view] = 0;
}

static void askPopular(App *app)
{
    app->popular_asked = true;
    ask(app, VIEW_POPULAR, DIR_POPULAR, NULL);
}

void appSetView(App *app, View view)
{
    if (view != VIEW_NOW_PLAYING)
        app->list_view = view;
    app->view = view;
    if (view == VIEW_POPULAR && !app->popular_asked)
        askPopular(app);
}

void appUpdate(App *app, uint64_t now_ns)
{
    (void)now_ns;
    directoryGetStatus(&app->directory);
    if (!app->waiting_active || app->directory.generation != app->waiting)
        return;
    if (app->directory.state == DIR_READY) {
        StationList list;
        if (directoryTake(app->waiting, &list)) {
            StationList *target = listOf(app, app->waiting_view);
            stationListFree(target);
            *target = list;
        }
        app->waiting_active = false;
    } else if (app->directory.state == DIR_FAILED) {
        app->waiting_active = false;
    }
}

// --- Playing ---

static void playFrom(App *app, const StationList *list, size_t index, uint64_t now_ns)
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
    (void)now_ns;
}

static void playSelected(App *app, uint64_t now_ns)
{
    const StationList *list = listOf(app, app->view);
    if (list && app->cursor[app->view] < list->count)
        playFrom(app, list, app->cursor[app->view], now_ns);
}

static void playAdjacent(App *app, int delta, uint64_t now_ns)
{
    if (app->play_list.count == 0)
        return;
    size_t count = app->play_list.count;
    size_t index = (app->play_index + count + (size_t)(delta < 0 ? count - 1 : 1)) % count;
    playFrom(app, &app->play_list, index, now_ns);
}

static void togglePlay(App *app, uint64_t now_ns)
{
    RadioStatus status;
    radioGetStatus(&status);
    if (status.state != RADIO_STOPPED && status.state != RADIO_FAILED)
        radioStop();
    else if (status.has_station)
        radioPlay(&status.station);
    else
        playSelected(app, now_ns);
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

static void search(App *app, uint64_t now_ns)
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
    stationListFree(&app->results);
    ask(app, VIEW_SEARCH, DIR_SEARCH, app->query);
}

static void retry(App *app)
{
    if (app->view == VIEW_SEARCH && app->query[0])
        ask(app, VIEW_SEARCH, DIR_SEARCH, app->query);
    else if (app->view == VIEW_POPULAR)
        askPopular(app);
}

// --- Buttons ---

static void moveCursor(App *app, long delta)
{
    const StationList *list = listOf(app, app->view);
    if (!list || list->count == 0)
        return;
    long next = (long)app->cursor[app->view] + delta;
    if (next < 0) next = 0;
    if (next >= (long)list->count) next = (long)list->count - 1;
    app->cursor[app->view] = (size_t)next;
}

static void switchView(App *app, int delta)
{
    appSetView(app, (View)((app->view + VIEW_COUNT + delta) % VIEW_COUNT));
}

void appHandleButtons(App *app, uint64_t pressed, uint64_t now_ns)
{
    if (pressed & HidNpadButton_Plus) {
        app->quit = true;
        return;
    }
    if (pressed & HidNpadButton_ZL) switchView(app, -1);
    if (pressed & HidNpadButton_ZR) switchView(app, 1);
    if (pressed & HidNpadButton_L) playAdjacent(app, -1, now_ns);
    if (pressed & HidNpadButton_R) playAdjacent(app, 1, now_ns);
    if (pressed & HidNpadButton_Minus) togglePlay(app, now_ns);

    if (app->view == VIEW_NOW_PLAYING) {
        if (pressed & HidNpadButton_A) togglePlay(app, now_ns);
        if (pressed & HidNpadButton_Y) favoritePlaying(app, now_ns);
        if (pressed & (HidNpadButton_B | HidNpadButton_X)) appSetView(app, app->list_view);
        return;
    }
    if (pressed & BUTTONS_UP) moveCursor(app, -1);
    if (pressed & BUTTONS_DOWN) moveCursor(app, 1);
    if (pressed & BUTTONS_PAGE_UP) moveCursor(app, -UI_LIST_ROWS);
    if (pressed & BUTTONS_PAGE_DOWN) moveCursor(app, UI_LIST_ROWS);
    if (pressed & HidNpadButton_A) playSelected(app, now_ns);
    if (pressed & HidNpadButton_Y) favoriteSelected(app, now_ns);
    if (pressed & HidNpadButton_X) {
        if (app->view == VIEW_SEARCH)
            search(app, now_ns);
        else
            appSetView(app, VIEW_NOW_PLAYING);
    }
    if ((pressed & HidNpadButton_B) && app->view == VIEW_SEARCH)
        appSetView(app, VIEW_POPULAR);
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
            playSelected(app, now_ns);
        }
        break;
    case HIT_FAVORITE:
        if (hit->arg < 0)
            favoritePlaying(app, now_ns);
        else if (list && (size_t)hit->arg < list->count) {
            app->cursor[app->view] = (size_t)hit->arg;
            favoriteSelected(app, now_ns);
        }
        break;
    case HIT_MINI_PLAYER: appSetView(app, VIEW_NOW_PLAYING); break;
    case HIT_PLAY: togglePlay(app, now_ns); break;
    case HIT_PREV: playAdjacent(app, -1, now_ns); break;
    case HIT_NEXT: playAdjacent(app, 1, now_ns); break;
    case HIT_BACK: appSetView(app, app->list_view); break;
    case HIT_SEARCH: search(app, now_ns); break;
    case HIT_RETRY: retry(app); break;
    case HIT_EXIT: app->quit = true; break;
    case HIT_LIST: break;  // Only for drags.
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
        bool on_list = start && (start->kind == HIT_LIST || start->kind == HIT_ROW || start->kind == HIT_FAVORITE);
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
