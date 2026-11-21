#include "settings.h"

#include <stddef.h>

#include "config.h"
#include "lang.h"

static const char *const ROW_LABELS[SETTING_COUNT] = {
    N_("Language"), N_("Accent color"), N_("Background"), N_("Background blur"), N_("Clock"), N_("World view"),
    N_("Sleep timer"), N_("Turn the screen off"), N_("Start in"), N_("Resume the last station"), N_("About KaiRadio"),
};

const char *settingsRowLabel(SettingRow row)
{
    return row >= 0 && row < SETTING_COUNT ? T(ROW_LABELS[row]) : "";
}

const char *settingsRowHelp(SettingRow row)
{
    static const char *const HELP[SETTING_COUNT] = {
        N_("The language KaiRadio is shown in. More can be added as files in the lang folder next to KaiRadio."),
        N_("Highlights, buttons and the map's pick. From station logo follows the station playing."),
        N_("JPEG or PNG images in the Backgrounds folder next to KaiRadio, darkened so text stays readable."),
        N_("How soft the background image is."),
        N_("The clock at the top right."),
        N_("The World tab as a globe you turn with a finger or the stick, or as a flat map."),
        N_("Stops the radio after this long, for falling asleep to it. Kept until KaiRadio closes."),
        N_("While a station plays and nothing is pressed. Any button or touch turns it back on."),
        N_("The tab KaiRadio opens on. Automatic: Favorites if you have some, else World."),
        N_("Plays the last station again when KaiRadio opens."),
        N_("Version, credits and licenses."),
    };
    return row >= 0 && row < SETTING_COUNT ? T(HELP[row]) : "";
}

const char *settingsRowSection(SettingRow row)
{
    return row == SETTING_LANGUAGE ? T("Display") : row == SETTING_SLEEP ? T("Playback") : NULL;
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
    static const char *const NAMES[START_COUNT] = {N_("Automatic"), N_("World"), N_("Popular"),
                                                     N_("Favorites"), N_("Recent")};
    return start >= 0 && start < START_COUNT ? T(NAMES[start]) : T(NAMES[START_AUTO]);
}
