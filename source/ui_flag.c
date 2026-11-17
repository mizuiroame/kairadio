#include "ui_flag.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "flags.h"
#include "flags_bin.h"
#include "gfx.h"
#include "image_config.h"
#include "stb_image.h"

#define CACHE 32  // Flags kept decoded: more than a screen shows.

typedef struct {
    const FlagEntry *flag;
    uint32_t *pixels;
    int w, h;            // Full size, or half on a 1080p screen (shrunk evenly, so sharp).
    unsigned last_used;
} CachedFlag;

static CachedFlag g_cache[CACHE];
static unsigned g_clock;

// Averages each 2x2 block, in place: the flag at half size.
static void halve(uint32_t *pixels, int w, int h)
{
    for (int y = 0; y < h / 2; y++)
        for (int x = 0; x < w / 2; x++) {
            const uint32_t *p = pixels + (size_t)(2 * y) * w + 2 * x;
            uint32_t q[4] = {p[0], p[1], p[w], p[w + 1]}, out = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                uint32_t sum = 0;
                for (int i = 0; i < 4; i++)
                    sum += q[i] >> shift & 0xFF;
                out |= ((sum + 2) / 4) << shift;
            }
            pixels[(size_t)y * (w / 2) + x] = out;
        }
}

static const CachedFlag *decoded(const FlagEntry *flag, bool half)
{
    int w = half ? FLAG_IMAGE_W / 2 : FLAG_IMAGE_W, h = half ? FLAG_IMAGE_H / 2 : FLAG_IMAGE_H;
    CachedFlag *slot = &g_cache[0];
    for (int i = 0; i < CACHE; i++) {
        CachedFlag *c = &g_cache[i];
        if (c->flag == flag && c->w == w) {
            c->last_used = ++g_clock;
            return c;
        }
        if (!c->pixels || (slot->pixels && c->last_used < slot->last_used))
            slot = c;  // Empty, or used longest ago.
    }
    int iw, ih, comp;
    uint32_t *pixels = (uint32_t *)stbi_load_from_memory(flags_bin + flag->offset, (int)flag->size, &iw, &ih, &comp, 4);
    if (!pixels || iw != FLAG_IMAGE_W || ih != FLAG_IMAGE_H) {
        stbi_image_free(pixels);
        return NULL;
    }
    if (half)
        halve(pixels, iw, ih);
    if (slot->pixels) {
        gfxImageChanged(slot->pixels);
        stbi_image_free(slot->pixels);
    }
    *slot = (CachedFlag){flag, pixels, w, h, ++g_clock};
    return slot;
}

bool uiDrawFlag(int x, int y, int w, int h, const char *code)
{
    const FlagEntry *flag = flagFind(code);
    if (!flag)
        return false;
    // A handheld (1080p) screen gets the flag at half, which fits it 1:1.
    const CachedFlag *c = decoded(flag, gfxDetail() < 1.2f);
    if (!c)
        return false;
    gfxDrawImageScaledFb((float)gfxToFb(x), (float)gfxToFb(y), (float)gfxToFb(w), (float)gfxToFb(h), c->w, c->h,
                         c->pixels);
    return true;
}
