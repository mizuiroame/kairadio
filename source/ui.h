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
