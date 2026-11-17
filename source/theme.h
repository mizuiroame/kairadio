// Accent colors: a few presets, and "From station logo", which takes a vivid
// version of the playing station's logo color (as KaiPlayer's "From cover").
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define THEME_ACCENT_COUNT 7
#define THEME_ACCENT_FROM_LOGO (THEME_ACCENT_COUNT - 1)

const char *themeAccentName(int accent);
// The accent color (0xAABBGGRR). For "From station logo" it uses logo_color
// when has_logo_color, and KaiRadio's sky blue without one.
uint32_t themeAccentColor(int accent, uint32_t logo_color, bool has_logo_color);
// A color made bright and saturated enough to stand out on the dark background.
uint32_t themeVivid(uint32_t color);
// The average of a logo's colorful pixels (count of them, 0xAABBGGRR), left
// of white, gray and black ones such as its tile; false if it has too few.
bool themeLogoColor(const uint32_t *pixels, size_t count, uint32_t *out);
