// The + menu, the Settings page and About: their buttons, and what each
// setting does when it changes.
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "buttons.h"
#include "radio.h"
#include "settings.h"
#include "theme.h"

#define NS_PER_MINUTE 60000000000ull

void appOpenMenu(App *app)
{
    app->overlay = OVERLAY_MENU;
    app->menu_cursor = MENU_SETTINGS;
}

void appOpenSettings(App *app)
{
    app->overlay = OVERLAY_SETTINGS;
    app->settings_cursor = SETTING_ACCENT;
    // The images there are now: a new one shows the next time Settings opens.
    app->background_count = backgroundList(app->config_folder, app->backgrounds, BACKGROUND_MAX_IMAGES);
}

static void save(App *app, uint64_t now_ns)
{
    if (!configSave(&app->config, app->config_folder))
        appToast(app, "The settings couldn't be saved on the card.", now_ns);
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
        snprintf(message, sizeof(message), "Put JPEG or PNG images in %s/Backgrounds first.", app->config_folder);
        appToast(app, message, now_ns);
        return;
    }
    int index = wrap((int)backgroundIndex(app), delta, choices);
    const char *name = index == 0 ? "" : app->backgrounds[index - 1];
    snprintf(app->config.background, sizeof(app->config.background), "%s", name);
    if (!backgroundLoad(app->config_folder, name, app->config.background_blur))
        appToast(app, "That image couldn't be opened (too big, or not a JPEG or PNG).", now_ns);
}

void appChangeSetting(App *app, int row, int delta, uint64_t now_ns)
{
    Config *c = &app->config;
    switch (row) {
    case SETTING_ACCENT: c->accent = wrap(c->accent, delta, THEME_ACCENT_COUNT); break;
    case SETTING_BACKGROUND: changeBackground(app, delta, now_ns); break;
    case SETTING_BLUR:
        c->background_blur = wrap(c->background_blur, delta, BLUR_COUNT);
        backgroundSetBlur(c->background_blur);
        break;
    case SETTING_CLOCK: c->clock_24h = !c->clock_24h; break;
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
    if (row == SETTING_ABOUT)
        app->overlay = OVERLAY_ABOUT;
    else
        appChangeSetting(app, row, 1, now_ns);
}

void appSettingValue(const App *app, int row, uint64_t now_ns, char *out, size_t size)
{
    const Config *c = &app->config;
    switch (row) {
    case SETTING_ACCENT: snprintf(out, size, "%s", themeAccentName(c->accent)); break;
    case SETTING_BACKGROUND: snprintf(out, size, "%s", c->background[0] ? c->background : "None"); break;
    case SETTING_BLUR: snprintf(out, size, "%s", backgroundBlurName(c->background_blur)); break;
    case SETTING_CLOCK: snprintf(out, size, "%s", c->clock_24h ? "24-hour (21:47)" : "12-hour (9:47 PM)"); break;
    case SETTING_SLEEP:
        if (!app->sleep_until_ns || now_ns >= app->sleep_until_ns)
            snprintf(out, size, "Off");
        else
            snprintf(out, size, "Stops in %d min",
                     (int)((app->sleep_until_ns - now_ns + NS_PER_MINUTE - 1) / NS_PER_MINUTE));
        break;
    case SETTING_SCREEN_OFF:
        if (c->screen_off_minutes)
            snprintf(out, size, "After %d min playing", c->screen_off_minutes);
        else
            snprintf(out, size, "Never");
        break;
    case SETTING_START: snprintf(out, size, "%s", settingsStartName(c->start_view)); break;
    case SETTING_RESUME: snprintf(out, size, "%s", c->resume ? "On" : "Off"); break;
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
    appToast(app, "Sleep timer: the radio stopped.", now_ns);
}

static void handleMenu(App *app, uint64_t pressed)
{
    if (pressed & BUTTONS_UP) app->menu_cursor = wrap(app->menu_cursor, -1, MENU_COUNT);
    if (pressed & BUTTONS_DOWN) app->menu_cursor = wrap(app->menu_cursor, 1, MENU_COUNT);
    if (pressed & HidNpadButton_Plus) app->quit = true;  // + twice: out, as before.
    if (pressed & HidNpadButton_B) app->overlay = OVERLAY_NONE;
    if (pressed & HidNpadButton_A) {
        if (app->menu_cursor == MENU_EXIT)
            app->quit = true;
        else
            appOpenSettings(app);
    }
}

static void handleSettings(App *app, uint64_t pressed, uint64_t now_ns)
{
    if (pressed & BUTTONS_UP) app->settings_cursor = wrap(app->settings_cursor, -1, SETTING_COUNT);
    if (pressed & BUTTONS_DOWN) app->settings_cursor = wrap(app->settings_cursor, 1, SETTING_COUNT);
    if (settingsRowEditable(app->settings_cursor)) {
        if (pressed & BUTTONS_LEFT) appChangeSetting(app, app->settings_cursor, -1, now_ns);
        if (pressed & BUTTONS_RIGHT) appChangeSetting(app, app->settings_cursor, 1, now_ns);
    }
    if (pressed & HidNpadButton_A) appActivateSetting(app, app->settings_cursor, now_ns);
    if (pressed & (HidNpadButton_B | HidNpadButton_Plus)) app->overlay = OVERLAY_NONE;
}

void appHandleOverlay(App *app, uint64_t pressed, uint64_t now_ns)
{
    switch (app->overlay) {
    case OVERLAY_MENU: handleMenu(app, pressed); break;
    case OVERLAY_SETTINGS: handleSettings(app, pressed, now_ns); break;
    case OVERLAY_ABOUT:
        if (pressed & (HidNpadButton_A | HidNpadButton_B)) app->overlay = OVERLAY_SETTINGS;
        if (pressed & HidNpadButton_Plus) app->overlay = OVERLAY_NONE;
        break;
    default: break;
    }
}
