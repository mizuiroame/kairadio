// Day and night: where the sun is overhead at known moments (solstices,
// equinoxes, the equation of time's extremes), and the night it makes.
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include "check.h"
#include "sunlight.h"

static bool near(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

static void testOverhead(void)
{
    // The solstices: the sun over the tropics, 23.44 degrees north or south.
    SunPlace june = sunOverhead(1718916660);  // 2024-06-20 20:51 UTC.
    CHECK(near(june.lat, 23.44f, 0.05f));
    // At 20:51 UTC the sun is past noon by 8.85 hours (the equation of time, -1.6 min): over 132.4 W.
    CHECK(near(june.lon, -132.4f, 0.5f));
    SunPlace december = sunOverhead(1734772800);  // 2024-12-21 09:20 UTC.
    CHECK(near(december.lat, -23.44f, 0.05f));
    // The equinoxes: over the equator.
    CHECK(near(sunOverhead(1710903960).lat, 0.0f, 0.05f));  // 2024-03-20 03:06 UTC.
    CHECK(near(sunOverhead(1727009040).lat, 0.0f, 0.05f));  // 2024-09-22 12:44 UTC.
    // Noon UTC: the sun is near Greenwich, off by the equation of time.
    CHECK(near(sunOverhead(1710936000).lon, 1.85f, 0.4f));   // 2024-03-20, the sun 7.4 min late.
    CHECK(near(sunOverhead(1707652800).lon, 3.55f, 0.4f));   // 2024-02-11, 14.2 min late.
    CHECK(near(sunOverhead(1730635200).lon, -4.1f, 0.4f));   // 2024-11-03, 16.4 min early.
    CHECK(sunOverhead(0).lon >= -180.0f && sunOverhead(0).lon < 180.0f);
}

static void testNight(void)
{
    SunPlace june = sunOverhead(1718916660);
    CHECK(near(sunHeight(june, june.lat, june.lon), 1.0f, 0.001f));  // Overhead.
    CHECK(sunNight(sunHeight(june, 89.0f, 0.0f)) == 0);              // The midnight sun at the North Pole,
    CHECK(sunNight(sunHeight(june, -89.0f, 0.0f)) == 255);           // the polar night at the South Pole.
    CHECK(sunNight(sunHeight(june, 34.0f, -118.0f)) == 0);           // Los Angeles: early afternoon.
    CHECK(sunNight(sunHeight(june, 35.7f, 139.7f)) == 0);            // Tokyo: 05:51, the sun up since 04:25,
    CHECK(sunNight(sunHeight(june, 28.6f, 77.2f)) == 255);           // Delhi: 02:21,
    uint8_t berlin = sunNight(sunHeight(june, 52.5f, 13.4f));        // Berlin: 22:51, the sun 9 degrees down,
    CHECK(berlin > 128 && berlin < 255);                              // its long twilight not yet full night.
    // The twilight darkens smoothly from day to night.
    CHECK(sunNight(1.0f) == 0 && sunNight(-1.0f) == 255);
    CHECK(sunNight(0.0f) > 0 && sunNight(0.0f) < 64);
    CHECK(sunNight(-0.1f) > sunNight(0.0f) && sunNight(-0.1f) < 255);

    // The grid: day under the sun, night opposite it, the same as one place at a time.
    enum { W = 72, H = 36 };
    uint8_t *grid = malloc(W * H);
    SunPlace equinox = {0.0f, 0.0f};
    sunNightGrid(equinox, W, H, 90.0f, -90.0f, grid);
    CHECK(grid[(H / 2) * W + W / 2] == 0);    // 0 E on the equator: noon.
    CHECK(grid[(H / 2) * W + 0] == 255);      // 180: midnight.
    CHECK(grid[(H / 2) * W + W - 1] == 255);
    float lat = 90.0f - 10.5f * 180.0f / H, lon = -180.0f + 30.5f * 360.0f / W;
    CHECK(grid[10 * W + 30] == sunNight(sunHeight(equinox, lat, lon)));
    free(grid);
}

void testSunlight(void)
{
    testOverhead();
    testNight();
}
