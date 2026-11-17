// What the preview's stand-ins record (stubs.c).
#pragma once

#include "directory.h"
#include "radio.h"

extern int g_stub_calls;            // Calls to play, stop, the directory and the keyboard.
extern RadioStatus g_stub_radio;    // What radioGetStatus gives.
extern DirectoryStatus g_stub_directory;
