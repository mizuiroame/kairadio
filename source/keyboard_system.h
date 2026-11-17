// The console's own software keyboard (swkbd), as OunceShell uses it on
// Switch 2. KaiPlayer falls back to its keyboard (keyboard.c) when this one
// can't open.
#pragma once

#include <stddef.h>

typedef enum {
    SYSTEM_KEYBOARD_OK,
    SYSTEM_KEYBOARD_CANCELLED,
    SYSTEM_KEYBOARD_UNAVAILABLE,
} SystemKeyboardResult;

// Shows the Latin keyboard and waits for it. On OK, out holds the text.
SystemKeyboardResult systemKeyboardInput(const char *title, const char *initial, char *out,
                                         size_t size, size_t max_length);

// Shows the number pad (digits only) and waits for it.
SystemKeyboardResult systemKeyboardNumber(const char *title, const char *initial, char *out,
                                          size_t size, size_t max_length);
