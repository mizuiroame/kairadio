// The panels over a view: the + menu, Settings and About.
#pragma once

#include <stdint.h>

#include "app.h"

// Draws app->overlay's panel, if any, over what's drawn. Its touch targets
// replace the view's, so only the panel takes taps.
void uiDrawOverlay(const App *app, uint64_t now_ns);
