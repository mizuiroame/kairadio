// The World view's zoom: the right stick and two fingers bring the flat map
// or the globe closer; a finger moves the zoomed flat map. The math is in
// mapzoom.c; the globe's zoom lives in its view (globe.h).
#include <math.h>

#include "buttons.h"

#include "app.h"
#include "ui_hit.h"
#include "worldmap.h"

#define PICK_MARGIN 60.0f  // How far inside the map's box a country picked with the stick is kept.

static bool onWorld(const App *app)
{
    return app->view == VIEW_WORLD && app->overlay == OVERLAY_NONE;
}

// The picked country's light on the flat map (map layout pixels). False if there's none.
static bool pickedPoint(const App *app, float *x, float *y)
{
    const CategoryList *countries = &app->categories[SEARCH_COUNTRY];
    if (app->map_cursor >= countries->count)
        return false;
    const MapCountry *c = mapFindCountry(countries->items[app->map_cursor].code);
    if (!c)
        return false;
    mapToLayout(c->label, x, y);
    return true;
}

// Zooms by factor: the globe around its middle (the country turned to), the
// flat map around the picked country (or the box's middle, if it's out of view).
static void zoomBy(App *app, float factor, float sx, float sy)
{
    if (app->config.world_globe) {
        app->globe_view.zoom = globeZoomBy(app->globe_view.zoom, factor);
        return;
    }
    mapZoomBy(&app->map_zoom, factor, sx, sy);
}

uint64_t appMapZoomButtons(App *app, uint64_t pressed)
{
    uint64_t stick = pressed & (HidNpadButton_StickRUp | HidNpadButton_StickRDown);
    if (!stick || !onWorld(app))
        return pressed;
    float sx = MAP_X + MAP_W / 2.0f, sy = MAP_Y + MAP_H / 2.0f, x, y, px, py;
    if (!app->config.world_globe && pickedPoint(app, &x, &y)) {
        mapZoomToScreen(&app->map_zoom, x, y, &px, &py);
        if (mapZoomInBox(px, py)) {
            sx = px;
            sy = py;
        }
    }
    zoomBy(app, (pressed & HidNpadButton_StickRUp) ? MAP_ZOOM_STEP : 1.0f / MAP_ZOOM_STEP, sx, sy);
    return pressed & ~stick;
}

bool appMapPinch(App *app, int count, float x0, float y0, float x1, float y1)
{
    if (count == 0) {
        app->pinching = app->touch_held = false;
        return false;
    }
    if (count < 2) {
        app->pinching = false;
        return app->touch_held;  // The finger left of two: nothing until it's up too.
    }
    float distance = hypotf(x1 - x0, y1 - y0), mx = (x0 + x1) / 2.0f, my = (y0 + y1) / 2.0f;
    bool globe = app->config.world_globe;
    if (!app->pinching) {
        if (!onWorld(app))
            return app->touch_held;  // Two fingers elsewhere: the first one goes on as it was.
        app->pinching = app->touch_held = true;
        app->pinch_distance = distance;
        app->pinch_zoom = globe ? (app->globe_view.zoom > 1.0f ? app->globe_view.zoom : 1.0f) : app->map_zoom.zoom;
        app->pinch_x = mx;
        app->pinch_y = my;
        app->globe_turning = false;
        return true;
    }
    if (!onWorld(app))
        return true;
    float zoom = mapPinchZoom(app->pinch_zoom, app->pinch_distance, distance, globe ? GLOBE_ZOOM_MAX : MAP_ZOOM_MAX);
    if (globe) {
        app->globe_view.zoom = zoom;
    } else {
        mapZoomPan(&app->map_zoom, mx - app->pinch_x, my - app->pinch_y);  // Both fingers moving: the map too.
        mapZoomBy(&app->map_zoom, zoom / app->map_zoom.zoom, mx, my);       // Around the fingers' middle.
    }
    app->pinch_x = mx;
    app->pinch_y = my;
    return true;
}

bool appMapTouch(App *app, const TouchEvent *e)
{
    if (!onWorld(app) || app->config.world_globe || app->map_zoom.zoom <= 1.0f || e->type != TOUCH_DRAG)
        return false;
    const HitRegion *start = uiHitFind(e->start_x, e->start_y);
    if (!start || start->kind != HIT_MAP)
        return false;
    mapZoomPan(&app->map_zoom, (float)e->dx, (float)e->dy);
    return true;
}

void appMapFollowPick(App *app)
{
    float x, y;
    if (!app->config.world_globe && app->map_zoom.zoom > 1.0f && pickedPoint(app, &x, &y))
        mapZoomShow(&app->map_zoom, x, y, PICK_MARGIN);
}

void appMapTapPoint(const App *app, float x, float y, float *mx, float *my)
{
    mapZoomFromScreen(&app->map_zoom, x, y, mx, my);
}
