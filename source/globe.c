#include "globe.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265f
#define RAD (PI_F / 180.0f)
#define AIR_WIDTH 0.075f       // The glow's width, a share of the globe's radius.
#define AIR_ALPHA 150.0f       // How bright it starts at the globe's edge.
#define SHADE_MIN 0.28f        // The far side of the light still shows this much.
#define LIGHT_X -0.42f         // The light comes from the upper left, toward the viewer.
#define LIGHT_Y 0.52f
#define LIGHT_Z 0.74f
#define NIGHT_DIM 0.55f        // How much of the light the night takes: the land still reads in it.
#define DAY_GAIN 1.12f         // and how much the day adds to it.

GeoPoint globeMapPlace(MapPoint p)
{
    GeoPoint g = {p.x / 65535.0f * 360.0f - 180.0f, MAP_NORTH - p.y / 65535.0f * (MAP_NORTH - MAP_SOUTH)};
    return g;
}

float globeWrapLon(float lon)
{
    lon = fmodf(lon + 180.0f, 360.0f);
    if (lon < 0)
        lon += 360.0f;
    return lon - 180.0f;
}

float globeRadius(GlobeView view)
{
    return GLOBE_R * (view.zoom > 1.0f ? view.zoom : 1.0f);
}

bool globeProject(GlobeView view, GeoPoint place, float *x, float *y, float *depth)
{
    float lat = place.lat * RAD, dlon = (place.lon - view.lon) * RAD, tilt = view.lat * RAD;
    float gx = cosf(lat) * sinf(dlon), gy = sinf(lat), gz = cosf(lat) * cosf(dlon);
    float vy = gy * cosf(tilt) - gz * sinf(tilt), vz = gy * sinf(tilt) + gz * cosf(tilt);
    float r = globeRadius(view);
    *x = GLOBE_CX + gx * r;
    *y = GLOBE_CY - vy * r;
    *depth = vz;
    return vz > 0.0f;
}

bool globeUnproject(GlobeView view, float x, float y, GeoPoint *out)
{
    float r = globeRadius(view);
    float nx = (x - GLOBE_CX) / r, ny = (GLOBE_CY - y) / r, r2 = nx * nx + ny * ny;
    if (r2 > 1.0f)
        return false;
    float nz = sqrtf(1.0f - r2), tilt = view.lat * RAD;
    float gy = ny * cosf(tilt) + nz * sinf(tilt), gz = -ny * sinf(tilt) + nz * cosf(tilt);
    out->lat = asinf(gy > 1.0f ? 1.0f : gy < -1.0f ? -1.0f : gy) / RAD;
    out->lon = globeWrapLon(view.lon + atan2f(nx, gz) / RAD);
    return true;
}

float globeDegreesPerPixel(GlobeView view)
{
    return 1.0f / (globeRadius(view) * RAD);
}

GlobeView globeDrag(GlobeView view, float dx, float dy)
{
    float step = globeDegreesPerPixel(view);  // Closer, a finger turns it less: the land stays under it.
    view.lon = globeWrapLon(view.lon - dx * step);
    view.lat += dy * step;
    view.lat = view.lat > GLOBE_TILT_MAX ? GLOBE_TILT_MAX : view.lat < -GLOBE_TILT_MAX ? -GLOBE_TILT_MAX : view.lat;
    return view;
}

GlobeView globeToward(GlobeView view, GlobeView target, float t)
{
    float dlon = globeWrapLon(target.lon - view.lon);  // The short way round.
    view.lon = globeWrapLon(view.lon + dlon * t);
    view.lat += (target.lat - view.lat) * t;
    return view;
}

int globeNearest(const CategoryList *countries, GlobeView view, float x, float y, float reach)
{
    int best = -1;
    float best_d = reach * reach;
    for (size_t i = 0; i < countries->count; i++) {
        const MapCountry *c = mapFindCountry(countries->items[i].code);
        float px, py, depth;
        if (!c || !globeProject(view, globeMapPlace(c->label), &px, &py, &depth) || depth < 0.15f)
            continue;  // Behind, or too near the edge to aim at.
        float d = (px - x) * (px - x) + (py - y) * (py - y);
        if (d < best_d) {
            best_d = d;
            best = (int)i;
        }
    }
    return best;
}

void globeTableFree(GlobeTable *t)
{
    free(t->row);
    free(t->lon);
    free(t->shade);
    free(t->alpha);
    free(t->air);
    memset(t, 0, sizeof(*t));
}

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

// One pixel of the table: (nx, ny) in units of the globe's radius.
static void tablePixel(GlobeTable *t, size_t i, float nx, float ny, float pixel, float tilt, int land_h)
{
    float d = sqrtf(nx * nx + ny * ny);
    t->air[i] = d > 1.0f;
    if (d > 1.0f) {  // The air: a glow fading out from the edge.
        float f = 1.0f - (d - 1.0f) / AIR_WIDTH;
        t->alpha[i] = f > 0.0f ? (uint8_t)(AIR_ALPHA * f * f) : 0;
        return;
    }
    t->alpha[i] = (uint8_t)(255.0f * clamp01((1.0f - d) / pixel + 0.5f));  // A smooth edge.
    float nz = sqrtf(1.0f - (d < 1.0f ? d * d : 1.0f));
    float gy = ny * cosf(tilt) + nz * sinf(tilt), gz = -ny * sinf(tilt) + nz * cosf(tilt);
    float lat = asinf(gy > 1.0f ? 1.0f : gy < -1.0f ? -1.0f : gy) / RAD;
    t->lon[i] = atan2f(nx, gz) / RAD;
    if (lat > MAP_NORTH)
        t->row[i] = -1;
    else if (lat < MAP_SOUTH)
        t->row[i] = -2;
    else {
        int row = (int)((MAP_NORTH - lat) / (MAP_NORTH - MAP_SOUTH) * land_h);
        t->row[i] = (int16_t)(row >= land_h ? land_h - 1 : row);
    }
    float lit = nx * LIGHT_X + ny * LIGHT_Y + nz * LIGHT_Z;
    float rim = 0.12f * (1.0f - nz);  // A little light along the edge, as the air scatters it.
    t->shade[i] = (uint8_t)(255.0f * clamp01(SHADE_MIN + (1.0f - SHADE_MIN) * clamp01(lit) + rim));
}

