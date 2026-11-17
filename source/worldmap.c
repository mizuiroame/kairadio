#include "worldmap.h"

#include <math.h>
#include <string.h>

#define MAX_LIGHTS 14

const MapCountry *mapFindCountry(const char *code)
{
    if (!code || !code[0] || !code[1] || code[2])
        return NULL;
    char key[3] = {code[0], code[1], 0};
    for (int i = 0; i < 2; i++)
        if (key[i] >= 'a' && key[i] <= 'z')
            key[i] = (char)(key[i] - 'a' + 'A');
    size_t lo = 0, hi = MAP_COUNTRY_COUNT;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(MAP_COUNTRIES[mid].code, key);
        if (c == 0)
            return &MAP_COUNTRIES[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

void mapToLayout(MapPoint p, float *x, float *y)
{
    *x = MAP_X + p.x * (float)MAP_W / 65535.0f;
    *y = MAP_Y + p.y * (float)MAP_H / 65535.0f;
}

int mapLightCount(int stations)
{
    if (stations <= 0)
        return 0;
    // 1 station: 2 lights; 100: 7; 1,000: 10; 7,000 (the most): 12.
    int n = (int)lroundf(2.0f + 2.6f * log10f((float)stations));
    return n > MAX_LIGHTS ? MAX_LIGHTS : n;
}

float mapGlowRadius(int stations)
{
    if (stations <= 0)
        return 0;
    return 5.0f + 3.5f * log10f((float)stations);
}

// The layout position of the list's country i, if the map has it.
static bool place(const CategoryList *countries, size_t i, float *x, float *y)
{
    const MapCountry *c = mapFindCountry(countries->items[i].code);
    if (!c)
        return false;
    mapToLayout(c->label, x, y);
    return true;
}

int mapNearest(const CategoryList *countries, float x, float y)
{
    int best = -1;
    float best_d = 0;
    for (size_t i = 0; i < countries->count; i++) {
        float cx, cy;
        if (!place(countries, i, &cx, &cy))
            continue;
        float d = (cx - x) * (cx - x) + (cy - y) * (cy - y);
        if (best < 0 || d < best_d) {
            best = (int)i;
            best_d = d;
        }
    }
    return best;
}

// The best country that way within a cone of spread (how far off the
// direction per pixel along it), or -1.
static int stepWithin(const CategoryList *countries, size_t from, float fx, float fy, int dx, int dy, float spread)
{
    int best = -1;
    float best_score = 0;
    for (size_t i = 0; i < countries->count; i++) {
        float cx, cy;
        if (i == from || !place(countries, i, &cx, &cy))
            continue;
        // How far along the direction, and how far off it.
        float along = (cx - fx) * dx + (cy - fy) * dy;
        float across = fabsf((cx - fx) * dy - (cy - fy) * dx);
        if (along <= 0 || across > along * spread)
            continue;
        float score = along + across * 2.0f;
        if (best < 0 || score < best_score) {
            best = (int)i;
            best_score = score;
        }
    }
    return best;
}

int mapStep(const CategoryList *countries, size_t from, int dx, int dy)
{
    float fx, fy;
    if (from >= countries->count || !place(countries, from, &fx, &fy))
        return mapNearest(countries, MAP_X + MAP_W / 2.0f, MAP_Y + MAP_H / 2.0f);
    // Within 45 degrees of the way pressed; failing that, anything roughly that way.
    int best = stepWithin(countries, from, fx, fy, dx, dy, 1.0f);
    return best >= 0 ? best : stepWithin(countries, from, fx, fy, dx, dy, 2.5f);
}
