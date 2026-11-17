// The World view: a night map of the world, each country lit by how many
// stations it has, and a card for the one picked.
#pragma once

#include <stdint.h>

#include "app.h"

void uiDrawMap(const App *app, uint64_t now_ns);
