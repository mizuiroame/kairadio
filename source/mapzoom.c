#include "mapzoom.h"

#include "worldmap.h"

#define BOX_CX (MAP_X + MAP_W / 2.0f)
#define BOX_CY (MAP_Y + MAP_H / 2.0f)

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void mapZoomReset(MapZoom *z)
{
    z->zoom = 1.0f;
    z->cx = BOX_CX;
    z->cy = BOX_CY;
}

void mapZoomClamp(MapZoom *z)
{
    if (!(z->zoom >= 1.0f))  // Also NaN.
        z->zoom = 1.0f;
    if (z->zoom > MAP_ZOOM_MAX)
        z->zoom = MAP_ZOOM_MAX;
    float half_w = MAP_W / (2.0f * z->zoom), half_h = MAP_H / (2.0f * z->zoom);
    z->cx = clampf(z->cx, MAP_X + half_w, MAP_X + MAP_W - half_w);
    z->cy = clampf(z->cy, MAP_Y + half_h, MAP_Y + MAP_H - half_h);
}

void mapZoomToScreen(const MapZoom *z, float x, float y, float *sx, float *sy)
{
    *sx = BOX_CX + (x - z->cx) * z->zoom;
    *sy = BOX_CY + (y - z->cy) * z->zoom;
}

void mapZoomFromScreen(const MapZoom *z, float sx, float sy, float *x, float *y)
{
    *x = z->cx + (sx - BOX_CX) / z->zoom;
    *y = z->cy + (sy - BOX_CY) / z->zoom;
}

void mapZoomBy(MapZoom *z, float factor, float sx, float sy)
{
    float x, y;
    mapZoomFromScreen(z, sx, sy, &x, &y);
    z->zoom = clampf(z->zoom * factor, 1.0f, MAP_ZOOM_MAX);
    // The same map point under (sx, sy): the middle moves to keep it there.
    z->cx = x - (sx - BOX_CX) / z->zoom;
    z->cy = y - (sy - BOX_CY) / z->zoom;
    mapZoomClamp(z);
}

void mapZoomPan(MapZoom *z, float dx, float dy)
{
    z->cx -= dx / z->zoom;  // The map goes with the finger.
    z->cy -= dy / z->zoom;
    mapZoomClamp(z);
}

void mapZoomShow(MapZoom *z, float x, float y, float margin)
{
    float sx, sy;
    mapZoomToScreen(z, x, y, &sx, &sy);
    if (sx < MAP_X + margin)
        z->cx -= (MAP_X + margin - sx) / z->zoom;
    else if (sx > MAP_X + MAP_W - margin)
        z->cx += (sx - (MAP_X + MAP_W - margin)) / z->zoom;
    if (sy < MAP_Y + margin)
        z->cy -= (MAP_Y + margin - sy) / z->zoom;
    else if (sy > MAP_Y + MAP_H - margin)
        z->cy += (sy - (MAP_Y + MAP_H - margin)) / z->zoom;
    mapZoomClamp(z);
}

bool mapZoomInBox(float sx, float sy)
{
    return sx >= MAP_X && sx < MAP_X + MAP_W && sy >= MAP_Y && sy < MAP_Y + MAP_H;
}

float globeZoomBy(float zoom, float factor)
{
    if (!(zoom >= 1.0f))
        zoom = 1.0f;
    return clampf(zoom * factor, 1.0f, GLOBE_ZOOM_MAX);
}

float mapPinchZoom(float start_zoom, float start_distance, float distance, float max_zoom)
{
    if (!(start_distance > 0.0f) || !(distance > 0.0f))
        return start_zoom;
    return clampf(start_zoom * distance / start_distance, 1.0f, max_zoom);
}
