#include "ui_settings.h"

#include <stdio.h>
#include <string.h>

#include "lang.h"
#include "settings.h"
#include "ui_buttons.h"
#include "ui_common.h"
#include "ui_hit.h"

#define MENU_W 640  // The + menu, as KaiVideo's and KaiPlayer's.
#define MENU_ROW_H 48
#define SETTINGS_W 760  // Settings, as KaiVideo's.
#define ROW_H 48
#define HEAD_H 76
#define FOOT_H 76  // The row's help, on up to two lines.
#define HELP_PX 16
#define HELP_LINE_H 24
#define VALUE_MAX_W 300
#define ABOUT_X 170
#define ABOUT_W (GFX_WIDTH - 2 * ABOUT_X)
#define ABOUT_H 504
#define PAD 32

// Dims the view, and makes the space around the panel close it on a tap.
static void backdrop(int x, int y, int w, int h)
{
    gfxBlendRect(0, 0, GFX_WIDTH, GFX_HEIGHT, GFX_RGB(0, 0, 0), 170);
    uiHitReset();  // The view under it takes no taps.
    uiHitAdd(0, 0, GFX_WIDTH, y, HIT_OVERLAY_OUT, 0);
    uiHitAdd(0, y + h, GFX_WIDTH, GFX_HEIGHT - y - h, HIT_OVERLAY_OUT, 0);
    uiHitAdd(0, y, x, h, HIT_OVERLAY_OUT, 0);
    uiHitAdd(x + w, y, GFX_WIDTH - x - w, h, HIT_OVERLAY_OUT, 0);
    gfxFillRoundRect(x, y, w, h, 16, COLOR_SURFACE);
}

static void highlight(int x, int y, int w, int h)
{
    gfxFillRoundRect(x, y, w, h, 10, COLOR_SURFACE_HI);
    gfxFillRect(x, y + 10, 3, h - 20, COLOR_ACCENT);
}

// The + menu (the app's name and version over its items), or the exit question.
static void drawMenu(const App *app)
{
    static const char *const ITEMS[MENU_COUNT] = {N_("Settings"), N_("Exit KaiRadio")};
    static const char *const ANSWERS[EXIT_COUNT] = {N_("Exit"), N_("Cancel")};
    bool asking = app->overlay == OVERLAY_EXIT;
    int count = asking ? EXIT_COUNT : MENU_COUNT;
    int h = 76 + count * MENU_ROW_H + 20;
    int x = (GFX_WIDTH - MENU_W) / 2, y = (GFX_HEIGHT - h) / 2;
    backdrop(x, y, MENU_W, h);
    if (asking) {
        fontDraw(FONT_BOLD, 22, x + PAD, y + 24, T("Exit KaiRadio?"), COLOR_TEXT);
    } else {
        int title_w = fontDraw(FONT_BOLD, 22, x + PAD, y + 24, "KaiRadio", COLOR_ACCENT);
        fontDraw(FONT_REGULAR, 15, x + PAD + title_w + 12, y + 31, KAIRADIO_VERSION, COLOR_FAINT);
    }
    for (int i = 0; i < count; i++) {
        int row_y = y + 76 + i * MENU_ROW_H;
        bool picked = app->menu_cursor == i;
        if (picked)
            highlight(x + 16, row_y, MENU_W - 32, MENU_ROW_H - 6);
        fontDrawClipped(picked ? FONT_BOLD : FONT_REGULAR, 21, x + PAD, row_y + 9, MENU_W - 2 * PAD,
                        T(asking ? ANSWERS[i] : ITEMS[i]), COLOR_TEXT);
        uiHitAdd(x + 16, row_y, MENU_W - 32, MENU_ROW_H - 6, HIT_MENU_ITEM, i);
    }
}

// "Back" with an arrow, left of a page's title. Returns its width.
static int backButton(int x, int y)
{
    const char *label = T("Back");
    int w = fontTextWidth(FONT_BOLD, 17, label) + 44;
    float mid = (float)y + 15.0f;
    gfxFillRoundRect(x, y, w, 30, 15, COLOR_SURFACE_HI);
    gfxTriangle((float)x + 12.0f, mid, (float)x + 21.0f, mid - 6.5f, (float)x + 21.0f, mid + 6.5f, COLOR_TEXT);
    fontDrawMiddle(FONT_BOLD, 17, x + 28, mid, label, COLOR_TEXT);
    uiHitAdd(x - 8, y - 10, w + 16, 50, HIT_SETTINGS_BACK, 0);
    return w;
}

static void drawSettingRow(const App *app, SettingRow row, int x, int y, uint64_t now_ns)
{
    bool picked = app->settings_cursor == (int)row, choice = settingsRowEditable(row);
    SettingsPage page;
    bool opens = settingOpensPage(row, &page);
    int w = SETTINGS_W - 32, h = ROW_H - 6;
    if (picked)
        highlight(x + 16, y, w, h);
    uiHitAdd(x + 16, y, w, h, HIT_SETTING, row);
    fontDraw(picked ? FONT_BOLD : FONT_REGULAR, 21, x + PAD, y + 8, settingsRowLabel(row), COLOR_TEXT);
    char value[CONFIG_BACKGROUND_MAX + 128];
    appSettingValue(app, row, now_ns, value, sizeof(value));
    int value_w = fontTextWidth(FONT_BOLD, 20, value);
    if (value_w > VALUE_MAX_W)
        value_w = VALUE_MAX_W;
    int right = x + SETTINGS_W - PAD;
    int value_x = right - value_w - (choice || opens ? 24 : 0);
    if (value[0])
        fontDrawClipped(FONT_BOLD, 20, value_x, y + 9, value_w, value, choice ? COLOR_ACCENT : COLOR_DIM);
    if (opens) {  // A page: an arrow into it.
        gfxFillTriangleRight(right - 12, y + 13, 10, 16, picked ? COLOR_TEXT : COLOR_DIM);
    } else if (choice) {  // Arrows to step it; a tap on either half of the value steps it that way.
        gfxFillTriangleLeft(value_x - 22, y + 13, 10, 16, COLOR_DIM);
        gfxFillTriangleRight(right - 12, y + 13, 10, 16, COLOR_DIM);
        uiHitAdd(value_x - 40, y, value_w / 2 + 40, h, HIT_SETTING_STEP, row * 2);
        uiHitAdd(value_x + value_w / 2, y, right - value_x - value_w / 2 + 16, h, HIT_SETTING_STEP, row * 2 + 1);
    } else if (picked) {  // About: A opens it.
        uiDrawButton(value_x - (value[0] ? 34 : 22), (float)y + 21.0f, "A", COLOR_SURFACE, COLOR_TEXT);
    }
}

