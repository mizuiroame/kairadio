// State of the application shared by input handling (app.c) and drawing
// (ui*.c).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "background.h"
#include "kinetic.h"
#include "categories.h"
#include "config.h"
#include "directory.h"
#include "library.h"
#include "radio.h"
#include "stations.h"
#include "sysinfo.h"
#include "touch.h"

typedef enum {
    VIEW_WORLD,  // The map: countries lit by their stations.
    VIEW_POPULAR,
    VIEW_SEARCH,
    VIEW_FAVORITES,
    VIEW_RECENT,
    VIEW_NOW_PLAYING,
    VIEW_COUNT,
} View;

// How Search finds stations: by name (typed), or by browsing a category.
typedef enum {
    SEARCH_NAME,
    SEARCH_GENRE,
    SEARCH_COUNTRY,
    SEARCH_LANGUAGE,
    SEARCH_MODES,
} SearchMode;

// What a directory answer is for.
typedef enum {
    TARGET_POPULAR,
    TARGET_NAME_RESULTS,
    TARGET_CATEGORY_RESULTS,
    TARGET_CATEGORIES,  // Of waiting_mode.
} Target;

#define SEARCH_MAX 64
#define CONFIG_FOLDER_MAX 128

// A panel over the view: the + menu, Settings, or About. Only it takes input.
typedef enum {
    OVERLAY_NONE,
    OVERLAY_MENU,
    OVERLAY_SETTINGS,
    OVERLAY_ABOUT,
} Overlay;

// The + menu's items.
typedef enum {
    MENU_SETTINGS,
    MENU_EXIT,
    MENU_COUNT,
} MenuItem;

typedef struct {
    View view;
    View list_view;             // The list view Now Playing goes back to.
    size_t cursor[VIEW_COUNT];  // Selected row of each station list.

    StationList popular;
    bool popular_asked;

    // Search.
    SearchMode search_mode;
    char query[SEARCH_MAX];
    StationList name_results;
    CategoryList categories[SEARCH_MODES];  // Genres, countries, languages (not SEARCH_NAME).
    bool categories_asked[SEARCH_MODES];
    size_t category_cursor[SEARCH_MODES];
    CategorySort category_sort[SEARCH_MODES];  // How each list of them is sorted (Y changes it).
    bool category_open;                     // Showing the stations of open_category.
    bool category_from_map;                 // Opened from the World view: Back goes there.
    Category open_category;

    // The World view: the country chosen, in categories[SEARCH_COUNTRY].
    size_t map_cursor;
    StationList category_results;

    // The directory request that the app waits for, and what it is for.
    unsigned waiting;
    Target waiting_target;
    SearchMode waiting_mode;
    bool waiting_active;
    DirectoryStatus directory;

    Library library;
    // The list the playing station was chosen from, for previous and next.
    StationList play_list;
    size_t play_index;

    // Settings, the + menu and the timers.
    Config config;
    char config_folder[CONFIG_FOLDER_MAX];
    Overlay overlay;
    int menu_cursor;             // MenuItem.
    int settings_cursor;         // SettingRow.
    char backgrounds[BACKGROUND_MAX_IMAGES][BACKGROUND_NAME_MAX];  // The images there were when Settings opened.
    size_t background_count;
    int sleep_minutes;           // The sleep timer as set; 0 is off.
    uint64_t sleep_until_ns;     // When it stops the radio; 0 for never.
    bool screen_off;             // Turned off after a while idle; any input turns it on.
    uint64_t last_input_ns;

    SysInfo sys;
    TouchTracker touch;
    Kinetic kinetic;   // A list following a finger, or gliding on after it.
    bool swallow_tap;  // A press stopped a glide: its tap doesn't open anything.
    char toast[224];
    uint64_t toast_until_ns;
    bool quit;
} App;

// The stations a view shows now, or NULL (Now Playing, or Search browsing
// categories).
const StationList *appList(const App *app, View view);
// True while Search shows a list of genres, countries or languages.
bool appBrowsingCategories(const App *app);
// True while the app waits for the directory to fill what the view shows.
bool appLoading(const App *app);
// True if the last request for what the view shows failed.
bool appFailed(const App *app);

void appInit(App *app, const char *config_folder);
void appFree(App *app);

// Shows a short message in the player bar.
void appToast(App *app, const char *message, uint64_t now_ns);

// Once per frame, before drawing: takes directory answers.
void appUpdate(App *app, uint64_t now_ns);

// Buttons pressed this frame (with auto-repeat), and touch input.
void appHandleButtons(App *app, uint64_t pressed, uint64_t now_ns);
void appHandleTouch(App *app, bool touching, int x, int y, uint64_t now_ns);
// What a tap at (x, y) does, against the last frame's targets. For the
// preview's checks too. Returns false if nothing is there.
bool appTap(App *app, int x, int y, uint64_t now_ns);

void appSetView(App *app, View view);

// Plays a station of a list, keeping the list for previous and next.
void appPlayFrom(App *app, const StationList *list, size_t index);

// app_settings.c: the + menu, Settings and About.
void appOpenMenu(App *app);
void appOpenSettings(App *app);
void appHandleOverlay(App *app, uint64_t pressed, uint64_t now_ns);
// Steps a setting's value by delta (left -1, right +1) and saves the settings.
void appChangeSetting(App *app, int row, int delta, uint64_t now_ns);
// A on a row: About opens, the others step forward.
void appActivateSetting(App *app, int row, uint64_t now_ns);
// The value a row shows ("Sky blue", "Off", "in 42 min").
void appSettingValue(const App *app, int row, uint64_t now_ns, char *out, size_t size);
// Checks the sleep timer: stops the radio when it runs out.
void appCheckSleep(App *app, uint64_t now_ns);
// Opens the stations of the country at index of categories[SEARCH_COUNTRY]
// (in Search), from the World view.
void appOpenMapCountry(App *app, size_t index);
void appSetSearchMode(App *app, SearchMode mode);
// The next way to sort the genres, countries or languages shown, keeping the
// one selected selected.
void appCycleSort(App *app, uint64_t now_ns);
