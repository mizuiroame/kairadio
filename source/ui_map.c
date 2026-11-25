#include "ui_map.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "font.h"
#include "gfx.h"
#include "globe.h"
#include "image_config.h"
#include "lang.h"
#include "mapzoom.h"
#include "stb_image.h"
#include "sunlight.h"
#include "terrain.h"
#include "ui_buttons.h"
#include "ui_common.h"
#include "ui_flag.h"
#include "ui_hit.h"
#include "ui_passport.h"
#include "world_map_png.h"
#include "world_terrain_jpg.h"
#include "weatherfetch.h"
#include "worldmap.h"

#define COLOR_LAND GFX_RGB(0x33, 0x39, 0x47)
#define COLOR_LIGHT GFX_RGB(0xFF, 0xC8, 0x6A)  // Warm, like a city at night.
#define COLOR_CORE GFX_RGB(0xFF, 0xF4, 0xDC)   // The middle of a country's light: the one to pick.
#define GLOW_MAX 160                           // Largest glow radius kept, screen pixels.
#define SPARK_R 2.4f                           // The small lights spread over a country.
#define CORE_R 2.6f
#define CARD_W 330  // Over the Pacific, clear of South America.
#define CARD_H 78
#define NIGHT_W 1024           // The night's grid: about the size of its own soft edge.
#define NIGHT_H 395            // (MAP_NORTH - MAP_SOUTH) / 360 of that.
#define GLOBE_TILT_STEP 0.25f  // The globe's table is made again when its tilt moves this much.
#define COLOR_OCEAN GFX_RGB(0x14, 0x22, 0x38)
#define COLOR_GLOBE_LAND GFX_RGB(0x46, 0x52, 0x66)
#define COLOR_ICE GFX_RGB(0x8C, 0x98, 0xA8)
#define COLOR_AIR GFX_RGB(0x5C, 0x9C, 0xE8)
// Beside the globe: the country picked, and a random station.
#define PANEL_X 32
#define PANEL_Y 118
#define PANEL_W 420
#define PANEL_FLAG_W 112
#define PANEL_FLAG_H 84
#define RANDOM_Y (PANEL_Y + 304)
#define INFO_ICON 22          // The time's and the weather's icons.
#define TUNER_TICKS 12        // The tuning dial's marks.
#define TUNER_REST 0.62f      // Where its needle rests.
#define RANDOM_H 84
#define SHARP_ZOOM 1.5f       // Zoomed past this, the flat map draws from its full picture.
#define GLOBE_FADE 64          // Pixels over which the zoomed globe fades out under the header and over the bar,
#define FLAT_FADE 48           // and the zoomed flat map at its box's edges.
#define PANEL_VEIL 225         // How opaque the panel's backing is over a globe zoomed under it,
#define VEIL_FADE 150          // and how far past the panel it fades out.
#define TABLE_SLACK 48        // Pixels made around the part that shows (at GLOBE_IMAGE), so a small zoom or move needn't make it again.
#define TURN_BUDGET 300000L   // The turning globe's pixels in the box: zoomed, about the whole globe's cost.
#define SHARP_BUDGET 2000000L // The still globe's: about the box at 1080p.
#define SHARP_STEP 300000L    // Its work each frame, in pixels painted: about the turning globe's.
#define TABLE_COST 2          // A pixel of the table costs about this many painted.
#define STILL_FRAMES 6        // Frames the globe stays still before its sharp picture is made.
#define FINE_SIZE 1024        // A picture this big or more paints from the land at its full detail (less: it'd flicker).
#define NIGHT_COLOR GFX_RGB(0x02, 0x04, 0x0E)  // The night over the flat map,
#define FLAT_NIGHT_ALPHA 150   // at its darkest: the land and the lights still read through it;
#define DAY_COLOR GFX_RGB(0xC8, 0xD6, 0xEC)    // and the day over it, a little light,
#define FLAT_DAY_ALPHA 20      // this much.
#define FLAT_TERRAIN 0.8f      // The flat map's land: its terrain this bright (the globe's light adds to it).
#define NIGHT_EVERY_S 60       // The night is worked out again each minute.
#define FULL_ROWS 64           // The land at its full detail is made this many rows a frame.


static uint8_t *g_land;           // MAP_IMAGE_W x MAP_IMAGE_H coverage, from the PNG.
static bool g_land_failed;
static Terrain g_terrain;         // The land's colors, from the JPEG (empty if it can't be read: plain land).
static uint32_t *g_map;           // The land for this screen: g_map_w x g_map_h, coverage as alpha.
static int g_map_w, g_map_h;
static uint8_t *g_glows[GLOW_MAX + 1];  // A soft round light of each radius, made when first needed.
static int64_t g_clock;           // uiMapSetClock's moment, or 0 for the console's clock.
static uint8_t *g_night;          // The night now over the globe's land grid (NIGHT_W x NIGHT_H),
static uint32_t *g_night_image;   // and as a picture over the flat map,
static int64_t g_night_minute;    // made for this minute.
static bool g_full_failed;        // No memory for the land at its full detail.

void uiMapSetClock(int64_t utc)
{
    g_clock = utc;
}