bool globeTableBuild(GlobeTable *t, int size, float tilt, int land_h)
{
    return globeTableBuildPart(t, size, tilt, land_h, 0, 0, size, size);
}

bool globeTableCovers(const GlobeTable *t, int size, float tilt, int x0, int y0, int x1, int y1)
{
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > size ? size : x1;
    y1 = y1 > size ? size : y1;
    return t->size == size && t->tilt == tilt && t->x0 <= x0 && t->y0 <= y0 && t->x1 >= x1 && t->y1 >= y1;
}

bool globeTableBuildPart(GlobeTable *t, int size, float tilt, int land_h, int x0, int y0, int x1, int y1)
{
    if (t->size != size) {
        globeTableFree(t);
        size_t n = (size_t)size * size;
        t->row = malloc(n * sizeof(*t->row));
        t->lon = malloc(n * sizeof(*t->lon));
        t->shade = malloc(n);
        t->alpha = malloc(n);
        t->air = malloc(n * sizeof(*t->air));
        if (!t->row || !t->lon || !t->shade || !t->alpha || !t->air) {
            globeTableFree(t);
            return false;
        }
        t->size = size;
    }
    t->tilt = tilt;
    t->land_h = land_h;
    t->x0 = x0 < 0 ? 0 : x0;
    t->y0 = y0 < 0 ? 0 : y0;
    t->x1 = x1 > size ? size : x1 < t->x0 ? t->x0 : x1;
    t->y1 = y1 > size ? size : y1 < t->y0 ? t->y0 : y1;
    float half = size / 2.0f, radius = half * GLOBE_DISC, pixel = 1.0f / radius, rt = tilt * RAD;
    for (int y = t->y0; y < t->y1; y++)
        for (int x = t->x0; x < t->x1; x++)
            tablePixel(t, (size_t)y * size + x, (x + 0.5f - half) / radius, (half - y - 0.5f) / radius, pixel, rt,
                       land_h);
    return true;
}

static uint32_t scaled(Color c, unsigned shade, unsigned alpha)
{
    unsigned r = (c & 0xFF) * shade / 255, g = ((c >> 8) & 0xFF) * shade / 255, b = ((c >> 16) & 0xFF) * shade / 255;
    r = r > 255 ? 255 : r;  // Shade past 255: brighter, kept in range.
    g = g > 255 ? 255 : g;
    b = b > 255 ? 255 : b;
    return r | g << 8 | b << 16 | (uint32_t)alpha << 24;
}

void globeRender(const GlobeTable *t, float lon, const uint8_t *land, int land_w, int land_h, const GlobeColors *c,
                 uint32_t *out)
{
    (void)land_h;
    globeRenderPart(t, lon, land, land_w, c, out, 0, 0, t->size, t->size);
}

// One pixel of the picture: nothing, the air's glow, ice, or ocean and land, shaded.
static uint32_t picturePixel(const GlobeTable *t, int i, float lon, const uint8_t *land, int land_w, float cols,
                             const GlobeColors *c)
{
    unsigned alpha = t->alpha[i];
    if (!alpha)
        return 0;
    if (t->air[i])
        return (c->air & 0x00FFFFFFu) | (uint32_t)alpha << 24;
    Color here;
    int col = (int)((globeWrapLon(lon + t->lon[i]) + 180.0f) * cols);
    col = col < 0 ? 0 : col >= land_w ? land_w - 1 : col;
    int row = t->row[i];
    if (row == -1) {
        here = c->ocean;
        row = 0;  // North of the map: its night as the map's top row.
    } else if (row == -2) {
        here = c->ice;
        row = t->land_h - 1;
    } else {
        size_t cell = (size_t)row * land_w + col;
        here = gfxMix(c->ocean, c->terrain ? c->terrain[cell] : c->land, land[cell]);
    }
    unsigned shade = t->shade[i];
    if (c->night) {  // From DAY_GAIN of the light in the day to 1 - NIGHT_DIM of it at night.
        float dark = c->night[(size_t)row * land_w + col] / 255.0f;
        shade = (unsigned)(shade * (DAY_GAIN - (DAY_GAIN - 1.0f + NIGHT_DIM) * dark));
    }
    return scaled(here, shade, alpha);
}

void globeRenderPart(const GlobeTable *t, float lon, const uint8_t *land, int land_w, const GlobeColors *c,
                     uint32_t *out, int x0, int y0, int x1, int y1)
{
    float cols = land_w / 360.0f;
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > t->size ? t->size : x1;
    y1 = y1 > t->size ? t->size : y1;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            out[y * t->size + x] = picturePixel(t, y * t->size + x, lon, land, land_w, cols, c);
}