// How much of text goes on a line max_w wide: whole words when it has spaces,
// else as many characters as fit (Chinese, Japanese). In bytes.
static size_t fitLine(const char *text, int px, int max_w)
{
    char line[512];
    size_t length = strlen(text), best = 0, space = 0;
    if (fontTextWidth(FONT_REGULAR, px, text) <= max_w || length >= sizeof(line))
        return length;
    for (size_t i = 1; i < length; i++) {
        if ((text[i] & 0xC0) == 0x80)
            continue;  // Inside a character.
        memcpy(line, text, i);
        line[i] = '\0';
        if (fontTextWidth(FONT_REGULAR, px, line) > max_w)
            break;
        best = i;
        if (text[i] == ' ')
            space = i;
    }
    return space ? space : best;
}

// The picked row's help at the panel's foot, on up to two lines.
static void drawHelp(const char *help, int x, int y, int max_w)
{
    size_t first = fitLine(help, HELP_PX, max_w);
    char line[512];
    snprintf(line, sizeof(line), "%.*s", (int)first, help);
    fontDraw(FONT_REGULAR, HELP_PX, x, y, line, COLOR_DIM);
    const char *rest = help + first;
    while (*rest == ' ')
        rest++;
    if (*rest)
        fontDrawClipped(FONT_REGULAR, HELP_PX, x, y + HELP_LINE_H, max_w, rest, COLOR_DIM);
}

static void drawSettings(const App *app, uint64_t now_ns)
{
    SettingsPage page = (SettingsPage)app->settings_page;
    SettingRow rows[SETTINGS_PAGE_MAX_ROWS];
    int count = settingsPageRows(page, rows);
    int h = HEAD_H + count * ROW_H + FOOT_H;
    int x = (GFX_WIDTH - SETTINGS_W) / 2, y = (GFX_HEIGHT - h) / 2;
    backdrop(x, y, SETTINGS_W, h);
    int title_x = x + PAD;
    if (page != SETTINGS_PAGE_MAIN)  // A page: back to the main one.
        title_x += backButton(x + PAD - 4, y + 18) + 12;
    fontDraw(FONT_BOLD, 24, title_x, y + 22, settingsPageTitle(page), COLOR_TEXT);
    for (int n = 0; n < count; n++)
        drawSettingRow(app, rows[n], x, y + HEAD_H + n * ROW_H, now_ns);
    drawHelp(settingsRowHelp(app->settings_cursor), x + PAD, y + h - FOOT_H + 10, SETTINGS_W - 2 * PAD);
}

static void drawAbout(void)
{
    static const char *const LINES[] = {
        N_("Internet radio for Switch 2 homebrew (kaiHEN), in the spirit of KaiPlayer."),
        "",
        N_("Stations: Radio Browser (radio-browser.info), a community directory."),
        N_("World map: Natural Earth (public domain). Flags: flag-icons (MIT)."),
        N_("Time zones and weather: Open-Meteo (open-meteo.com, CC BY 4.0)."),
        N_("Genre icons: Lucide (ISC). Fonts: Noto Sans (SIL OFL 1.1)."),
        N_("Audio: FFmpeg (LGPL 2.1). Also cJSON, stb, libcurl, zlib and Mesa."),
        "",
        N_("KaiRadio is open source under the MIT License. Copyright (c) 2026 Mizuiro."),
        "github.com/mizuiroame/kairadio",
        "",
        N_("Not affiliated with Nintendo, Radio Browser or any station."),
    };
    int top = (GFX_HEIGHT - ABOUT_H) / 2 - 20;
    backdrop(ABOUT_X, top, ABOUT_W, ABOUT_H);
    int x = ABOUT_X + PAD, y = top + 20;
    int title_w = fontDraw(FONT_BOLD, 26, x, y, "KaiRadio", COLOR_ACCENT);
    fontDraw(FONT_REGULAR, 18, x + title_w + 12, y + 7, KAIRADIO_VERSION, COLOR_DIM);
    y += 64;
    for (size_t i = 0; i < sizeof(LINES) / sizeof(LINES[0]); i++, y += LINES[i - 1][0] ? 34 : 18)
        fontDrawClipped(FONT_REGULAR, 18, x, y, ABOUT_W - 2 * PAD, T(LINES[i]), i == 0 ? COLOR_TEXT : COLOR_DIM);
}

void uiDrawOverlay(const App *app, uint64_t now_ns)
{
    switch (app->overlay) {
    case OVERLAY_MENU:
    case OVERLAY_EXIT: drawMenu(app); break;
    case OVERLAY_SETTINGS: drawSettings(app, now_ns); break;
    case OVERLAY_ABOUT: drawAbout(); break;
    default: break;
    }
}