// Works out the night again when the minute changes. False without memory for it.
static bool updateNight(void)
{
    static int64_t made = -1;
    if (!g_night) {
        g_night = malloc(NIGHT_W * NIGHT_H);
        g_night_image = g_night ? malloc(NIGHT_W * NIGHT_H * sizeof(uint32_t)) : NULL;
        if (!g_night_image) {
            free(g_night);
            g_night = NULL;
            return false;
        }
    }
    int64_t utc = g_clock ? g_clock : (int64_t)time(NULL), minute = utc / NIGHT_EVERY_S;
    if (minute == made)
        return true;
    made = minute;
    g_night_minute = minute;
    sunNightGrid(sunOverhead(utc), NIGHT_W, NIGHT_H, MAP_NORTH, MAP_SOUTH, g_night);
    gfxImageChanged(g_night_image);
    for (int i = 0; i < NIGHT_W * NIGHT_H; i++) {
        // Light by day, dark by night: in the twilight, each by its own share
        // of the cover (mixing the colors alone would make it a bright band).
        unsigned n = g_night[i], day = FLAT_DAY_ALPHA * (255 - n) / 255, night = FLAT_NIGHT_ALPHA * n / 255;
        unsigned alpha = day + night;
        Color color = alpha ? gfxMix(DAY_COLOR, NIGHT_COLOR, night * 255 / alpha) : DAY_COLOR;
        g_night_image[i] = (color & 0x00FFFFFFu) | (uint32_t)alpha << 24;
    }
    return true;
}

// Shrinks the land to w x h, each pixel the average of the ones it covers.
static uint32_t *landAt(int w, int h)
{
    uint32_t *out = malloc((size_t)w * h * sizeof(uint32_t));
    uint16_t *rows = malloc((size_t)w * MAP_IMAGE_H * sizeof(uint16_t));
    if (!out || !rows) {
        free(out);
        free(rows);
        return NULL;
    }
    // Across, then down; sums in 1/256ths of a pixel.
    float sx = (float)MAP_IMAGE_W / w, sy = (float)MAP_IMAGE_H / h;
    for (int y = 0; y < MAP_IMAGE_H; y++) {
        const uint8_t *src = g_land + (size_t)y * MAP_IMAGE_W;
        for (int x = 0; x < w; x++) {
            float a = x * sx, b = a + sx, sum = 0;
            for (int i = (int)a; i < (int)ceilf(b) && i < MAP_IMAGE_W; i++)
                sum += src[i] * (fminf(b, i + 1.0f) - fmaxf(a, (float)i));
            rows[(size_t)y * w + x] = (uint16_t)lroundf(sum / sx * 256.0f / 255.0f);
        }
    }
    for (int y = 0; y < h; y++) {
        float a = y * sy, b = a + sy;
        for (int x = 0; x < w; x++) {
            float sum = 0;
            for (int i = (int)a; i < (int)ceilf(b) && i < MAP_IMAGE_H; i++)
                sum += rows[(size_t)i * w + x] * (fminf(b, i + 1.0f) - fmaxf(a, (float)i));
            int alpha = (int)lroundf(sum / sy * 255.0f / 256.0f);
            out[(size_t)y * w + x] = (COLOR_LAND & 0x00FFFFFFu) | (uint32_t)(alpha > 255 ? 255 : alpha) << 24;
        }
    }
    free(rows);
    return out;
}

// The land at the screen's own detail: sharp handheld (1080p) and on a 1440p or 4K TV.
static const uint32_t *mapImage(int *w, int *h)
{
    if (!g_land && !g_land_failed) {
        int iw, ih, comp;
        g_land = stbi_load_from_memory(world_map_png, (int)world_map_png_size, &iw, &ih, &comp, 1);
        if (g_land && (iw != MAP_IMAGE_W || ih != MAP_IMAGE_H)) {
            stbi_image_free(g_land);
            g_land = NULL;
        }
        g_land_failed = !g_land;
        if (g_land)
            terrainLoad(&g_terrain, world_terrain_jpg, world_terrain_jpg_size);
    }
    if (!g_land)
        return NULL;
    float detail = gfxDetail() > 2.0f ? 2.0f : gfxDetail();
    int want_w = (int)lroundf(MAP_FB_W * detail), want_h = (int)lroundf(MAP_FB_H * detail);
    if (!g_map || g_map_w != want_w || g_map_h != want_h) {
        if (g_map) {
            gfxImageChanged(g_map);
            free(g_map);
        }
        g_map = landAt(want_w, want_h);
        if (g_map && g_terrain.rgb)
            terrainPaint(&g_terrain, g_map, want_w, want_h, FLAT_TERRAIN);
        g_map_w = want_w;
        g_map_h = want_h;
    }
    *w = g_map_w;
    *h = g_map_h;
    return g_map;
}

// The land at its full detail, for the zoomed flat map and the still globe:
// the PNG's coverage as alpha. It's big: made FULL_ROWS rows a call, NULL
// until it's all made (or if there's no memory for it: g_full_failed).
static const uint32_t *mapImageFull(void)
{
    static uint32_t *full;
    static int made;  // Rows made so far.
    if (made == MAP_IMAGE_H)
        return full;
    if (g_full_failed || !mapImage(&(int){0}, &(int){0}))  // mapImage reads the PNG.
        return NULL;
    if (!full) {
        full = malloc((size_t)MAP_IMAGE_W * MAP_IMAGE_H * sizeof(uint32_t));
        g_full_failed = !full;  // The screen's map, zoomed, and not asked again.
        if (!full)
            return NULL;
    }
    int end = made + FULL_ROWS > MAP_IMAGE_H ? MAP_IMAGE_H : made + FULL_ROWS;
    for (size_t i = (size_t)made * MAP_IMAGE_W; i < (size_t)end * MAP_IMAGE_W; i++)
        full[i] = (COLOR_LAND & 0x00FFFFFFu) | (uint32_t)g_land[i] << 24;
    if (g_terrain.rgb)
        terrainPaintRows(&g_terrain, full, MAP_IMAGE_W, MAP_IMAGE_H, made, end, FLAT_TERRAIN);
    made = end;
    return made == MAP_IMAGE_H ? full : NULL;
}

