// The + menu, the Settings page and About: their buttons, and what each
// setting does when it changes.
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "app.h"
#include "buttons.h"
#include "lang.h"
#include "languages.h"
#include "radio.h"
#include "settings.h"
#include "theme.h"

#define NS_PER_MINUTE 60000000000ull

void appOpenMenu(App *app)
{
    app->overlay = OVERLAY_MENU;
    app->menu_cursor = MENU_SETTINGS;
}

static void askExit(App *app)
{
    app->overlay = OVERLAY_EXIT;
    app->menu_cursor = EXIT_CONFIRM;
}

void appChooseMenuItem(App *app, int item)
{
    if (app->overlay == OVERLAY_EXIT) {
        if (item == EXIT_CONFIRM)
            app->quit = true;
        else
            app->overlay = OVERLAY_NONE;
    } else if (item == MENU_EXIT) {
        askExit(app);
    } else {
        appOpenSettings(app);
    }
}

static void openPage(App *app, SettingsPage page)
{
    SettingRow rows[SETTINGS_PAGE_MAX_ROWS];
    settingsPageRows(page, rows);
    app->settings_page = page;
    app->settings_cursor = rows[0];
}

void appOpenSettings(App *app)
{
    app->overlay = OVERLAY_SETTINGS;
    openPage(app, SETTINGS_PAGE_MAIN);
    // The images there are now: a new one shows the next time Settings opens.
    app->background_count = backgroundList(app->config_folder, app->backgrounds, BACKGROUND_MAX_IMAGES);
}

void appSettingsBack(App *app)
{
    if (app->settings_page == SETTINGS_PAGE_MAIN) {
        app->overlay = OVERLAY_NONE;
    } else {
        app->settings_cursor = settingPageOpener((SettingsPage)app->settings_page);
        app->settings_page = SETTINGS_PAGE_MAIN;
    }
}

static void save(App *app, uint64_t now_ns)
{
    if (!configSave(&app->config, app->config_folder))
        appToast(app, T("The settings couldn't be saved on the card."), now_ns);
}

// The background now chosen, 0 for none and i + 1 for backgrounds[i].
static size_t backgroundIndex(const App *app)
{
    for (size_t i = 0; i < app->background_count; i++)
        if (strcmp(app->backgrounds[i], app->config.background) == 0)
            return i + 1;
    return 0;
}

static int wrap(int value, int delta, int count)
{
    return ((value + delta) % count + count) % count;
}

static void changeBackground(App *app, int delta, uint64_t now_ns)
{
    int choices = (int)app->background_count + 1;
    if (choices == 1) {
        char message[sizeof(app->toast)];
        snprintf(message, sizeof(message), T("Put JPEG or PNG images in %s/Backgrounds first."), app->config_folder);
        appToast(app, message, now_ns);
        return;
    }
    int index = wrap((int)backgroundIndex(app), delta, choices);
    const char *name = index == 0 ? "" : app->backgrounds[index - 1];
    snprintf(app->config.background, sizeof(app->config.background), "%s", name);
    if (!backgroundLoad(app->config_folder, name, app->config.background_blur))
        appToast(app, T("That image couldn't be opened (too big, or not a JPEG or PNG)."), now_ns);
}

void appUseLanguage(App *app)
{
    if (!languageUse(app->config.language))
        snprintf(app->config.language, sizeof(app->config.language), "%s", "en");  // Gone: English.
    snprintf(app->config.language_name, sizeof(app->config.language_name), "%s",
             languageName(languageIndex(app->config.language)));
}

void appStartLanguages(App *app)
{
    char folder[sizeof(app->config_folder) + 8];
    snprintf(folder, sizeof(folder), "%s/lang", app->config_folder);
    mkdir(folder, 0777);  // Where to put more languages' files, there to be seen.
    languagesFind(folder);
    appUseLanguage(app);
}

void appChangeSetting(App *app, int row, int delta, uint64_t now_ns)
{
    Config *c = &app->config;
    switch (row) {
    case SETTING_LANGUAGE: {  // Through the languages found: English, those in the app, config/lang's files.
        int count = languagesCount();
        snprintf(c->language, sizeof(c->language), "%s", languageId(wrap(languageIndex(c->language), delta, count)));
        appUseLanguage(app);
        break;
    }
    case SETTING_ACCENT: c->accent = wrap(c->accent, delta, THEME_ACCENT_COUNT); break;
    case SETTING_BACKGROUND: changeBackground(app, delta, now_ns); break;
    case SETTING_BLUR:
        c->background_blur = wrap(c->background_blur, delta, BLUR_COUNT);
        backgroundSetBlur(c->background_blur);
        break;
    case SETTING_CLOCK: c->clock_24h = !c->clock_24h; break;
    case SETTING_WORLD: c->world_globe = !c->world_globe; break;
    case SETTING_SLEEP:
        app->sleep_minutes = settingsSleepStep(app->sleep_minutes, delta);
        app->sleep_until_ns = app->sleep_minutes ? now_ns + (uint64_t)app->sleep_minutes * NS_PER_MINUTE : 0;
        return;  // Only for now: not kept on the card.
    case SETTING_SCREEN_OFF: c->screen_off_minutes = settingsScreenOffStep(c->screen_off_minutes, delta); break;
    case SETTING_START: c->start_view = wrap(c->start_view, delta, START_COUNT); break;
    case SETTING_RESUME: c->resume = !c->resume; break;
    default: return;
    }
    save(app, now_ns);
}

