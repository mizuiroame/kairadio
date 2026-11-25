// The listening passport: the countries and stations you've listened to,
// kept on the card as passport.txt in KaiRadio's folder, one stamp a line
// ("CC first-utc station-uuid"). Pure logic but for loading and saving.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "stations.h"

#define PASSPORT_MAX 5000  // Stamps kept; past it, new stations still stamp new countries.

// A station heard (or, for one without an id, its country): where it is,
// what it is, and when it was first heard.
typedef struct {
    char code[3];                 // ISO 3166-1 alpha-2, capitals.
    char uuid[STATION_UUID_MAX];  // Radio Browser's id; empty for a country's stamp alone.
    int64_t first;                // UTC seconds.
} PassportStamp;

typedef struct {
    PassportStamp *stamps;  // In the order they were first heard.
    size_t count;
    char folder[256];       // Where passport.txt lives; empty: never saved.
} Passport;

// A country in it: how many of its stations were heard, and since when.
typedef struct {
    char code[3];
    int stations;
    int64_t first;
} PassportCountry;

// Reads text (passport.txt's) into p, which starts empty. Damaged lines are
// left out. False if memory runs out.
bool passportParse(Passport *p, const char *text);
// p as passport.txt's text; NULL if memory runs out. Free it with free().
char *passportToText(const Passport *p);
// Stamps station (its country, and it) heard at utc. True if that's new.
// A station with no country can't be stamped.
bool passportStamp(Passport *p, const Station *station, int64_t utc);
// True if a station of the country with code (any case) was heard.
bool passportHasCountry(const Passport *p, const char *code);
// The countries, in the order first heard: up to max of them into out. Returns how many there are.
size_t passportCountries(const Passport *p, PassportCountry *out, size_t max);
// How many stations were heard.
size_t passportStations(const Passport *p);

// Loads folder's passport.txt (missing or damaged: empty), and keeps folder for saving.
void passportLoad(Passport *p, const char *folder);
// Stamps station, and saves when it's new. True if it's the first of its country.
bool passportPlayed(Passport *p, const Station *station, int64_t utc);
void passportFree(Passport *p);