// The flat map, zoomed: drawn as big as the zoom makes it, cut to its box.
static void drawFlatMap(const App *app)
{
    int w, h;
    const uint32_t *map = mapImage(&w, &h);
    if (!map)
        return;
    const MapZoom *z = &app->map_zoom;
    const uint32_t *full = z->zoom > SHARP_ZOOM ? mapImageFull() : NULL;  // The screen's until it's made.
    if (full) {
        map = full;
        w = MAP_IMAGE_W;
        h = MAP_IMAGE_H;
    }
    float x0, y0;
    mapZoomToScreen(z, MAP_X, MAP_Y, &x0, &y0);
    gfxSetClip(MAP_X, MAP_Y, MAP_W, MAP_H);
    gfxDrawImageScaledFb(x0 * GFX_SCALE, y0 * GFX_SCALE, MAP_FB_W * z->zoom, MAP_FB_H * z->zoom, w, h, map);
    if (updateNight())  // Where it's night now, darker: the countries' lights go over it.
        gfxDrawImageScaledFb(x0 * GFX_SCALE, y0 * GFX_SCALE, MAP_FB_W * z->zoom, MAP_FB_H * z->zoom, NIGHT_W,
                             NIGHT_H, g_night_image);
    gfxResetClip();
}

// A (2r+1)-pixel square mask: bright in the middle, fading to nothing at r.
static const uint8_t *glow(int r)
{
    if (g_glows[r])
        return g_glows[r];
    int size = 2 * r + 1;
    uint8_t *mask = malloc((size_t)size * size);
    if (!mask)
        return NULL;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float d = hypotf((float)(x - r), (float)(y - r)) / (float)r;
            float a = d >= 1.0f ? 0.0f : expf(-4.0f * d * d) * (1.0f - d);
            mask[y * size + x] = (uint8_t)lroundf(a * 255.0f);
        }
    g_glows[r] = mask;
    return mask;
}

// A soft light of radius r (layout pixels) at (x, y), made at the screen's detail.
static void drawLight(float x, float y, float r, Color color)
{
    int rs = (int)lroundf(r * GFX_SCALE * gfxDetail());
    rs = rs < 1 ? 1 : rs > GLOW_MAX ? GLOW_MAX : rs;
    const uint8_t *mask = glow(rs);
    if (!mask)
        return;
    int size = 2 * rs + 1;
    float fb = (float)size / gfxDetail();  // Its size in framebuffer pixels.
    gfxBlendMaskScaled(x * GFX_SCALE - fb / 2.0f, y * GFX_SCALE - fb / 2.0f, fb, fb, mask, size, size, size, color);
}

// A filled, smooth dot: a ring as thick as its radius.
static void drawDot(float x, float y, float r, Color color)
{
    gfxArc(x, y, r / 2.0f, r, 0.0f, 6.2832f, color);
}

// Where a map point shows: on the flat map, or on the globe facing view
// (fade: 1 in its middle, toward 0 at its edge). False behind the globe.
static bool placeOf(const App *app, MapPoint p, float *x, float *y, float *fade)
{
    *fade = 1.0f;
    if (!app->config.world_globe) {
        float mx, my;
        mapToLayout(p, &mx, &my);
        mapZoomToScreen(&app->map_zoom, mx, my, x, y);
        return mapZoomInBox(*x, *y);
    }
    float depth;
    if (!globeProject(app->globe_view, globeMapPlace(p), x, y, &depth))
        return false;
    *fade = depth < 0.35f ? depth / 0.35f : 1.0f;  // Lights dim toward the edge, as they turn away.
    return true;
}

// A country's lights: faint ones spread over its land, twinkling, and at its
// middle a halo with a sharp core, the light to pick.
static void drawCountry(const App *app, const MapCountry *c, int stations, uint64_t now_ns, unsigned seed)
{
    float t = (float)(now_ns % 60000000000ull) / 1e9f, x, y, fade;
    int n = mapLightCount(stations);
    if (n > c->count)
        n = c->count;
    for (int i = 0; i < n; i++) {
        if (!placeOf(app, MAP_POINTS[c->first + i], &x, &y, &fade))
            continue;
        float phase = (float)((seed * 7u + (unsigned)i * 13u) % 100u) * 0.0628f;
        float bright = (0.52f + 0.2f * sinf(t * 1.3f + phase)) * fade;
        drawLight(x, y, SPARK_R, gfxMix(COLOR_BG, COLOR_LIGHT, (unsigned)(bright * 255.0f)));
    }
    if (!placeOf(app, c->label, &x, &y, &fade))
        return;
    drawLight(x, y, mapGlowRadius(stations) * (0.5f + 0.5f * fade), gfxMix(COLOR_BG, COLOR_LIGHT, (unsigned)(fade * 255)));
    drawDot(x, y, CORE_R + 1.4f, COLOR_BG);  // A dark edge, so the core reads apart from the halo.
    drawDot(x, y, CORE_R, gfxMix(COLOR_BG, COLOR_CORE, (unsigned)(fade * 255)));
}

// The land the globe is painted from: the flat map's own pictures of it.
typedef struct {
    const uint32_t *pixels;  // Coverage as alpha, the terrain's colors (at FLAT_TERRAIN).
    int w, h;
} GlobeLand;

// The land at the flat map's 1080p detail (MAP_FB_W across, as handheld it
// already is), or (fine) at its full detail. False if the PNG can't be read,
// or the full detail is still being made (without memory for it: the other).
static bool globeLand(bool fine, GlobeLand *out)
{
    static uint32_t *mid;  // Made only when the screen's map is another size (a 1440p or 4K TV).
    int w, h;
    const uint32_t *map = mapImage(&w, &h);
    if (!map)
        return false;
    const uint32_t *full = fine ? mapImageFull() : NULL;
    if (full) {
        *out = (GlobeLand){full, MAP_IMAGE_W, MAP_IMAGE_H};
        return true;
    }
    if (fine && !g_full_failed)
        return false;
    if (w != MAP_FB_W && !mid) {
        mid = landAt(MAP_FB_W, MAP_FB_H);
        if (mid && g_terrain.rgb)
            terrainPaint(&g_terrain, mid, MAP_FB_W, MAP_FB_H, FLAT_TERRAIN);
    }
    *out = w == MAP_FB_W || !mid ? (GlobeLand){map, w, h} : (GlobeLand){mid, MAP_FB_W, MAP_FB_H};
    return true;
}

