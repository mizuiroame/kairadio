// State of the application shared by input handling (app.c) and drawing
// (ui*.c).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

#define SEARCH_MAX 64

typedef struct {
    View view;
    View list_view;             // The list view Now Playing goes back to.
    size_t cursor[VIEW_COUNT];  // Selected row of each list.

    StationList popular;
    bool popular_asked;
    StationList results;
    char query[SEARCH_MAX];
    // The directory request that the app waits for, and for which view.
    unsigned waiting;
    View waiting_view;
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

// The stations a list view shows (NULL for Now Playing).
const StationList *appList(const App *app, View view);

void appInit(App *app, const char *config_folder);
void appFree(App *app);

// Shows a short message in the player bar.
void appToast(App *app, const char *message, uint64_t now_ns);

// Once per frame, before drawing: takes directory answers and notices the
// station changing state.
void appUpdate(App *app, uint64_t now_ns);

// Buttons pressed this frame (with auto-repeat), and touch input.
void appHandleButtons(App *app, uint64_t pressed, uint64_t now_ns);
void appHandleTouch(App *app, bool touching, int x, int y, uint64_t now_ns);
// What a tap at (x, y) does, against the last frame's targets. For the
// preview's checks too. Returns false if nothing is there.
bool appTap(App *app, int x, int y, uint64_t now_ns);

void appSetView(App *app, View view);
