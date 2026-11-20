#include "ui_settings.h"

#include <stdio.h>

#include "settings.h"
#include "ui_buttons.h"
#include "ui_common.h"
#include "ui_hit.h"

#define MENU_W 440
#define MENU_ROW_H 56
#define PANEL_X 170
#define PANEL_W (GFX_WIDTH - 2 * PANEL_X)
#define PANEL_Y 22
#define PANEL_H 648  // The rows, two sections and the line about the row picked.
#define ROW_H 46
#define ABOUT_H 504
#define SECTION_H 30
#define PAD 28

// Dims the view, and makes the space around the panel close it on a tap.
static void backdrop(int x, int y, int w, int h)
{
    gfxBlendRect(0, 0, GFX_WIDTH, GFX_HEIGHT, GFX_RGB(0, 0, 0), 170);
    uiHitReset();  // The view under it takes no taps.
    uiHitAdd(0, 0, GFX_WIDTH, y, HIT_OVERLAY_OUT, 0);
    uiHitAdd(0, y + h, GFX_WIDTH, GFX_HEIGHT - y - h, HIT_OVERLAY_OUT, 0);
    uiHitAdd(0, y, x, h, HIT_OVERLAY_OUT, 0);
    uiHitAdd(x + w, y, GFX_WIDTH - x - w, h, HIT_OVERLAY_OUT, 0);
    gfxFillRoundRect(x, y, w, h, 20, COLOR_SURFACE);
}

static void highlight(int x, int y, int w, int h)
{
    gfxFillRoundRect(x, y + 2, w, h - 4, 10, COLOR_SURFACE_HI);
    gfxFillRect(x, y + 12, 3, h - 24, COLOR_ACCENT);
}

static void drawMenu(const App *app)
{
    static const char *const ITEMS[MENU_COUNT] = {"Settings", "Exit KaiRadio"};
    int h = 76 + MENU_COUNT * MENU_ROW_H + 16;
    int x = (GFX_WIDTH - MENU_W) / 2, y = (GFX_HEIGHT - h) / 2;
    backdrop(x, y, MENU_W, h);
    fontDraw(FONT_BOLD, 24, x + PAD, y + 22, "KaiRadio", COLOR_ACCENT);
    fontDraw(FONT_REGULAR, 15, x + PAD + fontTextWidth(FONT_BOLD, 24, "KaiRadio") + 12, y + 30,
             KAIRADIO_VERSION, COLOR_FAINT);
    for (int i = 0; i < MENU_COUNT; i++) {
        int row_y = y + 76 + i * MENU_ROW_H;
        if (app->menu_cursor == i)
            highlight(x + 12, row_y, MENU_W - 24, MENU_ROW_H);
        fontDraw(FONT_REGULAR, 21, x + PAD, row_y + 14, ITEMS[i], app->menu_cursor == i ? COLOR_TEXT : COLOR_DIM);
        uiHitAdd(x, row_y, MENU_W, MENU_ROW_H, HIT_MENU_ITEM, i);
    }
}

// An arrow (left or right) beside a value, and the target that steps it.
static void stepArrow(float cx, float cy, bool right, int row, Color color)
{
    float d = right ? 1.0f : -1.0f;
    gfxLine(cx - 4 * d, cy - 7, cx + 3 * d, cy, 3, color);
    gfxLine(cx + 3 * d, cy, cx - 4 * d, cy + 7, 3, color);
    uiHitAdd((int)cx - 20, (int)cy - ROW_H / 2, 40, ROW_H, HIT_SETTING_STEP, row * 2 + (right ? 1 : 0));
}