// The globe's colors over that land: the terrain's, made as bright as the
// globe's light wants; where it's night (when night is), darker.
static GlobeColors globeColors(bool night)
{
    return (GlobeColors){COLOR_OCEAN,     COLOR_GLOBE_LAND, COLOR_ICE,   COLOR_AIR,           night ? g_night : NULL,
                         g_terrain.rgb != NULL, NIGHT_W,    NIGHT_H, 1.0f / FLAT_TERRAIN};
}

// A picture of the globe: the part of it in the World view's box, at some
// size, for one view. Made at once while the globe turns, or a few rows a
// frame for the sharp one that shows when it stops.
typedef struct {
    GlobeTable table;
    uint32_t *picture;
    size_t cap;                  // Pixels the picture holds.
    int size, x0, y0, x1, y1;    // The part painted, of a size x size picture,
    GlobeView view;              // for this view,
    float tilt;
    int64_t night;               // this night (its minute, -1 none),
    const uint32_t *land;        // and this land.
    int painted;                 // Its rows painted so far.
    bool shown;                  // Painted all for this view and size (the night may have moved since).
} GlobeLayer;

// The part of a size x size picture over the globe at view that shows in
// the box, and where (left, top) and how big (per_px layout pixels a pixel).
typedef struct {
    int size, x0, y0, x1, y1;
    float left, top, per_px;
} GlobePart;

static GlobePart globePart(GlobeView view, int size)
{
    float half = globeRadius(view) / GLOBE_DISC;  // The picture's half-width, glow included, layout pixels.
    GlobePart p = {size, 0, 0, 0, 0, GLOBE_CX - half, GLOBE_CY - half, 2 * half / size};
    p.x0 = (int)floorf((GLOBE_BOX_X - p.left) / p.per_px);
    p.y0 = (int)floorf((GLOBE_BOX_Y - p.top) / p.per_px);
    p.x1 = (int)ceilf((GLOBE_BOX_X + GLOBE_BOX_W - p.left) / p.per_px) + 1;
    p.y1 = (int)ceilf((GLOBE_BOX_Y + GLOBE_BOX_H - p.top) / p.per_px) + 1;
    p.x0 = p.x0 < 0 ? 0 : p.x0;
    p.y0 = p.y0 < 0 ? 0 : p.y0;
    p.x1 = p.x1 > size ? size : p.x1;
    p.y1 = p.y1 > size ? size : p.y1;
    return p;
}

// The globe's picture's size for at most budget pixels in the box, and no
// more than the screen's own (detail: at most this many per framebuffer pixel).
static int globeSize(GlobeView view, float detail, long budget)
{
    float half = globeRadius(view) / GLOBE_DISC;
    float w = fminf(GLOBE_CX + half, GLOBE_BOX_X + GLOBE_BOX_W) - fmaxf(GLOBE_CX - half, GLOBE_BOX_X);
    float h = fminf(GLOBE_CY + half, GLOBE_BOX_Y + GLOBE_BOX_H) - fmaxf(GLOBE_CY - half, GLOBE_BOX_Y);
    float want = GFX_SCALE * detail, scale = globeImageScale(want, w * h, (float)budget);
    return globeImageSize(scale, 2 * half, scale >= want);  // Within the budget, or at least the screen's.
}

// Paints layer for view, part and land, about budget pixels of work this frame
// (a row at least). True when it's all painted.
static bool layerPaint(GlobeLayer *l, GlobeView view, float tilt, GlobePart p, int slack, const GlobeLand *land,
                       const GlobeColors *colors, int64_t night, long budget)
{
    bool same_view = l->view.lon == view.lon && l->view.lat == view.lat && l->view.zoom == view.zoom &&
                     l->tilt == tilt && l->size == p.size && l->x0 == p.x0 && l->y0 == p.y0 && l->x1 == p.x1 &&
                     l->y1 == p.y1 && l->land == land->pixels;
    if (!same_view || l->night != night) {
        size_t n = (size_t)(p.x1 - p.x0) * (size_t)(p.y1 - p.y0);
        if (n > l->cap) {
            gfxImageChanged(l->picture);
            free(l->picture);
            l->picture = malloc(n * sizeof(uint32_t));
            l->cap = l->picture ? n : 0;
            if (!l->picture)
                return false;
        }
        l->shown = l->shown && same_view;  // The night moved: what's painted still shows until it's all painted again.
        l->view = view;
        l->tilt = tilt;
        l->size = p.size;
        l->x0 = p.x0;
        l->y0 = p.y0;
        l->x1 = p.x1;
        l->y1 = p.y1;
        l->night = night;
        l->land = land->pixels;
        l->painted = 0;
    }
    int rows = p.y1 - p.y0, w = p.x1 - p.x0;
    if (l->painted == rows)
        return true;
    GlobeTable *t = &l->table;
    if (t->size != p.size || t->tilt != tilt || t->land_h != land->h || t->x0 > p.x0 || t->y0 > p.y0 ||
        t->x1 < p.x1 || t->y1 < p.y1) {
        if (!globeTableStart(t, p.size, tilt, land->h, p.x0 - slack, p.y0 - slack, p.x1 + slack, p.y1 + slack))
            return false;
    }
    if (t->made < t->y1 - t->y0) {  // The table first: a pixel of it is worth TABLE_COST painted.
        long before = t->made;
        if (!globeTableMake(t, budget / TABLE_COST))
            return false;
        budget -= (t->made - before) * (long)(t->x1 - t->x0) * TABLE_COST;
        if (budget <= 0)
            return false;
    }
    long most = budget / (w > 0 ? w : 1);
    int count = most < 1 ? 1 : most > rows - l->painted ? rows - l->painted : (int)most;
    int y = p.y0 + l->painted;
    globeRenderPart(t, view.lon, land->pixels, land->w, colors, l->picture + (size_t)l->painted * w, w, p.x0, y, p.x1,
                    y + count);
    l->painted += count;
    if (l->painted < rows)
        return false;
    gfxImageChanged(l->picture);
    l->shown = true;
    return true;
}

