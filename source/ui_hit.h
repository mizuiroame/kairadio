// Touch targets. The UI adds a region for each control as it draws it; a
// tap is matched against the regions of the last frame, topmost first, so
// touch areas always match what is on screen.
#pragma once

#include <stdbool.h>

#define HIT_MAX_REGIONS 256

typedef enum {
    HIT_TAB,          // arg: View.
    HIT_LIST,         // The scrolling list area, for drags.
    HIT_ROW,          // arg: index in the current list. A tap plays it.
    HIT_CATEGORY,     // arg: index of a genre, country or language. A tap opens it.
    HIT_SEARCH_MODE,  // arg: SearchMode.
    HIT_FAVORITE,     // The heart: arg is a row index, or -1 for the station playing.
    HIT_MINI_PLAYER,  // Opens Now Playing.
    HIT_PLAY,         // Play or stop.
    HIT_PREV,         // The station before, in the list it was played from.
    HIT_NEXT,
    HIT_BACK,         // Like B: out of Now Playing, or out of a category.
    HIT_SEARCH,       // The search box: types a search.
    HIT_RETRY,        // Asks the directory again.
    HIT_EXIT,         // The X in the corner: quits.
    HIT_MAP,          // The World view's map: a tap picks the country nearest, or opens the one picked.
    HIT_MAP_OPEN,     // The card of the country picked: opens its stations.
    HIT_RANDOM,       // Plays a station from a random country.
    HIT_SORT,         // How genres, countries or languages are sorted: a tap changes it.
    HIT_MENU_BUTTON,  // The title: opens the + menu.
    HIT_MENU_ITEM,    // arg: MenuItem, or ExitItem in the exit question.
    HIT_SETTING,      // arg: SettingRow. A tap picks a choice, a tap on the picked one steps it; a page or About opens.
    HIT_SETTING_STEP, // arg: SettingRow * 2, + 1 for the right half: steps it back or forward.
    HIT_SETTINGS_BACK,  // The Back button on a Settings page: back to the main one.
    HIT_OVERLAY_OUT,  // Outside a panel: closes it (About back to Settings).
    HIT_NOTICE,       // The notice bubble at the top: a tap sends it away.
    HIT_POD_ROW,      // arg: a row of the Podcasts tab's list. A tap opens the podcast or plays the episode.
    HIT_POD_SUBSCRIBE,  // arg: a row of the search's answers, or -1 for the open podcast. Subscribes or not.
    HIT_POD_SEARCH,   // Types a podcast search.
    HIT_POD_BACK,     // Back from a podcast's episodes or a search's answers.
    HIT_POD_HEARD,    // arg: an episode's row: heard or not.
    HIT_SEEK,         // The episode's progress bar: a tap jumps there.
    HIT_JUMP,         // arg: seconds to jump by (-15 or 30).
    HIT_POD_MODE,     // arg: the Podcasts tab's mode (a PodPage: Yours, Top, Genres).
    HIT_POD_REGION,   // The region of the charts: opens the list of them.
} HitKind;

typedef struct {
    int x, y, w, h;
    HitKind kind;
    int arg;
} HitRegion;

void uiHitReset(void);
void uiHitAdd(int x, int y, int w, int h, HitKind kind, int arg);
// Targets added from now on are cut to between top and bottom (a list's place).
void uiHitClip(int top, int bottom);
// The topmost region at (x, y), or NULL.
const HitRegion *uiHitFind(int x, int y);
// The regions of the last frame, bottom first: for checks.
int uiHitCount(void);
const HitRegion *uiHitAt(int index);
