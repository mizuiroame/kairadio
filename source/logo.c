#include "logo.h"

#include <stdlib.h>
#include <string.h>

#include "image_config.h"
#include "stb_image.h"

#define LOGO_PAD (LOGO_SIZE / 12)  // Space between the logo and the tile's edge.
#define TILE 0xFFFFFFFFu

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

// An ICO file: the largest image inside, as a pointer into data. PNG images
// are returned as they are; BMP ones (no file header) are not readable here.
static bool icoLargestPng(const uint8_t *data, size_t size, const uint8_t **png, size_t *png_size)
{
    if (size < 6 || read16(data) != 0 || read16(data + 2) != 1)
        return false;
    unsigned count = read16(data + 4);
    if (count == 0 || 6 + (size_t)count * 16 > size)
        return false;
    unsigned best_area = 0;
    *png = NULL;
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *e = data + 6 + i * 16;
        unsigned w = e[0] ? e[0] : 256, h = e[1] ? e[1] : 256;
        uint32_t bytes = read32(e + 8), offset = read32(e + 12);
        if (offset > size || bytes > size - offset || bytes < 8)
            continue;
        if (memcmp(data + offset, "\x89PNG", 4) != 0)
            continue;
        if (w * h > best_area) {
            best_area = w * h;
            *png = data + offset;
            *png_size = bytes;
        }
    }
    return *png != NULL;
}

static uint8_t channel(uint32_t c, int shift)
{
    return (uint8_t)(c >> shift);
}

// Samples the image at (x, y), in source pixels, with a bilinear filter.
static uint32_t sample(const uint32_t *src, int w, int h, float x, float y)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > w - 1) x = (float)(w - 1);
    if (y > h - 1) y = (float)(h - 1);
    int x0 = (int)x, y0 = (int)y;
    int x1 = x0 + 1 < w ? x0 + 1 : x0, y1 = y0 + 1 < h ? y0 + 1 : y0;
    float fx = x - x0, fy = y - y0;
    uint32_t c00 = src[y0 * w + x0], c10 = src[y0 * w + x1], c01 = src[y1 * w + x0], c11 = src[y1 * w + x1];
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        float top = channel(c00, s) * (1 - fx) + channel(c10, s) * fx;
        float bottom = channel(c01, s) * (1 - fx) + channel(c11, s) * fx;
        out |= (uint32_t)(top * (1 - fy) + bottom * fy + 0.5f) << s;
    }
    return out;
}

// The average of the source pixels under one output pixel (shrinking).
static uint32_t average(const uint32_t *src, int w, int h, float x0, float y0, float step)
{
    int sx0 = (int)x0, sy0 = (int)y0;
    int sx1 = (int)(x0 + step), sy1 = (int)(y0 + step);
    if (sx1 <= sx0) sx1 = sx0 + 1;
    if (sy1 <= sy0) sy1 = sy0 + 1;
    if (sx1 > w) sx1 = w;
    if (sy1 > h) sy1 = h;
    unsigned sum[4] = {0}, n = 0;
    for (int y = sy0; y < sy1; y++)
        for (int x = sx0; x < sx1; x++, n++) {
            uint32_t c = src[y * w + x];
            for (int k = 0; k < 4; k++)
                sum[k] += channel(c, k * 8);
        }
    if (n == 0)
        return 0;
    uint32_t out = 0;
    for (int k = 0; k < 4; k++)
        out |= ((sum[k] + n / 2) / n) << (k * 8);
    return out;
}

// Lays c over the white tile by its alpha.
static uint32_t overTile(uint32_t c)
{
    unsigned a = c >> 24;
    uint32_t out = 0xFF000000u;
    for (int s = 0; s < 24; s += 8) {
        unsigned v = (channel(c, s) * a + 255 * (255 - a) + 127) / 255;
        out |= v << s;
    }
    return out;
}

bool logoDecode(const uint8_t *data, size_t size, uint32_t *out)
{
    if (!data || size == 0 || size > LOGO_FILE_MAX)
        return false;
    const uint8_t *png;
    size_t png_size;
    if (icoLargestPng(data, size, &png, &png_size)) {
        data = png;
        size = png_size;
    }
    int w, h, comp;
    // Logos come from anywhere on the web: one claiming to be huge is refused
    // before its pixels are allocated (stb's own limit is set for backgrounds).
    if (!stbi_info_from_memory(data, (int)size, &w, &h, &comp) || w > LOGO_MAX_DIMENSION ||
        h > LOGO_MAX_DIMENSION)
        return false;
    uint32_t *src = (uint32_t *)stbi_load_from_memory(data, (int)size, &w, &h, &comp, 4);
    if (!src)
        return false;
    if (w <= 0 || h <= 0) {
        stbi_image_free(src);
        return false;
    }
    for (int i = 0; i < LOGO_SIZE * LOGO_SIZE; i++)
        out[i] = TILE;
    int box = LOGO_SIZE - 2 * LOGO_PAD;
    float scale = (float)box / (float)(w > h ? w : h);
    int dw = (int)(w * scale + 0.5f), dh = (int)(h * scale + 0.5f);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int ox = (LOGO_SIZE - dw) / 2, oy = (LOGO_SIZE - dh) / 2;
    float step = 1.0f / scale;
    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < dw; x++) {
            uint32_t c = scale < 1.0f ? average(src, w, h, x * step, y * step, step)
                                      : sample(src, w, h, (x + 0.5f) * step - 0.5f, (y + 0.5f) * step - 0.5f);
            out[(oy + y) * LOGO_SIZE + ox + x] = overTile(c);
        }
    }
    stbi_image_free(src);
    return true;
}
