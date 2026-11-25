#include "terrain.h"

#include <stdlib.h>

#include "image_config.h"
#include "stb_image.h"

bool terrainLoad(Terrain *t, const uint8_t *jpg, size_t size)
{
    int comp;
    t->rgb = stbi_load_from_memory(jpg, (int)size, &t->w, &t->h, &comp, 3);
    if (!t->rgb)
        t->w = t->h = 0;
    return t->rgb != NULL;
}

void terrainFree(Terrain *t)
{
    stbi_image_free(t->rgb);
    t->rgb = NULL;
    t->w = t->h = 0;
}

static unsigned channel(float v)
{
    return v <= 0.0f ? 0u : v >= 255.0f ? 255u : (unsigned)(v + 0.5f);
}

Color terrainColor(const Terrain *t, float u, float v, float brightness)
{
    if (!t->rgb)
        return GFX_RGB(0, 0, 0);
    // Pixel middles at (i + 0.5) / w: clamped at the edges.
    float fx = u * t->w - 0.5f, fy = v * t->h - 0.5f;
    fx = fx < 0 ? 0 : fx > t->w - 1 ? (float)(t->w - 1) : fx;
    fy = fy < 0 ? 0 : fy > t->h - 1 ? (float)(t->h - 1) : fy;
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = x0 + 1 < t->w ? x0 + 1 : x0, y1 = y0 + 1 < t->h ? y0 + 1 : y0;
    float ax = fx - x0, ay = fy - y0;
    const uint8_t *p00 = t->rgb + ((size_t)y0 * t->w + x0) * 3, *p10 = t->rgb + ((size_t)y0 * t->w + x1) * 3;
    const uint8_t *p01 = t->rgb + ((size_t)y1 * t->w + x0) * 3, *p11 = t->rgb + ((size_t)y1 * t->w + x1) * 3;
    unsigned c[3];
    for (int i = 0; i < 3; i++) {
        float top = p00[i] + (p10[i] - p00[i]) * ax, bottom = p01[i] + (p11[i] - p01[i]) * ax;
        c[i] = channel((top + (bottom - top) * ay) * brightness);
    }
    return GFX_RGB(c[0], c[1], c[2]);
}

void terrainPaint(const Terrain *t, uint32_t *pixels, int w, int h, float brightness)
{
    terrainPaintRows(t, pixels, w, h, 0, h, brightness);
}

void terrainPaintRows(const Terrain *t, uint32_t *pixels, int w, int h, int y0, int y1, float brightness)
{
    y0 = y0 < 0 ? 0 : y0;
    y1 = y1 > h ? h : y1;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < w; x++) {
            uint32_t *p = &pixels[(size_t)y * w + x];
            Color c = terrainColor(t, (x + 0.5f) / w, (y + 0.5f) / h, brightness);
            *p = (c & 0x00FFFFFFu) | (*p & 0xFF000000u);
        }
}
