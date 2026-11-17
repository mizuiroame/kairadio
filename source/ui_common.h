// Colors, layout and small drawings shared by the views.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "font.h"
#include "gfx.h"
#include "radio.h"
#include "stations.h"

#define COLOR_BG GFX_RGB(0x12, 0x12, 0x14)
#define COLOR_SURFACE GFX_RGB(0x1C, 0x1C, 0x21)
#define COLOR_SURFACE_HI GFX_RGB(0x2C, 0x2C, 0x34)
// The accent color chosen in Settings (KaiRadio's sky blue by default); set
// at the start of each frame by uiDraw.
extern Color g_accent;
#define COLOR_ACCENT g_accent
#define COLOR_LIVE GFX_RGB(0xFF, 0x52, 0x6B)
#define COLOR_TEXT GFX_RGB(0xF2, 0xF2, 0xF5)
#define COLOR_DIM GFX_RGB(0xA4, 0xA4, 0xB0)
#define COLOR_FAINT GFX_RGB(0x5E, 0x5E, 0x6A)
#define COLOR_ERROR GFX_RGB(0xFF, 0x8A, 0x5C)

#define MARGIN 32
#define HEADER_H 64
#define SUBTITLE_Y 76
#define MINI_Y 572
#define MINI_H 92
#define FOOTER_Y 664

// A station's logo on its rounded tile; without one, its initial on a color
// of its own.
void uiDrawLogo(int x, int y, int size, const Station *station);

// A heart, filled when on, in a box of size centered on (cx, cy).
void uiDrawHeart(float cx, float cy, float size, bool on, Color color);

// A rounded label, 30 pixels high. Returns its width.
int uiPillWidth(const char *label);
void uiDrawPill(int x, int y, const char *label, Color bg, Color fg);

// "AAC · 128 kbps · 48 kHz", or what of it is known.
void uiFormatLine(const RadioStatus *radio, char *out, size_t size);

// Play (a triangle) or stop (a square) in a circle of radius r.
void uiDrawPlayButton(float cx, float cy, float r, bool playing, Color bg, Color fg);
// Skip back or forward: a triangle and a bar.
void uiDrawSkip(int cx, int cy, bool next, Color color);
// Animated bars for the station playing.
void uiDrawLiveBars(int x, int y, int h, uint64_t now_ns, Color color);
