// KaiRadio: an internet radio player for Switch 2 homebrew (kaiHEN +
// libounce). Stations come from the Radio Browser directory; streams are
// decoded with FFmpeg and drawn with the GPU.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <switch.h>

#include "app.h"
#include "buttons.h"
#include "directory.h"
#include "font.h"
#include "gfx.h"
#include "input.h"
#include "logos.h"
#include "net.h"
#include "radio.h"
#include "sysinfo.h"
#include "ui.h"

#define CONFIG_FOLDER "sdmc:/config/kairadio"
#define SCREEN_W 1280
#define SCREEN_H 720
#define PANEL_W 1920  // In case the touch panel reports its own resolution.

static uint64_t nowNs(void)
{
    return armTicksToNs(armGetSystemTick());
}

static void readTouch(App *app, uint64_t now)
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
            appToast(app, status.error, now);
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
        appHandleButtons(app, pressed, now);
        readTouch(app, now);
        appUpdate(app, now);
        syncWithRadio(app, &last_state, last_error, sizeof(last_error), now);

        RadioStatus status;
        radioGetStatus(&status);
        sysinfoGet(&app->sys, now);
        gfxBegin();
        uiDraw(app, &status, now);
        gfxEnd();
    }
}

// Shows why KaiRadio can't start, on libnx's text console, until + is
// pressed, and writes it to sdmc:/config/kairadio/error.txt.
static void startupError(const char *message)
{
    FILE *f = fopen(CONFIG_FOLDER "/error.txt", "w");
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
    (void)argc;
    (void)argv;
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
    if (!radioInit()) {
        netExit();
        fontExit();
        gfxExit();
        startupError("KaiRadio could not start the audio output.");
        return 1;
    }
    directoryInit();
    logosInit();
    sysinfoInit();

    static App app;
    appInit(&app, CONFIG_FOLDER);
    appSetView(&app, app.view);

    runLoop(&app);

    appletSetMediaPlaybackState(false);
    radioExit();
    logosExit();
    directoryExit();
    sysinfoExit();
    appFree(&app);
    netExit();
    fontExit();
    gfxExit();
    return 0;
}
