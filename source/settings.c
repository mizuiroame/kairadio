#include "settings.h"

#include <stddef.h>

#include "config.h"

static const char *const ROW_LABELS[SETTING_COUNT] = {
    "Accent color", "Background", "Background blur", "Clock", "World view",
    "Sleep timer", "Turn the screen off", "Start in", "Resume the last station", "About KaiRadio",
};

const char *settingsRowLabel(SettingRow row)
{
    return row >= 0 && row < SETTING_COUNT ? ROW_LABELS[row] : "";
}

const char *settingsRowHelp(SettingRow row)
{
    static const char *const HELP[SETTING_COUNT] = {
        "Highlights, buttons and the map's pick. From station logo follows the station playing.",
        "JPEG or PNG images in the Backgrounds folder next to KaiRadio, darkened so text stays readable.",
        "How soft the background image is.",
        "The clock at the top right.",
        "The World tab as a globe you turn with a finger or the stick, or as a flat map.",
        "Stops the radio after this long, for falling asleep to it. Kept until KaiRadio closes.",
        "While a station plays and nothing is pressed. Any button or touch turns it back on.",
        "The tab KaiRadio opens on. Automatic: Favorites if you have some, else World.",
        "Plays the last station again when KaiRadio opens.",
        "Version, credits and licenses.",
    };
    return row >= 0 && row < SETTING_COUNT ? HELP[row] : "";
}

const char *settingsRowSection(SettingRow row)
{
    return row == SETTING_ACCENT ? "Display" : row == SETTING_SLEEP ? "Playback" : NULL;
}

bool settingsRowEditable(SettingRow row)
{
    return row >= 0 && row < SETTING_COUNT && row != SETTING_ABOUT;
}

static int clampSleep(long minutes)
{
    return minutes < 0 ? 0 : minutes > SLEEP_MAX_MINUTES ? SLEEP_MAX_MINUTES : (int)minutes;
}

int settingsSleepStep(int minutes, int delta)
{
    minutes = clampSleep(minutes);
    int step = minutes / SLEEP_STEP_MINUTES;
    if (delta > 0)
        step += delta;
    else if (delta < 0)
        step += minutes % SLEEP_STEP_MINUTES ? delta + 1 : delta;  // 7 goes down to 5.
    return clampSleep((long)step * SLEEP_STEP_MINUTES);
}

static const int SCREEN_OFF_CHOICES[] = {0, 1, 2, 5, 10, 30};
#define SCREEN_OFF_COUNT ((int)(sizeof(SCREEN_OFF_CHOICES) / sizeof(SCREEN_OFF_CHOICES[0])))

int settingsScreenOffStep(int minutes, int delta)
{
    int index = 0;
    for (int i = 0; i < SCREEN_OFF_COUNT; i++)
        if (SCREEN_OFF_CHOICES[i] <= minutes)
            index = i;
    index = ((index + delta) % SCREEN_OFF_COUNT + SCREEN_OFF_COUNT) % SCREEN_OFF_COUNT;
    return SCREEN_OFF_CHOICES[index];
}

const char *settingsStartName(int start)
{
    static const char *const NAMES[START_COUNT] = {"Automatic", "World", "Popular", "Favorites", "Recent"};
    return start >= 0 && start < START_COUNT ? NAMES[start] : NAMES[START_AUTO];
}
