// KaiRadio: an internet radio player for Switch 2 homebrew (kaiHEN +
// libounce). Stations come from the Radio Browser directory; streams are
// decoded with FFmpeg and drawn with the GPU.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <switch.h>

#include "app.h"
#include "approot.h"
#include "background.h"
#include "buttons.h"
#include "directory.h"
#include "font.h"
#include "gfx.h"
#include "input.h"
#include "lang.h"
#include "logos.h"
#include "net.h"
#include "radio.h"
#include "screen.h"
#include "sysinfo.h"
#include "ui.h"
#include "weatherfetch.h"

#define OLD_CONFIG_FOLDER "sdmc:/config/kairadio"  // Before files went next to the NRO.
#define SCREEN_W 1280
#define SCREEN_H 720
#define PANEL_W 1920  // In case the touch panel reports its own resolution.
#define NS_PER_MINUTE 60000000000ull

static uint64_t nowNs(void)
{
    return armTicksToNs(armGetSystemTick());
}

// Passes the touch screen to the app; true while it's touched.
static bool readTouch(App *app, uint64_t now)
{
    static bool full_hd;  // Seen coordinates beyond 1280x720: scale them down.
    HidTouchScreenState state = {0};
    bool touching = hidGetTouchScreenStates(&state, 1) > 0 && state.count > 0;
    int x = 0, y = 0;
    if (touching) {
        x = (int)state.touches[0].x;
        y = (int)state.touches[0].y;
        if (x >= SCREEN_W || y >= SCREEN_H)
            full_hd = true;
        if (full_hd) {
            x = x * SCREEN_W / PANEL_W;
            y = y * SCREEN_W / PANEL_W;
        }
    }
    appHandleTouch(app, touching, x, y, now);
    appGlobeTick(app, now);
    return touching;
}

// Screen off: any button or touch brings it back, and that input does
// nothing else. While a station plays and nothing is touched, it turns off
// after the minutes chosen in Settings. Returns true while the screen is off.
static bool manageScreen(App *app, uint64_t pressed, bool touching, uint64_t now)
{
    static bool wake_touch;  // The touch that woke it: ignored until let go.
    bool input = pressed != 0 || touching;
    if (app->screen_off) {
        if (input) {
            screenSetOn(true);
            app->screen_off = false;
            wake_touch = touching;
            app->last_input_ns = now;
        }
        return true;
    }
    if (wake_touch) {
        wake_touch = touching;
        return touching;
    }
    if (input || !app->last_input_ns || app->overlay != OVERLAY_NONE)
        app->last_input_ns = now;
    RadioStatus status;
    radioGetStatus(&status);
    uint64_t idle_limit = (uint64_t)app->config.screen_off_minutes * NS_PER_MINUTE;
    if (idle_limit && status.state == RADIO_PLAYING && now - app->last_input_ns >= idle_limit) {
        app->screen_off = true;
        screenSetOn(false);
        return true;
    }
    return false;
}

// Keeps the console awake while a station plays, and shows playback errors.
static void syncWithRadio(App *app, RadioState *last_state, char *last_error, size_t size, uint64_t now)
{
    RadioStatus status;
    radioGetStatus(&status);
    bool playing = status.state != RADIO_STOPPED && status.state != RADIO_FAILED;
    bool was_playing = *last_state != RADIO_STOPPED && *last_state != RADIO_FAILED;
    if (playing != was_playing)
        appletSetMediaPlaybackState(playing);
    *last_state = status.state;
    if (strcmp(status.error, last_error) != 0) {
        snprintf(last_error, size, "%s", status.error);
        if (status.error[0])
            appNotify(app, NOTICE_ERROR, T(status.error), now);
    }
}

