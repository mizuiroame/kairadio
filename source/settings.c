#include "settings.h"

#include <stddef.h>

#include "config.h"
#include "lang.h"

static const char *const ROW_LABELS[SETTING_COUNT] = {
    N_("Playback"), N_("Display"), N_("About"),
    N_("Sleep timer"), N_("Resume the last station"), N_("Screen off while playing"),
    N_("Language"), N_("Accent color"), N_("Background"), N_("Background blur"), N_("Clock"), N_("World view"),
    N_("Start in"), N_("Credits and licenses"), N_("Check once a day"), N_("Look for an update"),
};
// Each page's rows, ended by SETTING_COUNT.
static const SettingRow PAGE_ROWS[SETTINGS_PAGE_COUNT][SETTINGS_PAGE_MAX_ROWS] = {
    {SETTING_OPEN_PLAYBACK, SETTING_OPEN_DISPLAY, SETTING_ABOUT, SETTING_COUNT},
    {SETTING_SLEEP, SETTING_RESUME, SETTING_SCREEN_OFF, SETTING_COUNT},
    {SETTING_LANGUAGE, SETTING_ACCENT, SETTING_BACKGROUND, SETTING_BLUR, SETTING_CLOCK, SETTING_WORLD, SETTING_START,
     SETTING_COUNT},
    {SETTING_CREDITS, SETTING_UPDATE_AUTO, SETTING_UPDATE_NOW, SETTING_COUNT},
};
static const char *const PAGE_TITLES[SETTINGS_PAGE_COUNT] = {N_("Settings"), N_("Playback"), N_("Display"),
                                                              N_("About")};

int settingsPageRows(SettingsPage page, SettingRow rows[SETTINGS_PAGE_MAX_ROWS])
{
    int n = 0;
    if (page < 0 || page >= SETTINGS_PAGE_COUNT)
        return 0;
    while (n < SETTINGS_PAGE_MAX_ROWS && PAGE_ROWS[page][n] != SETTING_COUNT) {
        rows[n] = PAGE_ROWS[page][n];
        n++;
    }
    return n;
}

const char *settingsPageTitle(SettingsPage page)
{
    return page >= 0 && page < SETTINGS_PAGE_COUNT ? T(PAGE_TITLES[page]) : "";
}

bool settingOpensPage(SettingRow row, SettingsPage *page)
{
    switch (row) {
    case SETTING_OPEN_PLAYBACK: *page = SETTINGS_PAGE_PLAYBACK; return true;
    case SETTING_OPEN_DISPLAY: *page = SETTINGS_PAGE_DISPLAY; return true;
    case SETTING_ABOUT: *page = SETTINGS_PAGE_ABOUT; return true;
    default: return false;
    }
}

SettingRow settingPageOpener(SettingsPage page)
{
    return page == SETTINGS_PAGE_DISPLAY   ? SETTING_OPEN_DISPLAY
           : page == SETTINGS_PAGE_ABOUT   ? SETTING_ABOUT
                                           : SETTING_OPEN_PLAYBACK;
}

const char *settingsRowLabel(SettingRow row)
{
    return row >= 0 && row < SETTING_COUNT ? T(ROW_LABELS[row]) : "";
}

const char *settingsRowHelp(SettingRow row)
{
    static const char *const HELP[SETTING_COUNT] = {
        N_("The sleep timer, the last station when KaiRadio opens, and the screen while a station plays."),
        N_("Language, colors, background, clock, the World tab and the tab KaiRadio opens on."),
        N_("Version, credits and licenses, and updates."),
        N_("Stops the radio after this long, for falling asleep to it. Kept until KaiRadio closes."),
        N_("Plays the last station again when KaiRadio opens."),
        N_("While a station plays and nothing is pressed. Any button or touch turns it back on."),
        N_("The language KaiRadio is shown in. More can be added as files in the lang folder of KaiRadio's config folder."),
        N_("Highlights, buttons and the map's pick. From station logo follows the station playing."),
        N_("JPEG or PNG images in the Backgrounds folder of KaiRadio's config folder, darkened so text stays readable."),
        N_("How soft the background image is."),
        N_("The clock at the top right."),
        N_("The World tab as a globe you turn with a finger or the stick, or as a flat map."),
        N_("The tab KaiRadio opens on. Automatic: Favorites if you have some, else World."),
        N_("Who made KaiRadio and what it's built with, and their licenses."),
        N_("Once a day KaiRadio asks GitHub for its latest version, and says when there's a new one. From "
           "Brewstore, Brewstore updates it."),
        N_("Asks GitHub now. With a new version found, and KaiRadio not from Brewstore, A downloads it, checks "
           "it and puts it in place for the next start."),
    };
    return row >= 0 && row < SETTING_COUNT ? T(HELP[row]) : "";
}

bool settingsRowEditable(SettingRow row)
{
    // Choices: the Playback and Display rows, and the daily check. Pages and actions aren't.
    return (row >= SETTING_SLEEP && row <= SETTING_START) || row == SETTING_UPDATE_AUTO;
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