static void layerDraw(const GlobeLayer *l, GlobePart p)
{
    gfxSetClip(GLOBE_BOX_X, GLOBE_BOX_Y, GLOBE_BOX_W, GLOBE_BOX_H);
    gfxDrawImageScaledFb((p.left + p.x0 * p.per_px) * GFX_SCALE, (p.top + p.y0 * p.per_px) * GFX_SCALE,
                         (p.x1 - p.x0) * p.per_px * GFX_SCALE, (p.y1 - p.y0) * p.per_px * GFX_SCALE, p.x1 - p.x0,
                         p.y1 - p.y0, l->picture);
    gfxResetClip();
}

// The globe: the land on a sphere facing app->globe_view, lit from the upper
// left, with its air glowing around it. While it turns or zooms, a picture
// made each frame at about the whole globe's cost; when it stops, a sharp one
// at the screen's own detail, made over a few frames, takes its place.
static void drawGlobe(const App *app)
{
    static GlobeLayer turning, still;
    static GlobeView last;
    static int still_frames;
    GlobeView view = app->globe_view;
    bool same = view.lon == last.lon && view.lat == last.lat && view.zoom == last.zoom;
    still_frames = same ? still_frames + 1 : 0;
    last = view;
    int64_t night = updateNight() ? g_night_minute : -1;

    float detail = gfxDetail();
    int sharp_size = globeSize(view, detail, SHARP_BUDGET), size = globeSize(view, detail, TURN_BUDGET);
    size = size < GLOBE_IMAGE ? GLOBE_IMAGE : size;  // Never less than the whole globe's, as it was.
    GlobeColors colors = globeColors(night >= 0);
    GlobeLand land;
    // When it's stopped, and the sharp picture is sharper (not when the turning one is about as sharp).
    if (still_frames >= STILL_FRAMES && sharp_size * 4 > size * 5 && globeLand(sharp_size >= FINE_SIZE, &land)) {
        GlobePart part = globePart(view, sharp_size);
        layerPaint(&still, view, view.lat, part, 0, &land, &colors, night, SHARP_STEP);
        if (still.shown && still.view.lon == view.lon && still.view.lat == view.lat && still.view.zoom == view.zoom &&
            still.size == sharp_size) {
            layerDraw(&still, part);
            return;
        }
    }
    if (!globeLand(size >= FINE_SIZE, &land))
        return;
    float tilt = roundf(view.lat / GLOBE_TILT_STEP) * GLOBE_TILT_STEP;
    GlobePart part = globePart(view, size);
    // Rows and columns made around the part that shows, so a small zoom or move needn't make the table again.
    int slack = (int)lroundf(TABLE_SLACK * size / (float)GLOBE_IMAGE);
    if (layerPaint(&turning, view, tilt, part, slack, &land, &colors, night, LONG_MAX))
        layerDraw(&turning, part);
}

// The picked country's light: a ring around it, pulsing.
static void drawPickedLight(const App *app, const Category *c, uint64_t now_ns)
{
    const MapCountry *place = mapFindCountry(c->code);
    float x, y, fade;
    if (place && placeOf(app, place->label, &x, &y, &fade)) {
        float pulse = (float)(now_ns % 1600000000ull) / 1.6e9f;
        gfxArc(x, y, 13.0f + 3.0f * sinf(pulse * 6.2832f), 3.0f, 0.0f, 6.2832f, COLOR_ACCENT);
        drawDot(x, y, CORE_R + 1.0f, COLOR_ACCENT);
    }
}

// The picked country: a ring around its light, and a card that opens it.
static void drawPicked(const App *app, const Category *c, uint64_t now_ns)
{
    const MapCountry *place = mapFindCountry(c->code);
    float x, y, fade;
    if (place && placeOf(app, place->label, &x, &y, &fade)) {
        float pulse = (float)(now_ns % 1600000000ull) / 1.6e9f;
        gfxArc(x, y, 13.0f + 3.0f * sinf(pulse * 6.2832f), 3.0f, 0.0f, 6.2832f, COLOR_ACCENT);
        drawDot(x, y, CORE_R + 1.0f, COLOR_ACCENT);
    }
    int cx = MARGIN, cy = MAP_Y + MAP_H - CARD_H - 8;
    gfxFillRoundRect(cx, cy, CARD_W, CARD_H, 14, COLOR_SURFACE);
    gfxFillRect(cx, cy + 16, 3, CARD_H - 32, COLOR_ACCENT);
    char label[CATEGORY_NAME_MAX], count[64];
    categoryLabel(c, label, sizeof(label));
    snprintf(count, sizeof(count), c->count == 1 ? T("%d station") : T("%d stations"), c->count);
    const char *open = T("Open");
    int w = uiPillWidth(open);
    int tx = cx + 20;
    if (uiDrawFlag(tx, cy + (CARD_H - 30) / 2, 40, 30, c->code))
        tx += 40 + 14;
    fontDrawClipped(FONT_BOLD, 22, tx, cy + 12, cx + CARD_W - w - 32 - tx, label, COLOR_TEXT);
    fontDraw(FONT_REGULAR, 17, tx, cy + 44, count, COLOR_DIM);
    uiDrawPill(cx + CARD_W - w - 16, cy + (CARD_H - 30) / 2, open, COLOR_ACCENT, COLOR_BG);
    uiHitAdd(cx, cy, CARD_W, CARD_H, HIT_MAP_OPEN, 0);
}

