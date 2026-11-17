// The Radio Browser directory (www.radio-browser.info), asked in a
// background thread so the UI never waits on the network. One request runs
// at a time; a new one replaces the one before.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "categories.h"
#include "stations.h"

#define DIRECTORY_LIMIT 150
#define CATEGORY_LIMIT 300

typedef enum {
    DIR_POPULAR,  // The most played stations.
    DIR_SEARCH,   // By name.
    DIR_COUNTRY,  // By two-letter country code.
    DIR_TAG,      // By genre or other tag.
    DIR_LANGUAGE, // By language name ("spanish").
    // Lists of categories, most stations first.
    DIR_GENRES,
    DIR_COUNTRIES,
    DIR_LANGUAGES,
} DirectoryQuery;

// True for the queries whose answer is categories rather than stations.
bool directoryListsCategories(DirectoryQuery query);

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
// The same for a categories answer.
bool directoryTakeCategories(unsigned generation, CategoryList *out);
// Tells the directory that a station was played, as it asks clients to do:
// it ranks stations by these clicks. Sent in the background.
void directoryCountClick(const char *uuid);

// The path and query of a request ("/json/stations/..."). Pure; for tests.
bool directoryPath(DirectoryQuery query, const char *text, char *out, size_t size);
