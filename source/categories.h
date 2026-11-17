// Genres (tags), countries and languages, as the directory lists them, with
// how many stations each has. Pure logic.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define CATEGORY_NAME_MAX 64

typedef struct {
    char name[CATEGORY_NAME_MAX];  // As the directory spells it ("jazz", "spanish", "Mexico").
    char code[8];                  // A country's ISO 3166-1 code or a language's ISO 639 code; may be empty.
    int count;                     // Stations.
} Category;

typedef struct {
    Category *items;
    size_t count;
} CategoryList;

// Reads a JSON array of {"name", "stationcount", "iso_3166_1" or "iso_639"}.
// Entries without a name or stations are left out, as are names too long
// to search by. False if it isn't a JSON array or memory runs out.
bool categoriesParse(const char *json, CategoryList *out);
void categoryListFree(CategoryList *list);

// The name to show: the first letter of each word in capitals for the
// lowercase tags and languages ("hip hop" -> "Hip Hop"). Countries are kept.
void categoryLabel(const Category *c, char *out, size_t size);
