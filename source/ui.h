// Drawing the screen from the app state. Every control adds its touch target
// (ui_hit.h) as it is drawn.
#pragma once

#include <stdint.h>

#include "app.h"
#include "radio.h"

// The station lists: rows of UI_ROW_H, UI_LIST_ROWS of them on screen.
#define UI_LIST_Y 112
#define UI_ROW_H 66
#define UI_LIST_ROWS 7

void uiDraw(const App *app, const RadioStatus *radio, uint64_t now_ns);

// The first row shown so that the cursor stays in view.
size_t uiFirstVisible(size_t cursor, size_t count);

// A list as a finger left it, as on a phone: where it is (free: not centered
// on the cursor), the list it's for, and what that list was when drawn.
typedef struct {
    bool free;
    const void *key;      // The list (its cursor's address): another one starts centered.
    size_t first;
    int offset;           // Pixels into the first row in view.
    size_t cursor, count; // As the list was drawn last.
    size_t want_cursor;   // Where the cursor should be to stay in view, after a move.
} UiListScroll;
extern UiListScroll g_list_scroll;

// A finger moves the list last drawn by px (positive: up). False at an end.
// want_cursor then says where the cursor goes to stay in view.
bool uiListScrollPx(int px);
// A button moved the cursor: the list keeps it in its middle again.
void uiListRelease(void);
