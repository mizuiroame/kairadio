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
#define SIZE_SLOP 0.01f        // A picture's size this share of a step off one is on it.

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

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
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

bool globeTableCovers(const GlobeTable *t, int size, float tilt, int land_h, int x0, int y0, int x1, int y1)
{
    x0 = clampi(x0, 0, size);
    y0 = clampi(y0, 0, size);
    x1 = clampi(x1, x0, size);
    y1 = clampi(y1, y0, size);
    return t->size == size && t->tilt == tilt && t->land_h == land_h && t->made == t->y1 - t->y0 && t->x0 <= x0 &&
           t->y0 <= y0 && t->x1 >= x1 && t->y1 >= y1;
}

bool globeTableStart(GlobeTable *t, int size, float tilt, int land_h, int x0, int y0, int x1, int y1)
{
    x0 = clampi(x0, 0, size);
    y0 = clampi(y0, 0, size);
    x1 = clampi(x1, x0, size);
    y1 = clampi(y1, y0, size);
    size_t n = (size_t)(x1 - x0) * (size_t)(y1 - y0);
    if (n > t->cap) {
        globeTableFree(t);
        t->row = malloc(n * sizeof(*t->row));
        t->lon = malloc(n * sizeof(*t->lon));
        t->shade = malloc(n);
        t->alpha = malloc(n);
        t->air = malloc(n * sizeof(*t->air));
        if (!t->row || !t->lon || !t->shade || !t->alpha || !t->air) {
            globeTableFree(t);
            return false;
        }
        t->cap = n;
    }
    t->size = size;
    t->tilt = tilt;
    t->land_h = land_h;
    t->x0 = x0;
    t->y0 = y0;
    t->x1 = x1;
    t->y1 = y1;
    t->made = 0;
    return true;
}

bool globeTableMake(GlobeTable *t, long pixels)
{
    int w = t->x1 - t->x0, rows = t->y1 - t->y0;
    float half = t->size / 2.0f, radius = half * GLOBE_DISC, pixel = 1.0f / radius, rt = t->tilt * RAD;
    for (long done = 0; t->made < rows && (done == 0 || done < pixels); done += w) {
        int y = t->y0 + t->made;
        for (int x = t->x0; x < t->x1; x++)
            tablePixel(t, globeTableAt(t, x, y), (x + 0.5f - half) / radius, (half - y - 0.5f) / radius, pixel, rt,
                       t->land_h);
        t->made++;
    }
    return t->made == rows;
}

bool globeTableBuildPart(GlobeTable *t, int size, float tilt, int land_h, int x0, int y0, int x1, int y1)
{
    if (!globeTableStart(t, size, tilt, land_h, x0, y0, x1, y1))
        return false;
    while (!globeTableMake(t, 1L << 30))
        ;
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

void globeRender(const GlobeTable *t, float lon, const uint32_t *land, int land_w, const GlobeColors *c,
                 uint32_t *out)
{
    globeRenderPart(t, lon, land, land_w, c, out, t->x1 - t->x0, t->x0, t->y0, t->x1, t->y1);
}

// What a render needs worked out once, not for each pixel.
typedef struct {
    const GlobeTable *t;
    const GlobeColors *c;
    const uint32_t *land;
    int land_w, night_w;
    float lon, cols;
    uint32_t night_x, night_y;  // The land's column and row to the night's, 16.16.
    Color ocean_under;          // The sea under the terrain, darker by its gain (the gain goes on the mix).
    unsigned gain;              // The terrain's gain, 8.8.
} Painter;

// One pixel of the picture: nothing, the air's glow, ice, or ocean and land, shaded.
static uint32_t picturePixel(const Painter *p, size_t i)
{
    const GlobeTable *t = p->t;
    const GlobeColors *c = p->c;
    unsigned alpha = t->alpha[i];
    if (!alpha)
        return 0;
    if (t->air[i])
        return (c->air & 0x00FFFFFFu) | (uint32_t)alpha << 24;
    Color here;
    float lon = p->lon + t->lon[i];  // In [-360, 360]: back to [-180, 180).
    lon = lon < -180.0f ? lon + 360.0f : lon >= 180.0f ? lon - 360.0f : lon;
    int col = (int)((lon + 180.0f) * p->cols);
    col = col < 0 ? 0 : col >= p->land_w ? p->land_w - 1 : col;
    int row = t->row[i];
    unsigned shade = t->shade[i];
    if (row == -1) {
        here = c->ocean;
        row = 0;  // North of the map: its night as the map's top row.
    } else if (row == -2) {
        here = c->ice;
        row = t->land_h - 1;
    } else {
        uint32_t land = p->land[(size_t)row * p->land_w + col];
        if (c->terrain) {
            here = gfxMix(p->ocean_under, land, land >> 24);
            shade = shade * p->gain >> 8;
        } else
            here = gfxMix(c->ocean, c->land, land >> 24);
    }
    if (c->night) {  // From DAY_GAIN of the light in the day to 1 - NIGHT_DIM of it at night.
        size_t cell = (size_t)((uint32_t)row * p->night_y >> 16) * p->night_w + ((uint32_t)col * p->night_x >> 16);
        float dark = c->night[cell] / 255.0f;
        shade = (unsigned)(shade * (DAY_GAIN - (DAY_GAIN - 1.0f + NIGHT_DIM) * dark));
    }
    return scaled(here, shade, alpha);
}

void globeRenderPart(const GlobeTable *t, float lon, const uint32_t *land, int land_w, const GlobeColors *c,
                     uint32_t *out, int stride, int x0, int y0, int x1, int y1)
{
    int night_w = c->night_w > 0 ? c->night_w : land_w, night_h = c->night_h > 0 ? c->night_h : t->land_h;
    Painter p = {t, c, land, land_w, night_w, globeWrapLon(lon), land_w / 360.0f, 0, 0, c->ocean, 256};
    p.night_x = (uint32_t)(((uint64_t)night_w << 16) / (unsigned)(land_w > 0 ? land_w : 1));
    p.night_y = (uint32_t)(((uint64_t)night_h << 16) / (unsigned)(t->land_h > 0 ? t->land_h : 1));
    if (c->terrain_gain > 0.0f && c->terrain_gain != 1.0f) {
        p.gain = (unsigned)lroundf(c->terrain_gain * 256.0f);
        p.ocean_under = scaled(c->ocean, (unsigned)lroundf(255.0f / c->terrain_gain), 0);
    }
    int cx0 = x0 < t->x0 ? t->x0 : x0, cy0 = y0 < t->y0 ? t->y0 : y0;
    int cx1 = x1 > t->x1 ? t->x1 : x1, cy1 = y1 > t->y1 ? t->y1 : y1;
    for (int y = cy0; y < cy1; y++) {
        uint32_t *line = out + (size_t)(y - y0) * stride;
        for (int x = cx0; x < cx1; x++)
            line[x - x0] = picturePixel(&p, globeTableAt(t, x, y));
    }
}

int globeImageSize(float scale, float span, bool up)
{
    float raw = scale * span;
    if (!(raw > 16.0f))
        return 16;
    float steps = 4.0f * log2f(raw / GLOBE_IMAGE);  // A hair either way is the step it's on.
    steps = up ? ceilf(steps - SIZE_SLOP) : floorf(steps + SIZE_SLOP);
    int size = (int)lroundf(GLOBE_IMAGE * exp2f(steps / 4.0f));
    return size + (size & 1);
}

float globeImageScale(float want, float area, float budget)
{
    if (!(area > 0.0f) || !(budget > 0.0f))
        return want;
    float most = sqrtf(budget / area);
    return want < most ? want : most;
}