// An old radio's tuning dial, for the random station: a window of
// frequencies and a needle that rests somewhere, or sweeps while the
// random country's stations come.
static void drawTuner(int x, int y, int w, int h, float needle)
{
    gfxFillRoundRect(x, y, w, h, 10, COLOR_ACCENT);
    gfxFillRoundRect(x + 3, y + 3, w - 6, h - 6, 8, GFX_RGB(0x14, 0x16, 0x1C));
    int left = x + 10, right = x + w - 10, top = y + 9;
    for (int i = 0; i <= TUNER_TICKS; i++) {
        float tx = left + (right - left) * (float)i / TUNER_TICKS;
        bool major = i % 4 == 0;
        gfxLine(tx, top, tx, top + (major ? 11.0f : 6.0f), major ? 2.0f : 1.4f, major ? COLOR_ACCENT : COLOR_DIM);
    }
    fontDraw(FONT_BOLD, 12, left, y + h - 22, "FM", COLOR_DIM);
    float nx = left + (right - left) * needle;
    gfxLine(nx, y + 6, nx, y + h - 6, 3.0f, GFX_RGB(0xF0, 0x45, 0x3A));
}

// A clock face showing roughly the hour.
static void drawClockIcon(float cx, float cy, float r, int hour, int minute, Color color)
{
    gfxArc(cx, cy, r, 2.2f, 0.0f, 6.2832f, color);
    float hand = ((hour % 12) + minute / 60.0f) / 12.0f * 6.2832f, min_hand = minute / 60.0f * 6.2832f;
    gfxLine(cx, cy, cx + sinf(hand) * r * 0.5f, cy - cosf(hand) * r * 0.5f, 2.2f, color);
    gfxLine(cx, cy, cx + sinf(min_hand) * r * 0.75f, cy - cosf(min_hand) * r * 0.75f, 1.8f, color);
}

// A cloud: three puffs and a flat bottom.
static void drawCloud(float cx, float cy, float s, Color color)
{
    gfxFillCircle((int)(cx - s * 0.32f), (int)(cy + s * 0.05f), (int)(s * 0.26f), color);
    gfxFillCircle((int)(cx + s * 0.05f), (int)(cy - s * 0.12f), (int)(s * 0.34f), color);
    gfxFillCircle((int)(cx + s * 0.36f), (int)(cy + s * 0.08f), (int)(s * 0.22f), color);
    gfxFillRoundRect((int)(cx - s * 0.55f), (int)(cy + s * 0.02f), (int)(s * 1.1f), (int)(s * 0.3f), (int)(s * 0.14f),
                     color);
}

// The weather as a small picture: sun or moon, clouds, rain, snow, fog, a storm.
static void drawWeatherIcon(float cx, float cy, float s, WeatherKind kind, bool day, Color color)
{
    Color light = day ? GFX_RGB(0xFF, 0xC8, 0x4A) : GFX_RGB(0xE6, 0xE8, 0xF4);
    if (kind == WEATHER_CLEAR || kind == WEATHER_PARTLY_CLOUDY) {
        float sx = kind == WEATHER_CLEAR ? cx : cx - s * 0.18f, sy = kind == WEATHER_CLEAR ? cy : cy - s * 0.16f;
        if (day) {
            gfxFillCircle((int)sx, (int)sy, (int)(s * 0.26f), light);
            for (int i = 0; i < 8; i++) {
                float a = i * 0.7854f;
                gfxLine(sx + cosf(a) * s * 0.36f, sy + sinf(a) * s * 0.36f, sx + cosf(a) * s * 0.48f,
                        sy + sinf(a) * s * 0.48f, 2.0f, light);
            }
        } else {  // A crescent moon.
            gfxFillCircle((int)sx, (int)sy, (int)(s * 0.32f), light);
            gfxFillCircle((int)(sx + s * 0.14f), (int)(sy - s * 0.1f), (int)(s * 0.28f), COLOR_BG);
        }
        if (kind == WEATHER_PARTLY_CLOUDY)
            drawCloud(cx + s * 0.12f, cy + s * 0.12f, s * 0.8f, color);
        return;
    }
    if (kind == WEATHER_FOG) {
        for (int i = 0; i < 3; i++)
            gfxLine(cx - s * 0.45f, cy - s * 0.25f + i * s * 0.25f, cx + s * 0.45f, cy - s * 0.25f + i * s * 0.25f,
                    2.5f, color);
        return;
    }
    drawCloud(cx, cy - s * 0.12f, s, color);
    Color drop = GFX_RGB(0x7C, 0xB8, 0xFF);
    for (int i = -1; i <= 1 && kind != WEATHER_CLOUDY && kind != WEATHER_STORM; i++) {
        float x = cx + i * s * 0.28f, y = cy + s * 0.32f;
        if (kind == WEATHER_SNOW)
            gfxFillCircle((int)x, (int)(y + s * 0.06f), (int)(s * 0.07f) + 1, GFX_RGB(0xF0, 0xF4, 0xFF));
        else
            gfxLine(x + s * 0.05f, y, x - s * 0.05f, y + (kind == WEATHER_DRIZZLE ? s * 0.12f : s * 0.22f), 2.0f,
                    drop);
    }
    if (kind == WEATHER_STORM) {  // A bolt.
        Color bolt = GFX_RGB(0xFF, 0xD2, 0x4A);
        gfxTriangle(cx + s * 0.05f, cy + s * 0.18f, cx - s * 0.16f, cy + s * 0.46f, cx + s * 0.02f, cy + s * 0.42f, bolt);
        gfxTriangle(cx + s * 0.02f, cy + s * 0.36f, cx + s * 0.16f, cy + s * 0.36f, cx - s * 0.06f, cy + s * 0.66f, bolt);
    }
}