static void drawSettingRow(const App *app, int row, int x, int y, int w, uint64_t now_ns)
{
    bool picked = app->settings_cursor == row;
    if (picked)
        highlight(x, y, w, ROW_H);
    uiHitAdd(x, y, w, ROW_H, HIT_SETTING, row);
    fontDrawMiddle(FONT_REGULAR, 20, x + 18, y + ROW_H / 2.0f, settingsRowLabel(row), picked ? COLOR_TEXT : COLOR_DIM);
    float cy = y + ROW_H / 2.0f;
    if (!settingsRowEditable(row)) {  // About: a chevron, it opens.
        float cx = x + w - 24.0f;
        gfxLine(cx - 4, cy - 7, cx + 3, cy, 3, COLOR_FAINT);
        gfxLine(cx + 3, cy, cx - 4, cy + 7, 3, COLOR_FAINT);
        return;
    }
    char value[CONFIG_BACKGROUND_MAX + 16];
    appSettingValue(app, row, now_ns, value, sizeof(value));
    int max_w = w / 2 - 60, value_w = fontTextWidth(FONT_BOLD, 19, value);
    if (value_w > max_w)
        value_w = max_w;
    int right = x + w - 44, left = right - value_w;
    Color color = picked ? COLOR_ACCENT : COLOR_TEXT;
    fontDrawClipped(FONT_BOLD, 19, left, (int)cy - 13, max_w, value, color);
    stepArrow((float)left - 22, cy, false, row, picked ? COLOR_DIM : COLOR_FAINT);
    stepArrow((float)right + 22, cy, true, row, picked ? COLOR_DIM : COLOR_FAINT);
}

static void drawSettings(const App *app, uint64_t now_ns)
{
    backdrop(PANEL_X, PANEL_Y, PANEL_W, PANEL_H);
    fontDraw(FONT_BOLD, 26, PANEL_X + PAD, PANEL_Y + 20, "Settings", COLOR_TEXT);
    int x = PANEL_X + 16, w = PANEL_W - 32, y = PANEL_Y + 66;
    for (int row = 0; row < SETTING_COUNT; row++) {
        const char *section = settingsRowSection(row);
        if (section) {
            fontDraw(FONT_BOLD, 15, x + 18, y + 8, section, COLOR_FAINT);
            y += SECTION_H;
        }
        drawSettingRow(app, row, x, y, w, now_ns);
        y += ROW_H;
    }
    gfxFillRect(PANEL_X + PAD, PANEL_Y + PANEL_H - 52, PANEL_W - 2 * PAD, 1, COLOR_SURFACE_HI);
    fontDrawClipped(FONT_REGULAR, 16, PANEL_X + PAD, PANEL_Y + PANEL_H - 40, PANEL_W - 2 * PAD,
                    settingsRowHelp(app->settings_cursor), COLOR_DIM);
}

static void drawAbout(void)
{
    static const char *const LINES[] = {
        "Internet radio for Switch 2 homebrew (kaiHEN), in the spirit of KaiPlayer.",
        "",
        "Stations: Radio Browser (radio-browser.info), a community directory.",
        "World map: Natural Earth (public domain). Flags: flag-icons (MIT).",
        "Time zones and weather: Open-Meteo (open-meteo.com, CC BY 4.0).",
        "Genre icons: Lucide (ISC). Fonts: Noto Sans (SIL OFL 1.1).",
        "Audio: FFmpeg (LGPL 2.1). Also cJSON, stb and libcurl.",
        "",
        "KaiRadio is open source under the MIT License. Copyright (c) 2026 Mizuiro.",
        "github.com/mizuiroame/kairadio",
        "",
        "Not affiliated with Nintendo, Radio Browser or any station.",
    };
    int top = (GFX_HEIGHT - ABOUT_H) / 2 - 20;
    backdrop(PANEL_X, top, PANEL_W, ABOUT_H);
    int x = PANEL_X + PAD, y = top + 20;
    int title_w = fontDraw(FONT_BOLD, 26, x, y, "KaiRadio", COLOR_ACCENT);
    fontDraw(FONT_REGULAR, 18, x + title_w + 12, y + 7, KAIRADIO_VERSION, COLOR_DIM);
    y += 64;
    for (size_t i = 0; i < sizeof(LINES) / sizeof(LINES[0]); i++, y += LINES[i - 1][0] ? 34 : 18)
        fontDrawClipped(FONT_REGULAR, 18, x, y, PANEL_W - 2 * PAD, LINES[i], i == 0 ? COLOR_TEXT : COLOR_DIM);
}

void uiDrawOverlay(const App *app, uint64_t now_ns)
{
    switch (app->overlay) {
    case OVERLAY_MENU: drawMenu(app); break;
    case OVERLAY_SETTINGS: drawSettings(app, now_ns); break;
    case OVERLAY_ABOUT: drawAbout(); break;
    default: break;
    }
}
