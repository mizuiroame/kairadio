// Accent colors: a few presets, and "From cover", which takes a vivid version
// of the playing song's cover color.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define THEME_ACCENT_COUNT 7
#define THEME_ACCENT_FROM_COVER (THEME_ACCENT_COUNT - 1)

const char *themeAccentName(int accent);
// The accent color (0xAABBGGRR). For "From cover" it uses the cover's
// vibrant color (silver for a black-and-white cover, vibrant 0) when
// has_cover, and the default pink without a cover.
uint32_t themeAccentColor(int accent, uint32_t cover_vibrant, bool has_cover);
// A cover's average color made bright and saturated enough to stand out on
// the dark background.
uint32_t themeVivid(uint32_t color);
