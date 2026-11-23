// Podcasts over the network, in a background thread so the UI never waits:
// Apple's podcast search (one at a time; a newer one replaces it) and
// podcasts' feeds (queued, each read once however often it's asked for).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "podcast.h"

typedef enum {
    POD_SEARCH_IDLE,
    POD_SEARCH_LOADING,
    POD_SEARCH_READY,
    POD_SEARCH_FAILED,
} PodSearchState;

typedef struct {
    PodSearchState state;
    unsigned generation;  // Changes with each search.
    char error[256];      // In the language in use.
} PodSearchStatus;

bool podFetchInit(void);
void podFetchExit(void);

void podFetchSearch(const char *words);
void podFetchSearchStatus(PodSearchStatus *out);
// Moves a READY search's answers to out (the caller frees them). False if
// there are none, or they're for an older search.
bool podFetchTakeSearch(unsigned generation, PodcastResults *out);

// Reads a podcast's feed in the background (once, if asked for again before).
void podFetchFeed(const char *feed);
// One feed read since the last call: its address in url, its episodes in out
// (the caller frees them) or, when it couldn't be read, error says why (in
// the language in use) and out is empty. False when none is waiting.
bool podFetchTakeFeed(char *url, size_t url_size, Feed *out, char *error, size_t error_size);
// Feeds asked for and not taken yet (to say "Updating...").
int podFetchFeedsPending(void);
