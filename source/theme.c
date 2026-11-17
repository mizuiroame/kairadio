#include "theme.h"

#include <math.h>

#define RGB(r, g, b) ((uint32_t)(0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r)))
#define MIN_SATURATION 0.55f
#define MIN_VALUE 0.88f
#define SILVER RGB(0xD8, 0xD8, 0xE2)  // For covers without color.

static const char *const NAMES[THEME_ACCENT_COUNT] = {
    "Pink", "Blue", "Green", "Purple", "Orange", "Red", "From cover",
};
static const uint32_t COLORS[THEME_ACCENT_COUNT - 1] = {
    RGB(0xFF, 0x6B, 0x9A), RGB(0x4D, 0xA3, 0xFF), RGB(0x3D, 0xD6, 0x8C),
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

uint32_t themeVivid(uint32_t color)
{
    float r = (float)(color & 0xFF) / 255.0f, g = (float)((color >> 8) & 0xFF) / 255.0f;
    float b = (float)((color >> 16) & 0xFF) / 255.0f;
    // To HSV, raise saturation and value, and back.
    float max = maxf(r, maxf(g, b)), min = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float delta = max - min, h = 0.0f;
    if (delta > 0.0f) {
        if (max == r)
            h = (g - b) / delta;
        else if (max == g)
            h = 2.0f + (b - r) / delta;
        else
            h = 4.0f + (r - g) / delta;
        h = h < 0.0f ? h + 6.0f : h;
    }
    float s = max > 0.0f ? delta / max : 0.0f;
    if (s < 0.08f)
        return SILVER;  // A grey color has no hue to take.
    s = maxf(s, MIN_SATURATION);
    float v = maxf(max, MIN_VALUE);
    float c = v * s, x = c * (1.0f - fabsf(fmodf(h, 2.0f) - 1.0f)), m = v - c;
    float rgb[3];
    int sector = (int)h % 6;
    switch (sector) {
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

uint32_t themeAccentColor(int accent, uint32_t cover_vibrant, bool has_cover)
{
    if (accent == THEME_ACCENT_FROM_COVER)
        return !has_cover ? COLORS[0] : cover_vibrant ? themeVivid(cover_vibrant) : SILVER;
    return accent >= 0 && accent < THEME_ACCENT_FROM_COVER ? COLORS[accent] : COLORS[0];
}
