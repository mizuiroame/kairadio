// The land's colors: sampling between pixels, painting a picture of the map
// (its coverage kept), and the real data/world_terrain.jpg read back.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "terrain.h"
#include "worldmap.h"

#define R(c) ((c) & 0xFF)
#define G(c) (((c) >> 8) & 0xFF)
#define B(c) (((c) >> 16) & 0xFF)

static void testSample(void)
{
    // Two pixels across, one down: red on the left, blue on the right.
    uint8_t rgb[] = {200, 0, 0, 0, 0, 200};
    Terrain t = {rgb, 2, 1};
    Color left = terrainColor(&t, 0.25f, 0.5f, 1.0f), right = terrainColor(&t, 0.75f, 0.5f, 1.0f);
    CHECK(R(left) == 200 && B(left) == 0 && R(right) == 0 && B(right) == 200);  // Pixel middles: as they are.
    Color middle = terrainColor(&t, 0.5f, 0.5f, 1.0f);
    CHECK(R(middle) == 100 && B(middle) == 100);                                // Between: blended.
    CHECK(R(terrainColor(&t, -1.0f, 2.0f, 1.0f)) == 200);                       // Past the edge: the edge's.
    CHECK(R(terrainColor(&t, 0.25f, 0.5f, 0.5f)) == 100);                       // Dimmed,
    CHECK(R(terrainColor(&t, 0.25f, 0.5f, 2.0f)) == 255);                       // and brighter, kept in range.
    Terrain none = {NULL, 0, 0};
    CHECK(terrainColor(&none, 0.5f, 0.5f, 1.0f) == GFX_RGB(0, 0, 0));

    uint32_t picture[4] = {0x80FFFFFFu, 0x00FFFFFFu, 0xFF000000u, 0x11000000u};
    terrainPaint(&t, picture, 4, 1, 1.0f);
    CHECK(picture[0] >> 24 == 0x80 && picture[1] >> 24 == 0 && picture[2] >> 24 == 0xFF);  // The coverage kept,
    CHECK(R(picture[0]) == 200 && B(picture[3]) == 200);                                     // the colors the terrain's.
    // A few rows at a time: only those, cut to the picture.
    uint32_t rows[2 * 3] = {0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u};
    terrainPaintRows(&t, rows, 2, 3, 1, 2, 1.0f);
    CHECK(rows[0] == 0x80000000u && rows[4] == 0x80000000u && rows[2] != 0x80000000u && rows[2] >> 24 == 0x80);
    terrainPaintRows(&t, rows, 2, 3, -4, 99, 1.0f);
    CHECK(rows[0] != 0x80000000u && rows[5] != 0x80000000u);
}

// The color at a place (degrees) on the map.
static Color at(const Terrain *t, float lon, float lat)
{
    return terrainColor(t, (lon + 180.0f) / 360.0f, (MAP_NORTH - lat) / (MAP_NORTH - MAP_SOUTH), 1.0f);
}

static void testData(void)
{
    FILE *f = fopen("../data/world_terrain.jpg", "rb");
    CHECK(f != NULL);
    if (!f)
        return;
    static uint8_t jpg[1 << 20];
    size_t size = fread(jpg, 1, sizeof(jpg), f);
    fclose(f);
    Terrain t;
    CHECK(terrainLoad(&t, jpg, size) && t.w == 2048 && t.h == 791);
    Color sahara = at(&t, 10.0f, 23.0f), amazon = at(&t, -62.0f, -4.0f), greenland = at(&t, -40.0f, 75.0f);
    CHECK(R(sahara) > B(sahara) + 8);                      // Desert: tan,
    CHECK(G(amazon) > R(amazon) && G(amazon) > B(amazon));  // rainforest: green,
    CHECK(B(greenland) > 120 && B(greenland) >= R(greenland) - 10);  // ice: pale.
    CHECK(!terrainLoad(&(Terrain){0}, jpg, 100));  // Cut short: can't be read.
    terrainFree(&t);
    CHECK(t.rgb == NULL);
}

void testTerrain(void)
{
    testSample();
    testData();
}
