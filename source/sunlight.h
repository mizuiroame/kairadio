// Day and night on the World view: where the sun is overhead at a moment
// (UTC, from the console's clock; no network), and how dark it is anywhere
// then. Pure logic: ui_map.c darkens the map and the globe with it.
#pragma once

#include <stdint.h>

// The twilight: the sun this many degrees above the horizon is still full
// day, this many below it is full night; between, it darkens smoothly.
#define SUN_DAY_ALTITUDE 0.8f
#define SUN_NIGHT_ALTITUDE -12.0f

// Where the sun is overhead: degrees north and east.
typedef struct {
    float lat, lon;
} SunPlace;

// The place under the sun at utc (seconds since 1970). About 0.01 degrees
// off, from the Astronomical Almanac's low-precision formulas.
SunPlace sunOverhead(int64_t utc);
// How high the sun is at (lat, lon): the sine of its altitude, 1 overhead,
// 0 on the horizon, less below it.
float sunHeight(SunPlace sun, float lat, float lon);
// How dark it is for a sun that high: 0 in day, 255 in full night.
uint8_t sunNight(float height);
// The night over a w x h equirectangular grid, 180W to 180E and north to
// south degrees, each cell's middle: out has w * h bytes, as sunNight gives.
void sunNightGrid(SunPlace sun, int w, int h, float north, float south, uint8_t *out);
