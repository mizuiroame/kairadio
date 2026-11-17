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

#define COLOR_LAND GFX_RGB(0x30, 0x35, 0x42)
#define COLOR_LIGHT GFX_RGB(0xFF, 0xC8, 0x6A)  // Warm, like a city at night.
#define GLOW_MAX 64                            // Largest glow radius kept, framebuffer pixels.
#define CARD_W 330  // Over the Pacific, clear of South America.
#define CARD_H 78

static uint32_t *g_map;     // The land, MAP_FB_W x MAP_FB_H, its coverage as alpha.
static bool g_map_failed;
static uint8_t *g_glows[GLOW_MAX + 1];  // A soft round light of each radius, made when first needed.

static const uint32_t *mapImage(void)
{
    if (g_map || g_map_failed)
        return g_map;
    int w, h, comp;
    uint8_t *gray = stbi_load_from_memory(world_map_png, (int)world_map_png_size, &w, &h, &comp, 1);
    g_map = gray && w == MAP_FB_W && h == MAP_FB_H ? malloc((size_t)w * h * sizeof(uint32_t)) : NULL;
    if (g_map)
        for (size_t i = 0; i < (size_t)w * h; i++)
            g_map[i] = (COLOR_LAND & 0x00FFFFFFu) | (uint32_t)gray[i] << 24;
    g_map_failed = !g_map;
    stbi_image_free(gray);
    return g_map;
}

// A (2r+1)-pixel square mask: bright in the middle, fading to nothing at r.
static const uint8_t *glow(int r)
{
    if (r < 1)
        r = 1;
    if (r > GLOW_MAX)
        r = GLOW_MAX;
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

// A light of radius r (layout pixels) at (x, y).
static void drawLight(float x, float y, float r, Color color)
{
    int rf = (int)lroundf(r * GFX_SCALE);
    const uint8_t *mask = glow(rf);
    if (!mask)
        return;
    if (rf > GLOW_MAX)
        rf = GLOW_MAX;
    int size = 2 * rf + 1;
    gfxBlendMask((int)lroundf(x * GFX_SCALE) - rf, (int)lroundf(y * GFX_SCALE) - rf, size, size, mask, size, color);
}

// A country's lights: some spread over its land, twinkling, and a bigger one at its middle.
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
        float bright = 0.55f + 0.45f * sinf(t * 1.3f + phase);
        drawLight(x, y, 3.2f, gfxMix(COLOR_BG, COLOR_LIGHT, (unsigned)(bright * 255.0f)));
    }
    float x, y;
    mapToLayout(c->label, &x, &y);
    drawLight(x, y, mapGlowRadius(stations), COLOR_LIGHT);
}

// The picked country: a ring around its light, and a card that opens it.
static void drawPicked(const Category *c, uint64_t now_ns)
{
    const MapCountry *place = mapFindCountry(c->code);
    if (place) {
        float x, y;
        mapToLayout(place->label, &x, &y);
        float pulse = (float)(now_ns % 1600000000ull) / 1.6e9f;
        gfxArc(x, y, 14.0f + 4.0f * sinf(pulse * 6.2832f), 3.0f, 0.0f, 6.2832f, COLOR_ACCENT);
    }
    int cx = MARGIN, cy = MAP_Y + MAP_H - CARD_H - 8;
    gfxFillRoundRect(cx, cy, CARD_W, CARD_H, 14, COLOR_SURFACE);
    gfxFillRect(cx, cy + 16, 3, CARD_H - 32, COLOR_ACCENT);
    char label[CATEGORY_NAME_MAX], count[48];
    categoryLabel(c, label, sizeof(label));
    snprintf(count, sizeof(count), "%d station%s", c->count, c->count == 1 ? "" : "s");
    fontDrawClipped(FONT_BOLD, 22, cx + 20, cy + 12, CARD_W - 130, label, COLOR_TEXT);
    fontDraw(FONT_REGULAR, 17, cx + 20, cy + 44, count, COLOR_DIM);
    const char *open = "Open";
    int w = uiPillWidth(open);
    uiDrawPill(cx + CARD_W - w - 16, cy + (CARD_H - 30) / 2, open, COLOR_ACCENT, COLOR_BG);
    uiHitAdd(cx, cy, CARD_W, CARD_H, HIT_MAP_OPEN, 0);
}

void uiDrawMap(const App *app, uint64_t now_ns)
{
    const uint32_t *map = mapImage();
    if (map)
        gfxDrawImageFb(gfxToFb(MAP_X), gfxToFb(MAP_Y), MAP_FB_W, MAP_FB_H, map);
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
