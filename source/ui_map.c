#include "ui_map.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "font.h"
#include "gfx.h"
#include "image_config.h"
#include "stb_image.h"
#include "ui_buttons.h"
#include "ui_common.h"
#include "ui_hit.h"
#include "world_map_png.h"
#include "worldmap.h"

#define COLOR_LAND GFX_RGB(0x33, 0x39, 0x47)
#define COLOR_LIGHT GFX_RGB(0xFF, 0xC8, 0x6A)  // Warm, like a city at night.
#define COLOR_CORE GFX_RGB(0xFF, 0xF4, 0xDC)   // The middle of a country's light: the one to pick.
#define GLOW_MAX 160                           // Largest glow radius kept, screen pixels.
#define SPARK_R 2.4f                           // The small lights spread over a country.
#define CORE_R 2.6f
#define CARD_W 330  // Over the Pacific, clear of South America.
#define CARD_H 78

static uint8_t *g_land;           // MAP_IMAGE_W x MAP_IMAGE_H coverage, from the PNG.
static bool g_land_failed;
static uint32_t *g_map;           // The land for this screen: g_map_w x g_map_h, coverage as alpha.
static int g_map_w, g_map_h;
static uint8_t *g_glows[GLOW_MAX + 1];  // A soft round light of each radius, made when first needed.

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
        g_map_w = want_w;
        g_map_h = want_h;
    }
    *w = g_map_w;
    *h = g_map_h;
    return g_map;
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

// A country's lights: faint ones spread over its land, twinkling, and at its
// middle a halo with a sharp core, the light to pick.
static void drawCountry(const MapCountry *c, int stations, uint64_t now_ns, unsigned seed)
{
    float t = (float)(now_ns % 60000000000ull) / 1e9f;
    int n = mapLightCount(stations);
    if (n > c->count)
        n = c->count;
    for (int i = 0; i < n; i++) {
        float x, y;
        mapToLayout(MAP_POINTS[c->first + i], &x, &y);
        float phase = (float)((seed * 7u + (unsigned)i * 13u) % 100u) * 0.0628f;
        float bright = 0.52f + 0.2f * sinf(t * 1.3f + phase);
        drawLight(x, y, SPARK_R, gfxMix(COLOR_BG, COLOR_LIGHT, (unsigned)(bright * 255.0f)));
    }
    float x, y;
    mapToLayout(c->label, &x, &y);
    drawLight(x, y, mapGlowRadius(stations), COLOR_LIGHT);
    drawDot(x, y, CORE_R + 1.4f, COLOR_BG);  // A dark edge, so the core reads apart from the halo.
    drawDot(x, y, CORE_R, COLOR_CORE);
}

// The picked country: a ring around its light, and a card that opens it.
static void drawPicked(const Category *c, uint64_t now_ns)
{
    const MapCountry *place = mapFindCountry(c->code);
    if (place) {
        float x, y;
        mapToLayout(place->label, &x, &y);
        float pulse = (float)(now_ns % 1600000000ull) / 1.6e9f;
        gfxArc(x, y, 13.0f + 3.0f * sinf(pulse * 6.2832f), 3.0f, 0.0f, 6.2832f, COLOR_ACCENT);
        drawDot(x, y, CORE_R + 1.0f, COLOR_ACCENT);
    }
    int cx = MARGIN, cy = MAP_Y + MAP_H - CARD_H - 8;
    gfxFillRoundRect(cx, cy, CARD_W, CARD_H, 14, COLOR_SURFACE);
    gfxFillRect(cx, cy + 16, 3, CARD_H - 32, COLOR_ACCENT);
    char label[CATEGORY_NAME_MAX], count[48];
    categoryLabel(c, label, sizeof(label));
    snprintf(count, sizeof(count), "%d station%s", c->count, c->count == 1 ? "" : "s");
    const char *open = "Open";
    int w = uiPillWidth(open);
    fontDrawClipped(FONT_BOLD, 22, cx + 20, cy + 12, CARD_W - w - 52, label, COLOR_TEXT);
    fontDraw(FONT_REGULAR, 17, cx + 20, cy + 44, count, COLOR_DIM);
    uiDrawPill(cx + CARD_W - w - 16, cy + (CARD_H - 30) / 2, open, COLOR_ACCENT, COLOR_BG);
    uiHitAdd(cx, cy, CARD_W, CARD_H, HIT_MAP_OPEN, 0);
}

void uiDrawMap(const App *app, uint64_t now_ns)
{
    int w, h;
    const uint32_t *map = mapImage(&w, &h);
    if (map)
        gfxDrawImageScaledFb((float)gfxToFb(MAP_X), (float)gfxToFb(MAP_Y), MAP_FB_W, MAP_FB_H, w, h, map);
    const CategoryList *countries = &app->categories[SEARCH_COUNTRY];
    if (countries->count == 0)
        return;
    uiHitAdd(MAP_X, MAP_Y, MAP_W, MAP_H, HIT_MAP, 0);
    // The ones with fewest stations first, so the bright ones draw over them.
    for (size_t i = countries->count; i-- > 0;) {
        const MapCountry *c = mapFindCountry(countries->items[i].code);
        if (c)
            drawCountry(c, countries->items[i].count, now_ns, (unsigned)i);
    }
    if (app->map_cursor < countries->count)
        drawPicked(&countries->items[app->map_cursor], now_ns);
}
