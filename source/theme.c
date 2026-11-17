#include "theme.h"

#include <math.h>

#define RGB(r, g, b) ((uint32_t)(0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r)))
#define MIN_SATURATION 0.55f
#define MIN_VALUE 0.88f
#define LOGO_MIN_SATURATION 0.25f  // Less than this is a white, gray or black pixel.
#define LOGO_MIN_SHARE 50          // Of 1000: fewer colorful pixels than this, and the logo has no color.

static const char *const NAMES[THEME_ACCENT_COUNT] = {
    "Sky blue", "Pink", "Green", "Purple", "Orange", "Red", "From station logo",
};
static const uint32_t COLORS[THEME_ACCENT_COUNT - 1] = {
    RGB(0x4F, 0xC3, 0xF7), RGB(0xFF, 0x6B, 0x9A), RGB(0x3D, 0xD6, 0x8C),
    RGB(0xA7, 0x8B, 0xFA), RGB(0xFF, 0x9F, 0x45), RGB(0xFF, 0x5C, 0x5C),
};

const char *themeAccentName(int accent)
{
    return accent >= 0 && accent < THEME_ACCENT_COUNT ? NAMES[accent] : "";
}

static float maxf(float a, float b)
{
    return a > b ? a : b;
}

static float minf(float a, float b)
{
    return a < b ? a : b;
}

// HSV of a color: hue 0-6, saturation and value 0-1.
static void toHsv(uint32_t color, float *h, float *s, float *v)
{
    float r = (float)(color & 0xFF) / 255.0f, g = (float)((color >> 8) & 0xFF) / 255.0f;
    float b = (float)((color >> 16) & 0xFF) / 255.0f;
    float max = maxf(r, maxf(g, b)), min = minf(r, minf(g, b)), delta = max - min;
    *h = 0.0f;
    if (delta > 0.0f) {
        if (max == r)
            *h = (g - b) / delta;
        else if (max == g)
            *h = 2.0f + (b - r) / delta;
        else
            *h = 4.0f + (r - g) / delta;
        *h = *h < 0.0f ? *h + 6.0f : *h;
    }
    *s = max > 0.0f ? delta / max : 0.0f;
    *v = max;
}

uint32_t themeVivid(uint32_t color)
{
    float h, s, v;
    toHsv(color, &h, &s, &v);
    if (s < 0.08f)
        return COLORS[0];  // A grey color has no hue to take.
    s = maxf(s, MIN_SATURATION);
    v = maxf(v, MIN_VALUE);
    float c = v * s, x = c * (1.0f - fabsf(fmodf(h, 2.0f) - 1.0f)), m = v - c;
    float rgb[3];
    switch ((int)h % 6) {
    case 0: rgb[0] = c, rgb[1] = x, rgb[2] = 0; break;
    case 1: rgb[0] = x, rgb[1] = c, rgb[2] = 0; break;
    case 2: rgb[0] = 0, rgb[1] = c, rgb[2] = x; break;
    case 3: rgb[0] = 0, rgb[1] = x, rgb[2] = c; break;
    case 4: rgb[0] = x, rgb[1] = 0, rgb[2] = c; break;
    default: rgb[0] = c, rgb[1] = 0, rgb[2] = x; break;
    }
    return RGB((uint32_t)((rgb[0] + m) * 255.0f + 0.5f), (uint32_t)((rgb[1] + m) * 255.0f + 0.5f),
               (uint32_t)((rgb[2] + m) * 255.0f + 0.5f));
}

bool themeLogoColor(const uint32_t *pixels, size_t count, uint32_t *out)
{
    uint64_t sum[3] = {0, 0, 0};
    size_t colorful = 0;
    for (size_t i = 0; pixels && i < count; i++) {
        float h, s, v;
        toHsv(pixels[i], &h, &s, &v);
        if (s < LOGO_MIN_SATURATION || v < 0.15f)
            continue;
        for (int c = 0; c < 3; c++)
            sum[c] += pixels[i] >> (c * 8) & 0xFF;
        colorful++;
    }
    if (count == 0 || colorful * 1000 < count * LOGO_MIN_SHARE)
        return false;
    *out = RGB(sum[0] / colorful, sum[1] / colorful, sum[2] / colorful);
    return true;
}

uint32_t themeAccentColor(int accent, uint32_t logo_color, bool has_logo_color)
{
    if (accent == THEME_ACCENT_FROM_LOGO)
        return has_logo_color ? themeVivid(logo_color) : COLORS[0];
    return accent >= 0 && accent < THEME_ACCENT_FROM_LOGO ? COLORS[accent] : COLORS[0];
}
