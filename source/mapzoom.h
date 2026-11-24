// The World view's zoom: how close the flat map or the globe is, and (on the
// flat map) which part of it shows. Pure logic over layout pixels: app_map.c
// moves it with two fingers, the right stick and a finger; ui_map.c draws with it.
#pragma once

#include <stdbool.h>

#define MAP_ZOOM_MAX 4.0f     // The flat map: Europe's and the Caribbean's countries apart.
#define GLOBE_ZOOM_MAX 2.5f   // The globe: closer still would be only a few countries.
#define MAP_ZOOM_STEP 1.25f   // One push of the right stick.

// The flat map's view: zoom (1 is the whole map) and the map's layout point
// in the middle of the map's box.
typedef struct {
    float zoom;
    float cx, cy;
} MapZoom;

// The whole map.
void mapZoomReset(MapZoom *z);
// Zoom kept within [1, MAP_ZOOM_MAX] and the middle where the map fills its box.
void mapZoomClamp(MapZoom *z);
// A map layout point (as mapToLayout gives) to where it shows on screen, and back.
void mapZoomToScreen(const MapZoom *z, float x, float y, float *sx, float *sy);
void mapZoomFromScreen(const MapZoom *z, float sx, float sy, float *x, float *y);
// Zooms by factor, the map's point under screen (sx, sy) staying there.
void mapZoomBy(MapZoom *z, float factor, float sx, float sy);
// Moves the map with a finger: dx, dy screen pixels.
void mapZoomPan(MapZoom *z, float dx, float dy);
// Moves the middle the least so map point (x, y) shows inside the box, margin pixels in.
void mapZoomShow(MapZoom *z, float x, float y, float margin);
// True when screen point (sx, sy) is inside the map's box.
bool mapZoomInBox(float sx, float sy);

// The globe's zoom by factor, kept within [1, GLOBE_ZOOM_MAX].
float globeZoomBy(float zoom, float factor);

// Two fingers: their distance when they came down and the zoom then; the
// zoom now for their distance now. 0 distances leave it as it was.
float mapPinchZoom(float start_zoom, float start_distance, float distance, float max_zoom);
