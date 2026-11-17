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
} HitKind;

typedef struct {
    int x, y, w, h;
    HitKind kind;
    int arg;
} HitRegion;

void uiHitReset(void);
void uiHitAdd(int x, int y, int w, int h, HitKind kind, int arg);
// The topmost region at (x, y), or NULL.
const HitRegion *uiHitFind(int x, int y);
// The regions of the last frame, bottom first: for checks.
int uiHitCount(void);
const HitRegion *uiHitAt(int index);
