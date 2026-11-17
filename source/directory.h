// The Radio Browser directory (www.radio-browser.info), asked in a
// background thread so the UI never waits on the network. One request runs
// at a time; a new one replaces the one before.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "stations.h"

#define DIRECTORY_LIMIT 150

typedef enum {
    DIR_POPULAR,  // The most played stations.
    DIR_SEARCH,   // By name.
    DIR_COUNTRY,  // By two-letter country code.
    DIR_TAG,      // By genre or other tag.
} DirectoryQuery;

typedef enum {
    DIR_IDLE,
    DIR_LOADING,
    DIR_READY,
    DIR_FAILED,
} DirectoryState;

typedef struct {
    DirectoryState state;
    unsigned generation;  // Changes with each request.
    char error[128];
} DirectoryStatus;

bool directoryInit(void);
void directoryExit(void);

void directoryRequest(DirectoryQuery query, const char *text);
void directoryGetStatus(DirectoryStatus *out);
// Moves the stations of a READY answer to out (the caller frees them). False
// if there is none, or it is for an older generation.
bool directoryTake(unsigned generation, StationList *out);
// Tells the directory that a station was played, as it asks clients to do:
// it ranks stations by these clicks. Sent in the background.
void directoryCountClick(const char *uuid);

// The path and query of a request ("/json/stations/..."). Pure; for tests.
bool directoryPath(DirectoryQuery query, const char *text, char *out, size_t size);
