// Settings kept on the card, in settings.txt in KaiRadio's folder: one
// "key=value" per line, as KaiPlayer keeps its own.
#pragma once

#include <stdbool.h>

#define CONFIG_BACKGROUND_MAX 128

// Where KaiRadio opens.
typedef enum {
    START_AUTO,       // Favorites if there are some, else World.
    START_WORLD,
    START_POPULAR,
    START_FAVORITES,
    START_RECENT,
    START_COUNT,
} StartView;

typedef struct {
    int accent;                 // Theme accent, see theme.h.
    char background[CONFIG_BACKGROUND_MAX];  // An image in the Backgrounds folder; empty for none.
    int background_blur;        // BackgroundBlur.
    bool clock_24h;             // 21:47 rather than 9:47 PM.
    int screen_off_minutes;     // Turn the screen off after this long idle while playing; 0 never.
    int start_view;             // StartView.
    bool resume;                // Play the last station again when KaiRadio opens.
    bool world_globe;           // The World view as a globe (Y turns it into the flat map and back).
    char language[64];          // Its id: "en", "es", or a file in config/lang; "" is English.
    char language_name[64];     // Its name, as Settings shows it (not kept: set at start).
} Config;

void configDefaults(Config *c);
// Reads folder/settings.txt over the defaults; a missing file leaves them.
void configLoad(Config *c, const char *folder);
bool configSave(const Config *c, const char *folder);
