// The World view's map: countries on it, lights, and moving between them.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "mapzoom.h"
#include "worldmap.h"

static Category country(const char *name, const char *code, int count)
{
    Category c;
    memset(&c, 0, sizeof(c));
    snprintf(c.name, sizeof(c.name), "%s", name);
    snprintf(c.code, sizeof(c.code), "%s", code);
    c.count = count;
    return c;
}

static void testFind(void)
{
    const MapCountry *mx = mapFindCountry("MX");
    CHECK(mx && strcmp(mx->code, "MX") == 0);
    CHECK(mapFindCountry("mx") == mx);  // The directory may send either case.
    CHECK(mapFindCountry("ZZ") == NULL && mapFindCountry("") == NULL && mapFindCountry("MEX") == NULL);
    CHECK(mapFindCountry(NULL) == NULL);
    // Sorted, so the search finds every one; and every country's points are in the table.
    size_t points = 0;
    for (size_t i = 0; i < MAP_COUNTRY_COUNT; i++) {
        CHECK(mapFindCountry(MAP_COUNTRIES[i].code) == &MAP_COUNTRIES[i]);
        CHECK(MAP_COUNTRIES[i].first == points);
        points += MAP_COUNTRIES[i].count;
    }
    // Small countries are there too, with their main light at least.
    CHECK(mapFindCountry("VA") != NULL && mapFindCountry("SG") != NULL);
}

static void testPlaces(void)
{
    float mx, my, us_x, us_y, jp_x, jp_y, br_x, br_y;
    mapToLayout(mapFindCountry("MX")->label, &mx, &my);
    mapToLayout(mapFindCountry("US")->label, &us_x, &us_y);
    mapToLayout(mapFindCountry("JP")->label, &jp_x, &jp_y);
    mapToLayout(mapFindCountry("BR")->label, &br_x, &br_y);
    CHECK(my > us_y && fabsf(mx - us_x) < 60);    // Mexico is south of the United States.
    CHECK(jp_x > MAP_X + MAP_W * 0.8f);             // Japan is far east.
    CHECK(br_y > MAP_Y + MAP_H * 0.5f && br_x > mx); // Brazil, south and east of Mexico.
    CHECK(mx >= MAP_X && mx <= MAP_X + MAP_W && my >= MAP_Y && my <= MAP_Y + MAP_H);
}

static void testLights(void)
{
    CHECK(mapLightCount(0) == 0 && mapGlowRadius(0) == 0);
    CHECK(mapLightCount(1) == 2);
    CHECK(mapLightCount(100) > mapLightCount(10) && mapLightCount(10) > mapLightCount(1));
    CHECK(mapLightCount(7000) <= 14 && mapLightCount(1000000) <= 14);
    CHECK(mapGlowRadius(5000) > mapGlowRadius(50));
}

static void testMoving(void)
{
    Category items[] = {
        country("The United States Of America", "US", 7000),
        country("Mexico", "MX", 800),
        country("Canada", "CA", 900),
        country("Japan", "JP", 700),
        country("Brazil", "BR", 600),
        country("Nowhere", "", 10),     // No code: not on the map.
        country("Atlantis", "ZZ", 5),   // A code the map hasn't.
    };
    CategoryList list = {items, sizeof(items) / sizeof(items[0])};
    CHECK(mapStep(&list, 0, 0, 1) == 1);   // Down from the US: Mexico.
    CHECK(mapStep(&list, 0, 0, -1) == 2);  // Up: Canada.
    CHECK(mapStep(&list, 1, 0, 1) == 4);   // Down from Mexico: Brazil.
    CHECK(mapStep(&list, 0, 1, 0) == 3);   // Right from the US: Japan, the only one that way.
    CHECK(mapStep(&list, 3, 1, 0) == -1);  // Nothing right of Japan.
    CHECK(mapStep(&list, 5, 1, 0) >= 0);   // From a country off the map: one near the middle.

    float x, y;
    mapToLayout(mapFindCountry("JP")->label, &x, &y);
    CHECK(mapNearest(&list, x + 5, y - 5) == 3);
    CategoryList empty = {NULL, 0};
    CHECK(mapNearest(&empty, x, y) == -1 && mapStep(&empty, 0, 1, 0) == -1);
}

