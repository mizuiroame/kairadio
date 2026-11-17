// State of the application shared by input handling (app.c) and drawing
// (ui*.c).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "categories.h"
#include "directory.h"
#include "library.h"
#include "radio.h"
#include "stations.h"
#include "sysinfo.h"
#include "touch.h"

typedef enum {
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
    bool category_open;                     // Showing the stations of open_category.
    Category open_category;
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

    SysInfo sys;
    TouchTracker touch;
    int drag_rows;  // Pixels dragged on a list, not yet a whole row.
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
void appSetSearchMode(App *app, SearchMode mode);
