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
// Where it may show: zoomed, it fills the World view, the whole width from
// under the header (64) to the player bar (572), its top and bottom fading
// out; the country's panel is drawn over it.
#define GLOBE_BOX_X 0
#define GLOBE_BOX_Y 64
#define GLOBE_BOX_W 1280
#define GLOBE_BOX_H 508

// A place: degrees east and north.
typedef struct {
    float lon, lat;
} GeoPoint;

// Which way the globe faces (the place in its middle), and how close it is:
// zoom 1 (or 0, unset) is the whole globe, more makes it bigger around its middle.
typedef struct {
    float lon, lat;
    float zoom;
} GlobeView;

// The globe's radius in layout pixels at view's zoom.
float globeRadius(GlobeView view);

// A map point's place (the map is equirectangular, MAP_NORTH to MAP_SOUTH).
GeoPoint globeMapPlace(MapPoint p);
// Where a place shows: layout x, y, and its depth (1 in the middle, 0 at the
// edge, less behind). False if it's on the far side.
bool globeProject(GlobeView view, GeoPoint place, float *x, float *y, float *depth);
// The place under layout point (x, y). False off the globe.
bool globeUnproject(GlobeView view, float x, float y, GeoPoint *out);
// Degrees the globe turns for a finger moving one layout pixel across its middle.
float globeDegreesPerPixel(GlobeView view);
// Turns view by a finger's move (dx, dy layout pixels): the land goes with it.
GlobeView globeDrag(GlobeView view, float dx, float dy);
// A step of the way from view toward target (the short way round), fraction t of it; view keeps its zoom.
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
    int land_h;      // The land's rows it was made for.
    int16_t *row;    // The land's row (of land_h rows), or -1 north of the map, -2 south of it (ice).
    float *lon;      // Degrees east of the middle's longitude.
    uint8_t *shade;  // Light on the sphere, 0-255.
    uint8_t *alpha;  // 255 on the globe, its edge smoothed; the air's glow around it; 0 beyond.
    bool *air;       // A pixel of the glow, not of the globe.
    int x0, y0, x1, y1;  // The part made (columns x0 to x1, rows y0 to y1, not included).
} GlobeTable;

// Makes (or makes again) the table for a size x size picture, a tilt, and land_h rows of land.
bool globeTableBuild(GlobeTable *t, int size, float tilt, int land_h);
// The same, only its part in columns x0 to x1 and rows y0 to y1 (cut to the
// picture): zoomed, only that shows, and it's made again for each tilt.
bool globeTableBuildPart(GlobeTable *t, int size, float tilt, int land_h, int x0, int y0, int x1, int y1);
// True when the table was made for size and tilt, over at least that part.
bool globeTableCovers(const GlobeTable *t, int size, float tilt, int x0, int y0, int x1, int y1);
void globeTableFree(GlobeTable *t);

typedef struct {
    Color ocean, land, ice, air;
    // The night over the land's grid (land_w x land_h, 0 day to 255 night, as
    // sunNightGrid makes it), darkening the globe there; NULL for none.
    const uint8_t *night;
    // The land's colors over the same grid (0xAABBGGRR, alpha unused), in
    // place of land; NULL for land everywhere.
    const uint32_t *terrain;
} GlobeColors;

// Paints the globe facing lon (the table's tilt) into out (size x size,
// 0xAABBGGRR), from land coverage (land_w x land_h, 0-255, equirectangular).
void globeRender(const GlobeTable *t, float lon, const uint8_t *land, int land_w, int land_h, const GlobeColors *c,
                 uint32_t *out);
// The same, only the picture's pixels in columns x0 to x1 and rows y0 to y1
// (not included): the part that shows when the globe is zoomed past its box.
void globeRenderPart(const GlobeTable *t, float lon, const uint8_t *land, int land_w, const GlobeColors *c,
                     uint32_t *out, int x0, int y0, int x1, int y1);
