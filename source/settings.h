// The Settings pages and their rows, and the choices that step (as KaiPlayer's
// and KaiVideo's). Pure logic; app_settings.c applies them and ui_settings.c
// draws them.
#pragma once

#include <stdbool.h>

typedef enum {
    // The main page.
    SETTING_OPEN_PLAYBACK,  // Opens the Playback page.
    SETTING_OPEN_DISPLAY,   // Opens the Display page.
    SETTING_ABOUT,          // Opens About; shows the version.
    // Playback.
    SETTING_SLEEP,
    SETTING_RESUME,
    SETTING_SCREEN_OFF,
    // Display.
    SETTING_LANGUAGE,  // The language KaiRadio is shown in (languages.h).
    SETTING_ACCENT,
    SETTING_BACKGROUND,
    SETTING_BLUR,
    SETTING_CLOCK,
    SETTING_WORLD,  // The World tab as a globe or a flat map.
    SETTING_START,
    SETTING_COUNT,
} SettingRow;

// Settings opens on the main page, whose rows open the others.
typedef enum {
    SETTINGS_PAGE_MAIN,
    SETTINGS_PAGE_PLAYBACK,
    SETTINGS_PAGE_DISPLAY,
    SETTINGS_PAGE_COUNT,
} SettingsPage;

#define SETTINGS_PAGE_MAX_ROWS 8

// The rows of a page, in order. Returns how many.
int settingsPageRows(SettingsPage page, SettingRow rows[SETTINGS_PAGE_MAX_ROWS]);
const char *settingsPageTitle(SettingsPage page);
// True for a row that opens a page, and which.
bool settingOpensPage(SettingRow row, SettingsPage *page);
// The main page's row that opens a page (the cursor goes back on it when the page closes).
SettingRow settingPageOpener(SettingsPage page);

const char *settingsRowLabel(SettingRow row);
// A line saying what the row does, shown at the panel's foot for the one picked.
const char *settingsRowHelp(SettingRow row);
// True for rows changed with left and right; the others open something with A.
bool settingsRowEditable(SettingRow row);

#define SLEEP_STEP_MINUTES 5
#define SLEEP_MAX_MINUTES 600  // 10 hours.

// Moves the sleep timer by delta steps of 5 minutes, to the next multiple
// of 5, within 0 (off) and SLEEP_MAX_MINUTES.
int settingsSleepStep(int minutes, int delta);

// The "screen off while playing" choices, in minutes; 0 is never.
int settingsScreenOffStep(int minutes, int delta);

// The StartView's name ("Automatic", "World"...).
const char *settingsStartName(int start);