void appActivateSetting(App *app, int row, uint64_t now_ns)
{
    SettingsPage page;
    if (settingOpensPage(row, &page))
        openPage(app, page);
    else if (row == SETTING_ABOUT)
        app->overlay = OVERLAY_ABOUT;
    else
        appChangeSetting(app, row, 1, now_ns);
}

void appSettingValue(const App *app, int row, uint64_t now_ns, char *out, size_t size)
{
    const Config *c = &app->config;
    switch (row) {
    case SETTING_LANGUAGE: snprintf(out, size, "%s", c->language_name[0] ? c->language_name : "English"); break;
    case SETTING_ACCENT: snprintf(out, size, "%s", themeAccentName(c->accent)); break;
    case SETTING_BACKGROUND: snprintf(out, size, "%s", c->background[0] ? c->background : T("None")); break;
    case SETTING_BLUR: snprintf(out, size, "%s", backgroundBlurName(c->background_blur)); break;
    case SETTING_CLOCK: snprintf(out, size, "%s", c->clock_24h ? T("24-hour (21:47)") : T("12-hour (9:47 PM)")); break;
    case SETTING_WORLD: snprintf(out, size, "%s", c->world_globe ? T("Globe") : T("Flat map")); break;
    case SETTING_SLEEP:
        if (!app->sleep_until_ns || now_ns >= app->sleep_until_ns)
            snprintf(out, size, "%s", T("Off"));
        else
            snprintf(out, size, T("Stops in %d min"),
                     (int)((app->sleep_until_ns - now_ns + NS_PER_MINUTE - 1) / NS_PER_MINUTE));
        break;
    case SETTING_SCREEN_OFF:
        if (c->screen_off_minutes)
            snprintf(out, size, T("After %d min playing"), c->screen_off_minutes);
        else
            snprintf(out, size, "%s", T("Never"));
        break;
    case SETTING_START: snprintf(out, size, "%s", settingsStartName(c->start_view)); break;
    case SETTING_RESUME: snprintf(out, size, "%s", c->resume ? T("On") : T("Off")); break;
    case SETTING_ABOUT: snprintf(out, size, "KaiRadio %s", KAIRADIO_VERSION); break;
    default: out[0] = '\0'; break;
    }
}

void appCheckSleep(App *app, uint64_t now_ns)
{
    if (!app->sleep_until_ns || now_ns < app->sleep_until_ns)
        return;
    app->sleep_until_ns = 0;
    app->sleep_minutes = 0;
    radioStop();
    appToast(app, T("Sleep timer: the radio stopped."), now_ns);
}

// The + menu and its exit question, as KaiPlayer's and KaiVideo's: + in the
// menu asks "Exit KaiRadio?", and + there exits.
static void handleMenu(App *app, uint64_t pressed)
{
    int count = app->overlay == OVERLAY_EXIT ? EXIT_COUNT : MENU_COUNT;
    if (pressed & HidNpadButton_Plus) {
        if (app->overlay == OVERLAY_EXIT)
            app->quit = true;
        else
            askExit(app);
        return;
    }
    if (pressed & BUTTONS_UP) app->menu_cursor = wrap(app->menu_cursor, -1, count);
    if (pressed & BUTTONS_DOWN) app->menu_cursor = wrap(app->menu_cursor, 1, count);
    if (pressed & HidNpadButton_B)
        app->overlay = OVERLAY_NONE;
    else if (pressed & HidNpadButton_A)
        appChooseMenuItem(app, app->menu_cursor);
}

// Up or down a row on the page shown, wrapping.
static void moveSettingsCursor(App *app, int delta)
{
    SettingRow rows[SETTINGS_PAGE_MAX_ROWS];
    int count = settingsPageRows((SettingsPage)app->settings_page, rows), at = 0;
    for (int i = 0; i < count; i++)
        if ((int)rows[i] == app->settings_cursor)
            at = i;
    app->settings_cursor = rows[wrap(at, delta, count)];
}

static void handleSettings(App *app, uint64_t pressed, uint64_t now_ns)
{
    if (pressed & HidNpadButton_Plus) {
        app->overlay = OVERLAY_NONE;
        return;
    }
    if (pressed & BUTTONS_UP) moveSettingsCursor(app, -1);
    if (pressed & BUTTONS_DOWN) moveSettingsCursor(app, 1);
    int row = app->settings_cursor;
    SettingsPage page;
    if (settingsRowEditable(row)) {
        if (pressed & BUTTONS_LEFT) appChangeSetting(app, row, -1, now_ns);
        if (pressed & BUTTONS_RIGHT) appChangeSetting(app, row, 1, now_ns);
    } else if ((pressed & BUTTONS_RIGHT) && settingOpensPage(row, &page)) {
        appActivateSetting(app, row, now_ns);
        return;
    }
    if (pressed & HidNpadButton_A)
        appActivateSetting(app, app->settings_cursor, now_ns);
    else if (pressed & HidNpadButton_B)
        appSettingsBack(app);
}

void appHandleOverlay(App *app, uint64_t pressed, uint64_t now_ns)
{
    switch (app->overlay) {
    case OVERLAY_MENU:
    case OVERLAY_EXIT: handleMenu(app, pressed); break;
    case OVERLAY_SETTINGS: handleSettings(app, pressed, now_ns); break;
    case OVERLAY_ABOUT:
        if (pressed & (HidNpadButton_A | HidNpadButton_B)) app->overlay = OVERLAY_SETTINGS;
        if (pressed & HidNpadButton_Plus) app->overlay = OVERLAY_NONE;
        break;
    default: break;
    }
}