// The time and weather where the country's stations are, as they come.
static void drawTimeAndWeather(const App *app, const Category *c, int x, int y)
{
    Weather w;
    WeatherState state = weatherFetchGet(c->code, &w);
    if (state != WEATHER_READY) {
        fontDraw(FONT_REGULAR, 17, x, y + 4,
                 state == WEATHER_FAILED ? T("The weather there can't be had now.")
                                         : T("Asking for the time and weather there\xE2\x80\xA6"),
                 COLOR_FAINT);
        return;
    }
    char time_text[24], line[128], temperature[48];
    int64_t utc = (int64_t)time(NULL);
    weatherLocalTime(utc, w.utc_offset_s, app->config.clock_24h, time_text, sizeof(time_text));
    int64_t local = utc + w.utc_offset_s;
    int seconds = (int)(((local % 86400) + 86400) % 86400);
    drawClockIcon(x + INFO_ICON / 2.0f, y + 13.0f, INFO_ICON / 2.0f - 1, seconds / 3600, seconds % 3600 / 60, COLOR_DIM);
    snprintf(line, sizeof(line), T("%s there"), time_text);
    fontDraw(FONT_BOLD, 19, x + INFO_ICON + 12, y + 2, line, COLOR_TEXT);
    WeatherKind kind = weatherKind(w.code);
    drawWeatherIcon(x + INFO_ICON / 2.0f, y + 47.0f, INFO_ICON + 4.0f, kind, w.day, COLOR_DIM);
    weatherTemperature(w.temperature, temperature, sizeof(temperature));
    snprintf(line, sizeof(line), "%s  \xC2\xB7  %s", weatherKindName(kind), temperature);
    fontDraw(FONT_REGULAR, 19, x + INFO_ICON + 12, y + 36, line, COLOR_TEXT);
}

// How much of the background covers a line i pixels in from a faded edge
// width pixels wide, at strength (0-1): all of it at the edge, smoothly none
// at the band's end.
static unsigned fadeAlpha(int i, int width, float strength)
{
    float t = 1.0f - (i + 0.5f) / width;
    return (unsigned)(255.0f * strength * t * t * (3.0f - 2.0f * t));
}

// How far past an edge something reaches, as a fade's strength: none when
// it stays inside, all of it once it's a band's width past.
static float pastEdge(float past, int width)
{
    return past <= 0 ? 0.0f : past >= width ? 1.0f : past / width;
}

// The zoomed globe's top and bottom fade into the background, under the
// header and over the player bar, instead of a straight cut: only as much
// as it reaches past them, so the whole globe isn't dimmed.
static void drawGlobeFades(const App *app)
{
    float reach = globeRadius(app->globe_view) / GLOBE_DISC;  // To its glow's edge.
    float top = pastEdge(GLOBE_BOX_Y - (GLOBE_CY - reach), GLOBE_FADE);
    float bottom = pastEdge(GLOBE_CY + reach - (GLOBE_BOX_Y + GLOBE_BOX_H), GLOBE_FADE);
    for (int i = 0; i < GLOBE_FADE && (top > 0 || bottom > 0); i++) {
        if (top > 0)
            gfxBlendRect(GLOBE_BOX_X, GLOBE_BOX_Y + i, GLOBE_BOX_W, 1, COLOR_BG, fadeAlpha(i, GLOBE_FADE, top));
        if (bottom > 0)
            gfxBlendRect(GLOBE_BOX_X, GLOBE_BOX_Y + GLOBE_BOX_H - 1 - i, GLOBE_BOX_W, 1, COLOR_BG,
                         fadeAlpha(i, GLOBE_FADE, bottom));
    }
}

// The zoomed flat map's four edges fade into the background instead of a
// straight cut; at the whole map's size (its edges the map's own), none.
static void drawFlatFades(const App *app)
{
    float strength = pastEdge((app->map_zoom.zoom - 1.0f) * MAP_W / 2.0f, FLAT_FADE);
    for (int i = 0; strength > 0 && i < FLAT_FADE; i++) {
        unsigned alpha = fadeAlpha(i, FLAT_FADE, strength);
        gfxBlendRect(MAP_X, MAP_Y + i, MAP_W, 1, COLOR_BG, alpha);
        gfxBlendRect(MAP_X, MAP_Y + MAP_H - 1 - i, MAP_W, 1, COLOR_BG, alpha);
        // The sides, short of the rows above: no corner faded twice.
        int y = MAP_Y + i + 1, h = MAP_H - 2 * i - 2;
        gfxBlendRect(MAP_X + i, y, 1, h, COLOR_BG, alpha);
        gfxBlendRect(MAP_X + MAP_W - 1 - i, y, 1, h, COLOR_BG, alpha);
    }
}

// A dark backing for the panel where the zoomed globe reaches under it: as
// opaque as the globe comes in, so the text stays readable over the land.
static void drawPanelVeil(const App *app)
{
    float left = GLOBE_CX - globeRadius(app->globe_view) / GLOBE_DISC;  // The globe's glow's left edge.
    float under = PANEL_X + PANEL_W + 24 - left;
    if (under <= 0)
        return;
    float strength = under >= 80 ? 1.0f : under / 80.0f;
    // Solid behind the panel, then fading to nothing toward the globe: no edge to see.
    int solid = PANEL_X + PANEL_W - 40, end = PANEL_X + PANEL_W + VEIL_FADE;
    for (int x = 0; x < end; x += 2) {
        float f = x < solid ? 1.0f : 1.0f - (float)(x - solid) / (end - solid);
        unsigned alpha = (unsigned)(PANEL_VEIL * strength * f * f);
        if (alpha)
            gfxBlendRect(x, GLOBE_BOX_Y, 2, GLOBE_BOX_H, COLOR_BG, alpha);
    }
}

