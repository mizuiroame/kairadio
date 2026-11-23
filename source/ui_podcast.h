// The Podcasts tab, and an episode in the player bar and in Now Playing.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app.h"
#include "radio.h"

void uiDrawPodcasts(const App *app, const RadioStatus *radio, uint64_t now_ns);
// The player bar while an episode plays: its progress, and back, pause and ahead.
void uiDrawEpisodeBar(const App *app, const RadioStatus *radio);
void uiDrawEpisodeNow(const App *app, const RadioStatus *radio, uint64_t now_ns);
// The help line under the Podcasts tab.
const char *uiPodcastsHelp(const App *app);
// "1:02:03" or "12:03".
void uiFormatTime(double seconds, char *out, size_t size);
