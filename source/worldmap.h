// The World view's map: where each country is on it, and moving between the
// countries that have stations. Pure logic; ui_map.c draws it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "categories.h"

// Where the map is drawn, in the 1280x720 layout, and its size in
// framebuffer pixels (data/world_map.png is made by tools/make_map.py).
#define MAP_X 44
#define MAP_Y 104
#define MAP_W 1191
#define MAP_H 460
#define MAP_FB_W 1786
#define MAP_FB_H 690
// The image is twice that, for 4K; smaller screens get it shrunk.
#define MAP_IMAGE_W (MAP_FB_W * 2)
#define MAP_IMAGE_H (MAP_FB_H * 2)

// A place on the map: 0-65535 across its width and height.
typedef struct {
    uint16_t x, y;
} MapPoint;

typedef struct {
    char code[3];      // ISO 3166-1 alpha-2, capitals.
    MapPoint label;    // Its main light.
    uint16_t first;    // Its points in MAP_POINTS, spread over its land.
    uint16_t count;
} MapCountry;

// map_countries.c (generated), sorted by code.
extern const MapPoint MAP_POINTS[];
extern const MapCountry MAP_COUNTRIES[];
extern const size_t MAP_COUNTRY_COUNT;

// The country with this code (any case), or NULL if the map hasn't it.
const MapCountry *mapFindCountry(const char *code);

// A point in layout coordinates.
void mapToLayout(MapPoint p, float *x, float *y);

// How many of a country's points light up for count stations, and how big
// its main light is (radius, layout pixels).
int mapLightCount(int stations);
float mapGlowRadius(int stations);

// Among the countries of the list that are on the map: the one nearest
// (x, y), or the one next to the country at index from in the direction
// (dx, dy) (-1, 0 or 1 each). -1 if there is none.
int mapNearest(const CategoryList *countries, float x, float y);
int mapStep(const CategoryList *countries, size_t from, int dx, int dy);
