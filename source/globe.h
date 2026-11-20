// The World view as a globe: the map's land wrapped on a sphere that turns
// with a finger or the stick, the countries' lights on it. Pure logic over
// numbers and pixels: ui_map.c draws it, app.c turns it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "categories.h"
#include "gfx.h"
#include "worldmap.h"

// Where the globe is, in the 1280x720 layout: its middle and radius.
#define GLOBE_CX 772
#define GLOBE_CY 328
#define GLOBE_R 212
// Its picture is square, the globe GLOBE_DISC of its half-width across: the
// rest is the glow of its air around it.
#define GLOBE_DISC 0.9f
#define GLOBE_IMAGE 640   // The picture's pixels across: made each frame, scaled to the screen.
#define GLOBE_TILT_MAX 70.0f  // How far north or south the middle can turn, degrees.

// A place: degrees east and north.
typedef struct {
    float lon, lat;
} GeoPoint;

// Which way the globe faces: the place in its middle.
typedef GeoPoint GlobeView;

// A map point's place (the map is equirectangular, MAP_NORTH to MAP_SOUTH).
GeoPoint globeMapPlace(MapPoint p);
// Where a place shows: layout x, y, and its depth (1 in the middle, 0 at the
// edge, less behind). False if it's on the far side.
bool globeProject(GlobeView view, GeoPoint place, float *x, float *y, float *depth);
// The place under layout point (x, y). False off the globe.
bool globeUnproject(GlobeView view, float x, float y, GeoPoint *out);
// Degrees the globe turns for a finger moving one layout pixel across its middle.
float globeDegreesPerPixel(void);
// Turns view by a finger's move (dx, dy layout pixels): the land goes with it.
GlobeView globeDrag(GlobeView view, float dx, float dy);
// A step of the way from view toward target (the short way round), fraction t of it.
GlobeView globeToward(GlobeView view, GlobeView target, float t);
// Longitude in [-180, 180).
float globeWrapLon(float lon);

// Among the countries of the list on the map, the one whose light shows
// nearest (x, y) on the globe facing view, within reach layout pixels; -1 if none.
int globeNearest(const CategoryList *countries, GlobeView view, float x, float y, float reach);

// For each pixel of the picture: what's there for a tilt (the middle's
// latitude), made again when the tilt changes.
typedef struct {
    int size;
    float tilt;
    int16_t *row;    // The land's row (of land_h rows), or -1 north of the map, -2 south of it (ice).
    float *lon;      // Degrees east of the middle's longitude.
    uint8_t *shade;  // Light on the sphere, 0-255.
    uint8_t *alpha;  // 255 on the globe, its edge smoothed; the air's glow around it; 0 beyond.
    bool *air;       // A pixel of the glow, not of the globe.
} GlobeTable;

// Makes (or makes again) the table for a size x size picture, a tilt, and land_h rows of land.
bool globeTableBuild(GlobeTable *t, int size, float tilt, int land_h);
void globeTableFree(GlobeTable *t);

typedef struct {
    Color ocean, land, ice, air;
} GlobeColors;

// Paints the globe facing lon (the table's tilt) into out (size x size,
// 0xAABBGGRR), from land coverage (land_w x land_h, 0-255, equirectangular).
void globeRender(const GlobeTable *t, float lon, const uint8_t *land, int land_w, int land_h, const GlobeColors *c,
                 uint32_t *out);
