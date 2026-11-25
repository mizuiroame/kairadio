// The land's colors by height and climate (data/world_terrain.jpg, made by
// tools/make_terrain.py from Natural Earth's hypsometric tints), over the same
// equirectangular part of the world as the map. ui_map.c paints the flat map's
// land and the globe's with them; where the land ends stays the coverage's.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

typedef struct {
    uint8_t *rgb;  // w x h, 3 bytes a pixel.
    int w, h;
} Terrain;

// Decodes the JPEG. False if it can't be read (t is then empty).
bool terrainLoad(Terrain *t, const uint8_t *jpg, size_t size);
void terrainFree(Terrain *t);
// The color at (u, v), 0-1 across and down the map, blended between its
// four nearest pixels, times brightness (1 as it is).
Color terrainColor(const Terrain *t, float u, float v, float brightness);
// Gives each pixel of a w x h picture of the map the terrain's color there
// (times brightness), keeping its alpha (the land's coverage).
void terrainPaint(const Terrain *t, uint32_t *pixels, int w, int h, float brightness);
// The same, only its rows y0 to y1 (not included, cut to the picture): a big
// picture can be painted a few rows a frame.
void terrainPaintRows(const Terrain *t, uint32_t *pixels, int w, int h, int y0, int y1, float brightness);
