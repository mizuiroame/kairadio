// Stations as the Radio Browser directory describes them, read from its JSON
// answers. Favorites are saved in the same format. Pure logic.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "link.h"

#define STATION_UUID_MAX 40
#define STATION_NAME_MAX 128
#define STATION_TEXT_MAX 96

typedef struct {
    char uuid[STATION_UUID_MAX];
    char name[STATION_NAME_MAX];
    char url[LINK_URL_MAX];      // The stream ("url_resolved", else "url").
    char favicon[LINK_URL_MAX];  // The station's logo; may be empty.
    char country[STATION_TEXT_MAX];
    char countrycode[4];
    char language[STATION_TEXT_MAX];
    char tags[STATION_TEXT_MAX];
    char codec[16];              // "MP3", "AAC", "AAC+", "OGG"...; may be empty.
    int bitrate;                 // kbit/s; 0 if unknown.
    bool hls;
} Station;

typedef struct {
    Station *items;
    size_t count;
} StationList;

// Reads a JSON array of stations. Entries without a name or a usable
// http(s) address are left out. False if the text isn't a JSON array or
// memory runs out; out is then empty.
bool stationsParse(const char *json, StationList *out);

// The stations as a JSON array that stationsParse reads back. NULL if
// memory runs out; free it with free().
char *stationsToJson(const StationList *list);

void stationListFree(StationList *list);
// Adds a copy of station at the end. False if memory runs out.
bool stationListAppend(StationList *list, const Station *station);
// Removes the item at index (keeps the order).
void stationListRemove(StationList *list, size_t index);
// The index of the station with uuid (or, without one, the same address),
// or -1.
long stationListFind(const StationList *list, const Station *station);

// "Country · AAC 128 kbps · jazz, swing": a short line under the name.
void stationDetails(const Station *station, char *out, size_t size);
