// The stations kept on the card: favorites, and the ones played lately.
// Both are JSON files in KaiRadio's folder, in the directory's format.
#pragma once

#include <stdbool.h>

#include "stations.h"

#define RECENT_MAX 30

typedef struct {
    StationList favorites;
    StationList recent;  // Most recent first.
    char folder[256];    // Where the files live.
} Library;

// Loads both lists from folder (created when saving). Missing or damaged
// files give empty lists.
void libraryLoad(Library *lib, const char *folder);
void libraryFree(Library *lib);

bool libraryIsFavorite(const Library *lib, const Station *station);
// Adds or removes a favorite and saves. Returns whether it is one now.
bool libraryToggleFavorite(Library *lib, const Station *station);
// Moves station to the top of the recent list and saves.
void libraryPlayed(Library *lib, const Station *station);