static void runLoop(App *app)
{
    PadState pad;
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
    InputRepeat repeat;
    inputRepeatInit(&repeat, BUTTONS_REPEATING);
    RadioState last_state = RADIO_STOPPED;
    char last_error[160] = "";

    while (!app->quit && appletMainLoop()) {
        padUpdate(&pad);
        uint64_t now = nowNs();
        uint64_t pressed = inputRepeatUpdate(&repeat, padGetButtonsDown(&pad), padGetButtons(&pad), now);
        HidTouchScreenState touch = {0};
        bool touching = hidGetTouchScreenStates(&touch, 1) > 0 && touch.count > 0;
        if (!manageScreen(app, pressed, touching, now)) {
            appHandleButtons(app, pressed, now);
            readTouch(app, now);
        }
        appUpdate(app, now);
        appCheckSleep(app, now);
        syncWithRadio(app, &last_state, last_error, sizeof(last_error), now);
        if (app->screen_off) {
            // The backlight is off, or (docked) the picture is only black; the audio goes on.
            gfxBegin();
            gfxClear(GFX_RGB(0, 0, 0));
            gfxEnd();
            continue;
        }

        RadioStatus status;
        radioGetStatus(&status);
        sysinfoGet(&app->sys, now);
        gfxBegin();
        uiDraw(app, &status, now);
        gfxEnd();
    }
}

// Shows why KaiRadio can't start, on libnx's text console, until + is
// pressed, and writes it to error.txt in KaiRadio's folder.
// KaiRadio's own files, next to its NRO (approot.h).
static char g_folder[APPROOT_PATH_MAX];

static void startupError(const char *message)
{
    char path[APPROOT_PATH_MAX + 16];
    snprintf(path, sizeof(path), "%s/error.txt", g_folder);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%s\n", message);
        fclose(f);
    }
    consoleInit(NULL);
    printf("\n  %s\n\n  Press + to exit.\n", message);
    PadState pad;
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
}

int main(int argc, char **argv)
{
    // Files next to the NRO; the first time, what the old folder had
    // (favorites, recent, settings, backgrounds) is copied there.
    bool next_to_nro = appRootFolder(argc > 0 ? argv[0] : NULL, "kairadio", g_folder, sizeof(g_folder));
    appRootMakeFolder(g_folder);
    if (next_to_nro)
        appRootMigrate(OLD_CONFIG_FOLDER, g_folder);
    srand((unsigned)time(NULL));
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    if (!gfxInit()) {
        startupError("KaiRadio could not start drawing (the GPU or its window).");
        return 1;
    }
    if (!fontInit()) {
        gfxExit();
        startupError("KaiRadio could not load its fonts.");
        return 1;
    }
    if (!netInit()) {
        fontExit();
        gfxExit();
        startupError("KaiRadio could not start the network.");
        return 1;
    }
    if (!radioInit(g_folder)) {
        netExit();
        fontExit();
        gfxExit();
        startupError("KaiRadio could not start the audio output.");
        return 1;
    }
    directoryInit();
    weatherFetchStart();
    logosInit();
    sysinfoInit();

    screenInit();
    static App app;
    appInit(&app, g_folder);
    appStartLanguages(&app);  // Before anything is said on screen.
    appStartUpdates(&app, argc > 0 ? argv[0] : NULL);
    appSetView(&app, app.view);
    if (app.config.background[0] &&
        !backgroundLoad(g_folder, app.config.background, app.config.background_blur))
        appNotify(&app, NOTICE_ERROR, T("The background image couldn't be opened."), nowNs());
    // Settings > Resume: the last station played, with Recent for previous and next.
    if (app.config.resume && app.library.recent.count > 0)
        appPlayFrom(&app, &app.library.recent, 0);

    runLoop(&app);
    appStopUpdates();  // A download stops; its part is removed.

    appletSetMediaPlaybackState(false);
    screenExit();
    radioExit();
    logosExit();
    weatherFetchStop();
    directoryExit();
    sysinfoExit();
    appFree(&app);
    netExit();
    fontExit();
    gfxExit();
    return 0;
}
