#include "sunlight.h"

#include <math.h>
#include <stdlib.h>

#define RAD_D (3.14159265358979 / 180.0)
#define RAD_F (3.14159265f / 180.0f)
#define J2000_UNIX_DAYS 10957.5  // 2000-01-01 12:00 UTC, in days since 1970.

static double wrap360(double deg)
{
    deg = fmod(deg, 360.0);
    return deg < 0 ? deg + 360.0 : deg;
}

SunPlace sunOverhead(int64_t utc)
{
    double n = (double)utc / 86400.0 - J2000_UNIX_DAYS;  // Days from J2000.0.
    double mean_lon = wrap360(280.460 + 0.9856474 * n), anomaly = wrap360(357.528 + 0.9856003 * n) * RAD_D;
    double ecliptic = (mean_lon + 1.915 * sin(anomaly) + 0.020 * sin(2.0 * anomaly)) * RAD_D;
    double tilt = (23.439 - 0.0000004 * n) * RAD_D;
    double right_ascension = atan2(cos(tilt) * sin(ecliptic), cos(ecliptic)) / RAD_D;
    double declination = asin(sin(tilt) * sin(ecliptic)) / RAD_D;
    double sidereal = wrap360(280.46061837 + 360.98564736629 * n);  // Greenwich's, degrees.
    double lon = wrap360(right_ascension - sidereal + 180.0) - 180.0;
    SunPlace sun = {(float)declination, (float)lon};
    return sun;
}

float sunHeight(SunPlace sun, float lat, float lon)
{
    float a = lat * RAD_F, d = sun.lat * RAD_F;
    return sinf(a) * sinf(d) + cosf(a) * cosf(d) * cosf((lon - sun.lon) * RAD_F);
}

uint8_t sunNight(float height)
{
    static float day, night;
    if (day == 0.0f) {
        day = sinf(SUN_DAY_ALTITUDE * RAD_F);
        night = sinf(SUN_NIGHT_ALTITUDE * RAD_F);
    }
    if (height >= day)
        return 0;
    if (height <= night)
        return 255;
    float t = (day - height) / (day - night);
    return (uint8_t)lroundf(255.0f * t * t * (3.0f - 2.0f * t));  // Smooth at both ends.
}

void sunNightGrid(SunPlace sun, int w, int h, float north, float south, uint8_t *out)
{
    if (w <= 0 || h <= 0)
        return;
    float *cos_lon = malloc((size_t)w * sizeof(float));  // Across: the same for every row.
    float d = sun.lat * RAD_F;
    for (int x = 0; cos_lon && x < w; x++)
        cos_lon[x] = cosf((-180.0f + (x + 0.5f) * 360.0f / w - sun.lon) * RAD_F);
    for (int y = 0; y < h; y++) {
        float lat = north - (y + 0.5f) * (north - south) / h;
        float a = lat * RAD_F, along = sinf(a) * sinf(d), across = cosf(a) * cosf(d);
        uint8_t *row = out + (size_t)y * w;
        for (int x = 0; x < w; x++)
            row[x] = sunNight(cos_lon ? along + across * cos_lon[x]
                                      : sunHeight(sun, lat, -180.0f + (x + 0.5f) * 360.0f / w));
    }
    free(cos_lon);
}
