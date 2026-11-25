// The World view's globe: where places show on it, turning it, and painting it.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "globe.h"

static bool near(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

static void testProject(void)
{
    GlobeView view = {10.0f, 20.0f, 0.0f};
    float x, y, depth;
    CHECK(globeProject(view, (GeoPoint){10.0f, 20.0f}, &x, &y, &depth));  // The middle shows in the middle.
    CHECK(near(x, GLOBE_CX, 0.01f) && near(y, GLOBE_CY, 0.01f) && near(depth, 1.0f, 0.001f));
    CHECK(!globeProject(view, (GeoPoint){-170.0f, -20.0f}, &x, &y, &depth));  // The far side doesn't.
    CHECK(globeProject(view, (GeoPoint){40.0f, 20.0f}, &x, &y, &depth) && x > GLOBE_CX);  // East is right,
    CHECK(globeProject(view, (GeoPoint){10.0f, 50.0f}, &x, &y, &depth) && y < GLOBE_CY);  // north is up.
    GeoPoint back;
    CHECK(globeUnproject(view, x, y, &back) && near(back.lon, 10.0f, 0.05f) && near(back.lat, 50.0f, 0.05f));
    CHECK(globeUnproject(view, GLOBE_CX - 100, GLOBE_CY + 60, &back));
    CHECK(globeProject(view, back, &x, &y, &depth) && near(x, GLOBE_CX - 100, 0.05f) && near(y, GLOBE_CY + 60, 0.05f));
    CHECK(!globeUnproject(view, GLOBE_CX + GLOBE_R + 2, GLOBE_CY, &back));  // Off the globe.

    MapPoint corner = {0, 0}, bottom = {65535, 65535};
    GeoPoint a = globeMapPlace(corner), b = globeMapPlace(bottom);
    CHECK(near(a.lon, -180.0f, 0.01f) && near(a.lat, MAP_NORTH, 0.01f));
    CHECK(near(b.lon, 180.0f, 0.01f) && near(b.lat, MAP_SOUTH, 0.01f));
}

static void testTurn(void)
{
    CHECK(near(globeWrapLon(190.0f), -170.0f, 0.001f) && near(globeWrapLon(-181.0f), 179.0f, 0.001f));
    GlobeView v = globeDrag((GlobeView){0, 0, 0}, 100.0f, 0.0f);  // A finger to the right: the land goes right,
    CHECK(v.lon < 0.0f && near(v.lon, -100.0f * globeDegreesPerPixel((GlobeView){0, 0, 0}), 0.001f));  // the middle comes from the west.
    v = globeDrag((GlobeView){0, 0, 0}, 0.0f, 100000.0f);
    CHECK(near(v.lat, GLOBE_TILT_MAX, 0.001f));  // It doesn't turn over the pole.
    // Toward a country the short way round: from 170E to 170W is 20 degrees east.
    v = globeToward((GlobeView){170.0f, 0, 0}, (GlobeView){-170.0f, 10.0f, 0}, 0.5f);
    CHECK(near(v.lon, 180.0f, 0.01f) || near(v.lon, -180.0f, 0.01f));
    CHECK(near(v.lat, 5.0f, 0.01f));
}

static void testPaint(void)
{
    enum { SIZE = 64, LAND_W = 36, LAND_H = 14 };
    GlobeTable t;
    memset(&t, 0, sizeof(t));
    CHECK(globeTableBuild(&t, SIZE, 0.0f, LAND_H) && t.size == SIZE);
    uint32_t land[LAND_W * LAND_H];  // Its coverage as alpha; red terrain on it.
    for (int i = 0; i < LAND_W * LAND_H; i++)
        land[i] = (GFX_RGB(220, 0, 0) & 0xFFFFFFu) | ((i % LAND_W) < LAND_W / 2 ? 0xFF000000u : 0);  // Land west, sea east.
    uint32_t *out = malloc(SIZE * SIZE * sizeof(uint32_t));
    GlobeColors colors = {GFX_RGB(0, 0, 200), GFX_RGB(0, 200, 0), GFX_RGB(200, 200, 200), GFX_RGB(100, 150, 250), NULL, false, 0, 0, 0.0f};
    globeRender(&t, 0.0f, land, LAND_W, &colors, out);
    CHECK(out[0] >> 24 == 0);                            // A corner: nothing.
    uint32_t west = out[SIZE / 2 * SIZE + SIZE / 2 - 12], east = out[SIZE / 2 * SIZE + SIZE / 2 + 12];
    CHECK((west >> 24) == 255 && ((west >> 8) & 0xFF) > (west & 0xFF0000) >> 16);  // Green land west of 0,
    CHECK((east >> 24) == 255 && ((east >> 16) & 0xFF) > ((east >> 8) & 0xFF));      // blue sea east of it.
    int edge = (int)(SIZE / 2 * GLOBE_DISC) + 2;                                      // Just past the edge: the air.
    uint32_t air = out[SIZE / 2 * SIZE + SIZE / 2 + edge];
    CHECK(t.air[SIZE / 2 * SIZE + SIZE / 2 + edge] && (air >> 24) > 0 && (air >> 24) < 255);
    globeRender(&t, 180.0f, land, LAND_W, &colors, out);                      // Turned halfway round:
    east = out[SIZE / 2 * SIZE + SIZE / 2 + 12];
    CHECK(((east >> 8) & 0xFF) > ((east >> 16) & 0xFF));                              // land now east of the middle.
    // Night over the east half of the land's grid: the globe darker there, the same elsewhere.
    uint8_t night[LAND_W * LAND_H];
    for (int i = 0; i < LAND_W * LAND_H; i++)
        night[i] = (i % LAND_W) >= LAND_W / 2 ? 255 : 0;
    globeRender(&t, 0.0f, land, LAND_W, &colors, out);
    uint32_t day_east = out[SIZE / 2 * SIZE + SIZE / 2 + 12], day_west = out[SIZE / 2 * SIZE + SIZE / 2 - 12];
    uint32_t pole = out[3 * SIZE + SIZE / 2];  // North of the map: its top row's night.
    colors.night = night;
    globeRender(&t, 0.0f, land, LAND_W, &colors, out);
    uint32_t night_east = out[SIZE / 2 * SIZE + SIZE / 2 + 12];
    CHECK(((night_east >> 16) & 0xFF) < ((day_east >> 16) & 0xFF) && ((night_east >> 16) & 0xFF) > 0);
    uint32_t lit_west = out[SIZE / 2 * SIZE + SIZE / 2 - 12];  // In the day: a little brighter than with no night.
    CHECK(((lit_west >> 8) & 0xFF) > ((day_west >> 8) & 0xFF));
    CHECK((out[3 * SIZE + SIZE / 2] >> 24) == (pole >> 24));
    colors.night = NULL;
    // The terrain's colors in place of the land's: red land west of 0.
    colors.terrain = true;
    globeRender(&t, 0.0f, land, LAND_W, &colors, out);
    uint32_t red = out[SIZE / 2 * SIZE + SIZE / 2 - 12];
    CHECK((red & 0xFF) > ((red >> 8) & 0xFF) && out[SIZE / 2 * SIZE + SIZE / 2 + 12] == day_east);  // The sea as it was.
    colors.terrain = false;
    CHECK(globeTableCovers(&t, SIZE, 0.0f, LAND_H, 0, 0, SIZE, SIZE) && !globeTableCovers(&t, SIZE, 1.0f, LAND_H, 0, 0, 4, 4));
    CHECK(!globeTableCovers(&t, SIZE, 0.0f, LAND_H * 2, 0, 0, 4, 4));  // Made for another land.
    // A table made only in part covers only that part, keeps only it, and paints the same there.
    GlobeTable part;
    memset(&part, 0, sizeof(part));
    CHECK(globeTableBuildPart(&part, SIZE, 0.0f, LAND_H, 16, 16, 48, 48) && part.cap == 32 * 32);
    CHECK(globeTableCovers(&part, SIZE, 0.0f, LAND_H, 20, 20, 40, 40) && !globeTableCovers(&part, SIZE, 0.0f, LAND_H, 0, 0, 40, 40));
    size_t middle = SIZE / 2 * SIZE + SIZE / 2;
    CHECK(part.row[globeTableAt(&part, SIZE / 2, SIZE / 2)] == t.row[middle]);
    CHECK(part.alpha[globeTableAt(&part, SIZE / 2, SIZE / 2)] == t.alpha[middle]);
    uint32_t *small = malloc(32 * 32 * sizeof(uint32_t));
    globeRender(&part, 0.0f, land, LAND_W, &colors, small);  // Its part only, 32 across.
    CHECK(small[(SIZE / 2 - 16) * 32 + SIZE / 2 - 16] == out[middle]);
    CHECK(globeTableBuildPart(&part, SIZE, 0.0f, LAND_H, -9, -9, 999, 999) && part.x0 == 0 && part.x1 == SIZE);
    CHECK(part.row[middle] == t.row[middle] && part.alpha[middle] == t.alpha[middle]);
    // Made a few rows at a time: not covering until it's all made, then the same.
    CHECK(globeTableStart(&part, SIZE, 0.0f, LAND_H, 0, 0, SIZE, SIZE) && part.made == 0);
    CHECK(!globeTableCovers(&part, SIZE, 0.0f, LAND_H, 0, 0, 4, 4));
    CHECK(!globeTableMake(&part, 1) && part.made == 1);  // A row at least.
    CHECK(!globeTableMake(&part, SIZE * 10) && part.made == 11);
    while (!globeTableMake(&part, SIZE * 10))
        ;
    CHECK(part.made == SIZE && globeTableCovers(&part, SIZE, 0.0f, LAND_H, 0, 0, SIZE, SIZE));
    CHECK(memcmp(part.shade, t.shade, SIZE * SIZE) == 0 && memcmp(part.row, t.row, SIZE * SIZE * sizeof(int16_t)) == 0);
    globeTableFree(&part);
    free(small);
    // Only a part painted, its rows stride apart from (x0, y0): the rest is left as it was.
    memset(out, 0xAB, SIZE * SIZE * sizeof(uint32_t));
    globeRenderPart(&t, 0.0f, land, LAND_W, &colors, out + 16 * SIZE + 16, SIZE, 16, 16, 48, 48);
    CHECK(out[0] == 0xABABABABu && out[SIZE * SIZE - 1] == 0xABABABABu);
    CHECK(out[SIZE / 2 * SIZE + SIZE / 2] != 0xABABABABu && out[15 * SIZE + 32] == 0xABABABABu);
    uint32_t whole = out[middle];
    globeRenderPart(&t, 0.0f, land, LAND_W, &colors, out, SIZE, 0, 0, SIZE, SIZE);
    CHECK(out[middle] == whole && out[0] >> 24 == 0);
    globeTableFree(&t);
    free(out);
}

// Zoomed, the globe is bigger around its middle, and a finger turns it less.
static void testZoom(void)
{
    GlobeView flat = {10.0f, 20.0f, 0.0f}, close = {10.0f, 20.0f, 2.0f};
    CHECK(near(globeRadius(flat), GLOBE_R, 0.01f) && near(globeRadius(close), 2.0f * GLOBE_R, 0.01f));
    float x1, y1, x2, y2, depth;
    GeoPoint east = {25.0f, 20.0f};
    CHECK(globeProject(flat, east, &x1, &y1, &depth) && globeProject(close, east, &x2, &y2, &depth));
    CHECK(near(x2 - GLOBE_CX, 2.0f * (x1 - GLOBE_CX), 0.05f));
    GeoPoint back;
    CHECK(globeUnproject(close, x2, y2, &back) && near(back.lon, 25.0f, 0.05f));
    CHECK(globeUnproject(close, GLOBE_CX + GLOBE_R + 2, GLOBE_CY, &back));  // Past the small globe's edge: still on it.
    CHECK(near(globeDegreesPerPixel(close), globeDegreesPerPixel(flat) / 2.0f, 0.0001f));
    GlobeView turned = globeToward(close, (GlobeView){30.0f, 0.0f, 0.0f}, 0.5f);
    CHECK(near(turned.zoom, 2.0f, 0.001f));  // Turning keeps the zoom.
}

// The land at another detail than the night's, and terrain colors made darker with a gain to undo it.
static void testFineLand(void)
{
    enum { SIZE = 64, LAND_W = 72, LAND_H = 28, NIGHT_W = 36, NIGHT_H = 14 };
    GlobeTable t;
    memset(&t, 0, sizeof(t));
    CHECK(globeTableBuild(&t, SIZE, 0.0f, LAND_H));
    uint8_t night[NIGHT_W * NIGHT_H];
    uint32_t terrain[LAND_W * LAND_H], dim[LAND_W * LAND_H];
    for (int i = 0; i < LAND_W * LAND_H; i++) {
        terrain[i] = GFX_RGB(200, 100, 40);
        dim[i] = GFX_RGB(160, 80, 32);  // 0.8 of it.
    }
    for (int i = 0; i < NIGHT_W * NIGHT_H; i++)
        night[i] = (i % NIGHT_W) >= NIGHT_W / 2 ? 255 : 0;  // Night east of 0, on its own coarser grid.
    uint32_t a[SIZE * SIZE], b[SIZE * SIZE];
    GlobeColors colors = {GFX_RGB(0, 0, 200), GFX_RGB(0, 200, 0), GFX_RGB(200, 200, 200), GFX_RGB(100, 150, 250),
                          night, true, NIGHT_W, NIGHT_H, 0.0f};
    globeRender(&t, 0.0f, terrain, LAND_W, &colors, a);
    size_t west = SIZE / 2 * SIZE + SIZE / 2 - 12, east = SIZE / 2 * SIZE + SIZE / 2 + 12;
    CHECK((a[east] & 0xFF) < (a[west] & 0xFF));  // The night where its grid has it.
    colors.terrain_gain = 1.25f;
    globeRender(&t, 0.0f, dim, LAND_W, &colors, b);
    for (int c = 0; c < 24; c += 8) {  // The same colors, within rounding.
        int d = (int)((a[west] >> c) & 0xFF) - (int)((b[west] >> c) & 0xFF);
        CHECK(d >= -3 && d <= 3);
    }
    globeTableFree(&t);
}

// The picture's size by zoom: in steps, and within a budget of pixels.
static void testImageSize(void)
{
    CHECK(globeImageSize(1.0f, GLOBE_IMAGE, false) == GLOBE_IMAGE && globeImageSize(1.0f, GLOBE_IMAGE, true) == GLOBE_IMAGE);
    CHECK(globeImageSize(0.9999f, GLOBE_IMAGE, false) == GLOBE_IMAGE);  // A hair off a step is on it.
    CHECK(globeImageSize(1.05f, GLOBE_IMAGE, false) == GLOBE_IMAGE);    // A little more: the step below,
    int up = globeImageSize(1.05f, GLOBE_IMAGE, true);                   // or the one above.
    CHECK(up > GLOBE_IMAGE && up < GLOBE_IMAGE * 5 / 4 && up % 2 == 0);
    CHECK(globeImageSize(1.2f, GLOBE_IMAGE, false) == up);
    CHECK(globeImageSize(2.0f, GLOBE_IMAGE, false) == 2 * GLOBE_IMAGE);
    CHECK(globeImageSize(0.0f, 100.0f, true) == 16 && globeImageSize(NAN, 100.0f, false) == 16);
    CHECK(near(globeImageScale(1.5f, 1000.0f, 1e9f), 1.5f, 0.0001f));       // As fine as asked, under budget,
    CHECK(near(globeImageScale(1.5f, 1e6f, 250000.0f), 0.5f, 0.0001f));     // or as the budget allows.
    CHECK(near(globeImageScale(1.5f, 0.0f, 1000.0f), 1.5f, 0.0001f));
}

void testGlobe(void)
{
    testFineLand();
    testImageSize();
    testProject();
    testTurn();
    testPaint();
    testZoom();
}