static bool nearly(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

// The flat map's zoom: around a point, moved with a finger, kept on the map.
static void testZoom(void)
{
    MapZoom z;
    mapZoomReset(&z);
    float sx, sy, x, y;
    mapZoomToScreen(&z, MAP_X + 100, MAP_Y + 50, &sx, &sy);  // Not zoomed: where it is.
    CHECK(nearly(sx, MAP_X + 100) && nearly(sy, MAP_Y + 50));
    mapZoomPan(&z, 200, 0);  // Nothing to move at zoom 1.
    CHECK(nearly(z.cx, MAP_X + MAP_W / 2.0f));

    mapZoomBy(&z, 2.0f, MAP_X + 300, MAP_Y + 200);  // The point under (300, 200) stays there.
    CHECK(nearly(z.zoom, 2.0f));
    mapZoomFromScreen(&z, MAP_X + 300, MAP_Y + 200, &x, &y);
    CHECK(nearly(x, MAP_X + 300) && nearly(y, MAP_Y + 200));
    mapZoomToScreen(&z, x, y, &sx, &sy);
    CHECK(nearly(sx, MAP_X + 300) && nearly(sy, MAP_Y + 200));

    mapZoomBy(&z, 100.0f, MAP_X, MAP_Y);  // At most MAP_ZOOM_MAX,
    CHECK(nearly(z.zoom, MAP_ZOOM_MAX));
    mapZoomPan(&z, 100000, 100000);       // and never past the map's edge:
    mapZoomToScreen(&z, MAP_X, MAP_Y, &sx, &sy);
    CHECK(nearly(sx, MAP_X) && nearly(sy, MAP_Y));  // its corner at the box's corner.
    mapZoomPan(&z, -100000, -100000);
    mapZoomToScreen(&z, MAP_X + MAP_W, MAP_Y + MAP_H, &sx, &sy);
    CHECK(nearly(sx, MAP_X + MAP_W) && nearly(sy, MAP_Y + MAP_H));
    mapZoomBy(&z, 0.001f, MAP_X, MAP_Y);  // Out all the way: the whole map again.
    CHECK(nearly(z.zoom, 1.0f) && nearly(z.cx, MAP_X + MAP_W / 2.0f) && nearly(z.cy, MAP_Y + MAP_H / 2.0f));

    // A country picked far off is brought into view.
    mapZoomBy(&z, 3.0f, MAP_X, MAP_Y);
    mapZoomShow(&z, MAP_X + MAP_W - 10, MAP_Y + MAP_H - 10, 60);
    mapZoomToScreen(&z, MAP_X + MAP_W - 10, MAP_Y + MAP_H - 10, &sx, &sy);
    CHECK(mapZoomInBox(sx, sy));
    CHECK(!mapZoomInBox(MAP_X - 1, MAP_Y) && mapZoomInBox(MAP_X, MAP_Y));
    z.zoom = NAN;
    mapZoomClamp(&z);
    CHECK(nearly(z.zoom, 1.0f));

    CHECK(nearly(globeZoomBy(1.0f, 10.0f), GLOBE_ZOOM_MAX) && nearly(globeZoomBy(0.0f, 0.5f), 1.0f));
    CHECK(nearly(globeZoomBy(2.0f, 0.8f), 1.6f));
    CHECK(nearly(mapPinchZoom(1.0f, 100.0f, 250.0f, 4.0f), 2.5f));  // Fingers apart: closer,
    CHECK(nearly(mapPinchZoom(2.0f, 100.0f, 25.0f, 4.0f), 1.0f));   // together: farther, not past 1,
    CHECK(nearly(mapPinchZoom(3.0f, 0.0f, 50.0f, 4.0f), 3.0f));     // and a broken start changes nothing.
}

void testWorldMap(void)
{
    testFind();
    testPlaces();
    testLights();
    testMoving();
    testZoom();
}
