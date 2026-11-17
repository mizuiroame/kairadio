#include "ui_common.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "logos.h"
#include "util.h"

#define PILL_TEXT 15
#define PILL_PAD 14

Color g_accent = GFX_RGB(0x4F, 0xC3, 0xF7);

// Colors for logos that can't be had, picked by the station's name.
static const Color INITIAL_COLORS[] = {
    GFX_RGB(0x3D, 0x5A, 0xFE), GFX_RGB(0x00, 0x96, 0x88), GFX_RGB(0xE9, 0x1E, 0x63),
    GFX_RGB(0xFF, 0x98, 0x00), GFX_RGB(0x8E, 0x24, 0xAA), GFX_RGB(0x43, 0xA0, 0x47),
};

static unsigned nameHash(const char *s)
{
    unsigned h = 5381;
    for (; *s; s++)
        h = h * 33 + (unsigned char)*s;
    return h;
}

void uiDrawLogo(int x, int y, int size, const Station *station)
{
    int radius = size / 6;
    const uint32_t *pixels = logosGet(station->favicon);
    if (pixels) {
        gfxBlitRounded(x, y, size, LOGO_SIZE, pixels, radius);
        return;
    }
    Color bg = INITIAL_COLORS[nameHash(station->name) % (sizeof(INITIAL_COLORS) / sizeof(INITIAL_COLORS[0]))];
    gfxFillRoundRect(x, y, size, size, radius, bg);
    // The first letter (a whole UTF-8 character).
    char initial[8] = "";
    const char *s = station->name;
    while (*s == ' ' || *s == '.' || *s == '-')
        s++;
    const char *start = s;
    utilNextCodePoint(&s);
    size_t len = (size_t)(s - start) < sizeof(initial) - 1 ? (size_t)(s - start) : sizeof(initial) - 1;
    memcpy(initial, start, len);
    initial[len] = '\0';
    if (initial[0] >= 'a' && initial[0] <= 'z')
        initial[0] = (char)(initial[0] - 'a' + 'A');
    fontDrawCentered(FONT_BOLD, size * 45 / 100, x + size / 2.0f, y + size / 2.0f, initial, COLOR_TEXT);
}

void uiDrawHeart(float cx, float cy, float size, bool on, Color color)
{
    float r = size * 0.27f;
    float lx = cx - r * 0.95f, rx = cx + r * 0.95f, top = cy - size * 0.12f;
    float tip = cy + size * 0.42f;
    if (on) {
        gfxFillCircle((int)lx, (int)top, (int)r, color);
        gfxFillCircle((int)rx, (int)top, (int)r, color);
        gfxTriangle(cx - r * 1.9f, top + r * 0.35f, cx + r * 1.9f, top + r * 0.35f, cx, tip, color);
        return;
    }
    float t = size * 0.09f;
    gfxArc(lx, top, r, t, (float)M_PI * 0.75f, (float)M_PI * 2.0f, color);
    gfxArc(rx, top, r, t, (float)M_PI, (float)M_PI * 2.25f, color);
    gfxLine(cx - r * 1.83f, top + r * 0.55f, cx, tip, t, color);
    gfxLine(cx + r * 1.83f, top + r * 0.55f, cx, tip, t, color);
}

int uiPillWidth(const char *label)
{
    return fontTextWidth(FONT_BOLD, PILL_TEXT, label) + 2 * PILL_PAD;
}

void uiDrawPill(int x, int y, const char *label, Color bg, Color fg)
{
    int w = uiPillWidth(label);
    gfxFillRoundRect(x, y, w, 30, 15, bg);
    fontDrawMiddle(FONT_BOLD, PILL_TEXT, x + PILL_PAD, (float)y + 15.0f, label, fg);
}

void uiFormatLine(const RadioStatus *radio, char *out, size_t size)
{
    const char *codec = radio->codec[0] ? radio->codec : radio->station.codec;
    char parts[3][24] = {"", "", ""};
    snprintf(parts[0], sizeof(parts[0]), "%s%s", radio->hls ? "HLS " : "", codec);
    if (radio->bitrate > 0)
        snprintf(parts[1], sizeof(parts[1]), "%d kbps", radio->bitrate);
    if (radio->sample_rate > 0) {
        if (radio->sample_rate % 1000 == 0)
            snprintf(parts[2], sizeof(parts[2]), "%u kHz", radio->sample_rate / 1000);
        else
            snprintf(parts[2], sizeof(parts[2]), "%.1f kHz", radio->sample_rate / 1000.0);
    }
    size_t o = 0;
    out[0] = '\0';
    for (int i = 0; i < 3; i++) {
        if (!parts[i][0] || strcmp(parts[i], "HLS ") == 0)
            continue;
        int n = snprintf(out + o, size - o, "%s%s", o ? " \xC2\xB7 " : "", parts[i]);
        if (n < 0 || (size_t)n >= size - o)
            break;
        o += (size_t)n;
    }
}

void uiDrawPlayButton(float cx, float cy, float r, bool playing, Color bg, Color fg)
{
    gfxFillCircle((int)cx, (int)cy, (int)r, bg);
    if (playing) {
        float s = r * 0.62f;
        gfxFillRoundRect((int)(cx - s / 2), (int)(cy - s / 2), (int)s, (int)s, (int)(r * 0.1f), fg);
    } else {
        float h = r * 0.9f;
        gfxTriangle(cx - h * 0.35f, cy - h / 2, cx - h * 0.35f, cy + h / 2, cx + h * 0.55f, cy, fg);
    }
}

void uiDrawSkip(int cx, int cy, bool next, Color color)
{
    if (next) {
        gfxFillTriangleRight(cx - 10, cy - 10, 16, 20, color);
        gfxFillRect(cx + 6, cy - 10, 4, 20, color);
    } else {
        gfxFillTriangleLeft(cx - 6, cy - 10, 16, 20, color);
        gfxFillRect(cx - 10, cy - 10, 4, 20, color);
    }
}

void uiDrawLiveBars(int x, int y, int h, uint64_t now_ns, Color color)
{
    double t = (double)(now_ns % 10000000000ull) / 1e9;
    for (int i = 0; i < 3; i++) {
        double phase = sin(t * (5.0 + i * 1.7) + i * 1.9) * 0.5 + 0.5;
        int bar = (int)(h * (0.3 + 0.7 * phase));
        gfxFillRoundRect(x + i * 7, y + h - bar, 4, bar, 2, color);
    }
}
