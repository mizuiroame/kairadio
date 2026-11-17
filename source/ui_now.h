// The Now Playing view: the station's logo large, what it plays, and its
// controls.
#pragma once

#include <stdint.h>

#include "app.h"
#include "radio.h"

void uiDrawNowPlaying(const App *app, const RadioStatus *radio, uint64_t now_ns);
