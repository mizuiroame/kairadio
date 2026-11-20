// The Settings page: its rows, and the choices that step (as KaiPlayer's).
// Pure logic; app_settings.c applies them and ui_settings.c draws them.
#pragma once

#include <stdbool.h>

typedef enum {
    // Display.
    SETTING_ACCENT,
    SETTING_BACKGROUND,
    SETTING_BLUR,
    SETTING_CLOCK,
    SETTING_WORLD,  // The World tab as a globe or a flat map.
    // Playback.
    SETTING_SLEEP,
    SETTING_SCREEN_OFF,
    SETTING_START,
    SETTING_RESUME,
    SETTING_ABOUT,
    SETTING_COUNT,
} SettingRow;

const char *settingsRowLabel(SettingRow row);
// A heading drawn above the row ("Display", "Playback"), or NULL.
const char *settingsRowSection(SettingRow row);
// A line saying what the row does, shown under the list for the one picked.
const char *settingsRowHelp(SettingRow row);
// False for rows that open with A rather than change with left and right (About).
bool settingsRowEditable(SettingRow row);

#define SLEEP_STEP_MINUTES 5
#define SLEEP_MAX_MINUTES 600  // 10 hours.

// Moves the sleep timer by delta steps of 5 minutes, to the next multiple
// of 5, within 0 (off) and SLEEP_MAX_MINUTES.
int settingsSleepStep(int minutes, int delta);

// The "turn the screen off after" choices, in minutes; 0 is never.
int settingsScreenOffStep(int minutes, int delta);

// The StartView's name ("Automatic", "World"...).
const char *settingsStartName(int start);
