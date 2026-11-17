#include "ui_hit.h"

#include <stddef.h>

static HitRegion g_regions[HIT_MAX_REGIONS];
static int g_count;

void uiHitReset(void)
{
    g_count = 0;
}

void uiHitAdd(int x, int y, int w, int h, HitKind kind, int arg)
{
    if (g_count < HIT_MAX_REGIONS && w > 0 && h > 0)
        g_regions[g_count++] = (HitRegion){x, y, w, h, kind, arg};
}

int uiHitCount(void)
{
    return g_count;
}

const HitRegion *uiHitAt(int index)
{
    return index >= 0 && index < g_count ? &g_regions[index] : NULL;
}

const HitRegion *uiHitFind(int x, int y)
{
    for (int i = g_count - 1; i >= 0; i--) {  // Added later = drawn on top.
        const HitRegion *r = &g_regions[i];
        if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h)
            return r;
    }
    return NULL;
}