// Beside the globe: the country picked (its flag, name and stations, its
// time and weather, and Open), a random station, and how to turn the globe.
static void drawPanel(const App *app, const Category *c, uint64_t now_ns)
{
    int x = PANEL_X, y = PANEL_Y;
    drawPanelVeil(app);
    if (c) {
        if (!uiDrawFlag(x, y, PANEL_FLAG_W, PANEL_FLAG_H, c->code))
            gfxFillRoundRect(x, y, PANEL_FLAG_W, PANEL_FLAG_H, 10, COLOR_SURFACE);
        char label[CATEGORY_NAME_MAX], count[64];
        categoryLabel(c, label, sizeof(label));
        snprintf(count, sizeof(count), c->count == 1 ? T("%d station") : T("%d stations"), c->count);
        fontDrawClipped(FONT_BOLD, 30, x, y + PANEL_FLAG_H + 18, PANEL_W, label, COLOR_TEXT);
        fontDraw(FONT_REGULAR, 19, x, y + PANEL_FLAG_H + 62, count, COLOR_DIM);
        drawTimeAndWeather(app, c, x, y + PANEL_FLAG_H + 98);
        int pill_y = y + PANEL_FLAG_H + 168;
        const char *open = T("Open its stations");
        uiDrawPill(x, pill_y, open, COLOR_ACCENT, COLOR_BG);
        uiHitAdd(x - 6, pill_y - 8, uiPillWidth(open) + 12, 46, HIT_MAP_OPEN, 0);
    }
    // A random station: somewhere unexpected.
    int ry = RANDOM_Y;
    gfxFillRoundRect(x, ry, PANEL_W, RANDOM_H, 16, COLOR_SURFACE);
    gfxFillRect(x, ry + 18, 3, RANDOM_H - 36, COLOR_ACCENT);
    bool tuning = app->waiting_active && app->waiting_target == TARGET_RANDOM;
    float needle = tuning ? 0.5f + 0.42f * sinf((float)(now_ns % 6283185307ull) / 1e9f * 5.0f) : TUNER_REST;
    drawTuner(x + 16, ry + (RANDOM_H - 50) / 2, 62, 50, needle);
    fontDraw(FONT_BOLD, 22, x + 86, ry + 16, T("Random station"), COLOR_TEXT);
    int said = fontDraw(FONT_REGULAR, 17, x + 86, ry + 48, T("Somewhere unexpected"), COLOR_DIM);
    uiDrawButton(x + 86 + said + 12, (float)(ry + 58), "Y", COLOR_SURFACE_HI, COLOR_DIM);
    uiHitAdd(x, ry, PANEL_W, RANDOM_H, HIT_RANDOM, 0);
    fontDrawClipped(FONT_REGULAR, 16, x, ry + RANDOM_H + 14, PANEL_W,
                    T("Turn the globe with a finger; the stick picks countries."), COLOR_FAINT);
    fontDrawClipped(FONT_REGULAR, 16, x, ry + RANDOM_H + 36, PANEL_W,
                    T("Two fingers or the right stick: closer or farther."), COLOR_FAINT);
}

void uiDrawMap(const App *app, uint64_t now_ns)
{
    if (app->config.world_globe)
        drawGlobe(app);
    else
        drawFlatMap(app);
    const CategoryList *countries = &app->categories[SEARCH_COUNTRY];
    if (countries->count == 0)
        return;
    if (app->config.world_globe) {  // The globe's square, cut to its box.
        float r = globeRadius(app->globe_view);
        float x0 = fmaxf(GLOBE_CX - r, GLOBE_BOX_X), y0 = fmaxf(GLOBE_CY - r, GLOBE_BOX_Y);
        float x1 = fminf(GLOBE_CX + r, GLOBE_BOX_X + GLOBE_BOX_W), y1 = fminf(GLOBE_CY + r, GLOBE_BOX_Y + GLOBE_BOX_H);
        uiHitAdd((int)x0, (int)y0, (int)(x1 - x0), (int)(y1 - y0), HIT_MAP, 0);
        gfxSetClip(GLOBE_BOX_X, GLOBE_BOX_Y, GLOBE_BOX_W, GLOBE_BOX_H);  // Its lights too.
    } else {
        uiHitAdd(MAP_X, MAP_Y, MAP_W, MAP_H, HIT_MAP, 0);
        gfxSetClip(MAP_X, MAP_Y, MAP_W, MAP_H);
    }
    // The ones with fewest stations first, so the bright ones draw over them,
    // however Search > Country sorts the list.
    static size_t order[CATEGORY_LIMIT];
    size_t n = countries->count < CATEGORY_LIMIT ? countries->count : CATEGORY_LIMIT;
    for (size_t i = 0; i < n; i++) {
        size_t j = i;
        for (; j > 0 && countries->items[order[j - 1]].count > countries->items[i].count; j--)
            order[j] = order[j - 1];
        order[j] = i;
    }
    for (size_t k = 0; k < n; k++) {
        const Category *item = &countries->items[order[k]];
        const MapCountry *c = mapFindCountry(item->code);
        if (c)
            drawCountry(app, c, item->count, now_ns, (unsigned)k);
    }
    gfxResetClip();
    const Category *picked = app->map_cursor < countries->count ? &countries->items[app->map_cursor] : NULL;
    if (app->config.world_globe) {  // Beside the globe, with room for more than the flat map's card.
        gfxSetClip(GLOBE_BOX_X, GLOBE_BOX_Y, GLOBE_BOX_W, GLOBE_BOX_H);  // Zoomed, it may be out of the box.
        if (picked)
            drawPickedLight(app, picked, now_ns);
        gfxResetClip();
        drawGlobeFades(app);
        drawPanel(app, picked, now_ns);
    } else {
        drawFlatFades(app);
        if (picked)
            drawPicked(app, picked, now_ns);
    }
    uiDrawPassportCard(app);
}
