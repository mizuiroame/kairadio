// The World view: a night map of the world, each country lit by how many
// stations it has, and a card for the one picked.
#pragma once

#include <stdint.h>

#include "app.h"

void uiDrawMap(const App *app, uint64_t now_ns);
// Day and night are drawn for this moment (UTC seconds) instead of the
// console's clock; 0 goes back to the clock. For the previews.
void uiMapSetClock(int64_t utc);
