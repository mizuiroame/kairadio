// Host stand-ins for the parts of KaiRadio that need the console or the
// network: playback, the directory, logos and the keyboard. They record
// what the app asks of them, so the preview can check that taps act.
#include <stdio.h>
#include <string.h>

#include "directory.h"
#include "keyboard_system.h"
#include "logo.h"
#include "logos.h"
#include "radio.h"
#include "stubs.h"

int g_stub_calls;
RadioStatus g_stub_radio;
DirectoryStatus g_stub_directory;

void radioPlay(const Station *station)
{
    g_stub_calls++;
    g_stub_radio.has_station = true;
    g_stub_radio.station = *station;
    g_stub_radio.state = RADIO_CONNECTING;
}

void radioStop(void)
{
    g_stub_calls++;
    g_stub_radio.state = RADIO_STOPPED;
}

void radioGetStatus(RadioStatus *out)
{
    *out = g_stub_radio;
}

const char *radioStateLabel(RadioState state)
{
    switch (state) {
    case RADIO_CONNECTING: return "Connecting\xE2\x80\xA6";
    case RADIO_BUFFERING: return "Buffering\xE2\x80\xA6";
    case RADIO_PLAYING: return "Live";
    case RADIO_RECONNECTING: return "Reconnecting\xE2\x80\xA6";
    case RADIO_FAILED: return "Can't play";
    default: return "Stopped";
    }
}

void directoryRequest(DirectoryQuery query, const char *text)
{
    (void)query, (void)text;
    g_stub_calls++;
    g_stub_directory.generation++;
    g_stub_directory.state = DIR_LOADING;
}

void directoryGetStatus(DirectoryStatus *out)
{
    *out = g_stub_directory;
}

bool directoryTake(unsigned generation, StationList *out)
{
    (void)generation, (void)out;
    return false;
}

bool directoryTakeCategories(unsigned generation, CategoryList *out)
{
    (void)generation, (void)out;
    return false;
}

void directoryCountClick(const char *uuid)
{
    (void)uuid;
}

SystemKeyboardResult systemKeyboardInput(const char *title, const char *initial, char *out, size_t size,
                                         size_t max_length)
{
    (void)title, (void)initial, (void)max_length;
    g_stub_calls++;
    snprintf(out, size, "jazz");
    return SYSTEM_KEYBOARD_OK;
}

// Logos: a drawn pattern for addresses with "logo" in them, none otherwise
// (the tile with the station's initial).
static uint32_t g_logo[LOGO_SIZE * LOGO_SIZE];

const uint32_t *logosGet(const char *url)
{
    if (!url || !strstr(url, "logo"))
        return NULL;
    if (!g_logo[0]) {
        for (int y = 0; y < LOGO_SIZE; y++)
            for (int x = 0; x < LOGO_SIZE; x++) {
                int dx = x - LOGO_SIZE / 2, dy = y - LOGO_SIZE / 2;
                bool ring = dx * dx + dy * dy < (LOGO_SIZE * 2 / 5) * (LOGO_SIZE * 2 / 5) &&
                            dx * dx + dy * dy > (LOGO_SIZE / 5) * (LOGO_SIZE / 5);
                g_logo[y * LOGO_SIZE + x] = ring ? 0xFF2050E0u : 0xFFFFFFFFu;
            }
    }
    return g_logo;
}

void logosEndFrame(void)
{
}
