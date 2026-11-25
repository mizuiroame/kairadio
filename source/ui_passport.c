#include "ui_passport.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "font.h"
#include "lang.h"
#include "ui_buttons.h"
#include "ui_common.h"
#include "ui_flag.h"
#include "ui_hit.h"
#include "ui_settings.h"

#define PANEL_X 170  // As About.
#define PANEL_W (GFX_WIDTH - 2 * PANEL_X)
#define PANEL_H 504
#define PAD 32
#define COLUMNS 5
#define STAMP_GAP 12
#define STAMP_W ((PANEL_W - 2 * PAD - (COLUMNS - 1) * STAMP_GAP) / COLUMNS)
#define STAMP_H 110
#define FLAG_W 64
#define FLAG_H 48
#define GRID_Y 118   // Below the title and the count.
#define SHOWN_MAX 400  // Countries drawn: more than there are.
#define CARD_FLAGS 3      // The World view's card: the newest stamps' flags,
#define CARD_FLAG_W 40
#define CARD_FLAG_H 30
#define CARD_FLAG_STEP 22  // overlapping.

int uiPassportRows(const App *app)
{
    PassportCountry none;
    int n = (int)passportCountries(&app->passport, &none, 0);
    return (n + COLUMNS - 1) / COLUMNS;
}

// The country's name as the directory gives it (in the World view's list), else its code.
static void countryName(const App *app, const char *code, char *out, size_t size)
{
    const CategoryList *countries = &app->categories[SEARCH_COUNTRY];
    for (size_t i = 0; i < countries->count; i++)
        if (strcasecmp(countries->items[i].code, code) == 0) {
            char label[CATEGORY_NAME_MAX];
            categoryLabel(&countries->items[i], label, sizeof(label));
            snprintf(out, size, "%s", label);
            return;
        }
    snprintf(out, size, "%s", code);
}

// One country's stamp: its flag, name, the stations heard there and the day first heard.
static void drawStamp(const App *app, const PassportCountry *c, int x, int y)
{
    gfxFillRoundRect(x, y, STAMP_W, STAMP_H, 12, COLOR_SURFACE_HI);
    int flag_x = x + (STAMP_W - FLAG_W) / 2;
    if (!uiDrawFlag(flag_x, y + 10, FLAG_W, FLAG_H, c->code))
        fontDrawCentered(FONT_BOLD, 22, x + STAMP_W / 2.0f, y + 10 + FLAG_H / 2.0f, c->code, COLOR_DIM);
    char name[CATEGORY_NAME_MAX], line[64], day[16];
    countryName(app, c->code, name, sizeof(name));
    int w = fontTextWidth(FONT_BOLD, 16, name);
    int nx = w < STAMP_W - 16 ? x + (STAMP_W - w) / 2 : x + 8;
    fontDrawClipped(FONT_BOLD, 16, nx, y + 64, STAMP_W - 16, name, COLOR_TEXT);
    time_t first = (time_t)c->first;
    struct tm when;
    if (c->stations > 0)
        snprintf(line, sizeof(line), c->stations == 1 ? T("%d station") : T("%d stations"), c->stations);
    else
        line[0] = '\0';
    if (gmtime_r(&first, &when) && strftime(day, sizeof(day), "%Y-%m-%d", &when))
        snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%s", line[0] ? "  \xC2\xB7  " : "", day);
    w = fontTextWidth(FONT_REGULAR, 13, line);
    fontDrawClipped(FONT_REGULAR, 13, w < STAMP_W - 16 ? x + (STAMP_W - w) / 2 : x + 8, y + 88, STAMP_W - 16, line,
                    COLOR_DIM);
}

// The arrows to the stamps above or below, where there are some.
static void drawScroll(const App *app, int x, int y)
{
    int rows = uiPassportRows(app);
    if (app->passport_row + UI_PASSPORT_ROWS < rows) {
        uiDrawButton(x - 22, (float)y + 11, "v", COLOR_SURFACE_HI, COLOR_TEXT);
        uiHitAdd(x - 34, y - 8, 46, 38, HIT_PASSPORT_SCROLL, 1);
    }
    if (app->passport_row > 0) {
        uiDrawButton(x - 56, (float)y + 11, "^", COLOR_SURFACE_HI, COLOR_TEXT);
        uiHitAdd(x - 68, y - 8, 34, 38, HIT_PASSPORT_SCROLL, -1);
    }
}

void uiDrawPassport(const App *app)
{
    int top = (GFX_HEIGHT - PANEL_H) / 2 - 20, x = PANEL_X + PAD;
    uiDrawBackdrop(PANEL_X, top, PANEL_W, PANEL_H);
    fontDraw(FONT_BOLD, 34, x, top + 20, T("Passport"), COLOR_TEXT);
    static PassportCountry countries[SHOWN_MAX];
    int n = (int)passportCountries(&app->passport, countries, SHOWN_MAX);
    n = n > SHOWN_MAX ? SHOWN_MAX : n;
    if (n == 0) {
        fontDrawClipped(FONT_REGULAR, 18, x, top + GRID_Y, PANEL_W - 2 * PAD,
                        T("No stamps yet: each country you listen to gets one here."), COLOR_DIM);
        return;
    }
    char count[64], stations[64], line[160];
    snprintf(count, sizeof(count), n == 1 ? T("%d country") : T("%d countries"), n);
    int heard = (int)passportStations(&app->passport);
    snprintf(stations, sizeof(stations), heard == 1 ? T("%d station") : T("%d stations"), heard);
    snprintf(line, sizeof(line), "%s  \xC2\xB7  %s", count, stations);
    fontDraw(FONT_REGULAR, 19, x, top + 70, line, COLOR_DIM);
    drawScroll(app, PANEL_X + PANEL_W - PAD, top + 30);
    for (int row = 0; row < UI_PASSPORT_ROWS; row++)
        for (int col = 0; col < COLUMNS; col++) {
            int i = (app->passport_row + row) * COLUMNS + col;
            if (i >= n)
                return;
            drawStamp(app, &countries[i], x + col * (STAMP_W + STAMP_GAP), top + GRID_Y + row * (STAMP_H + STAMP_GAP));
        }
}

void uiDrawPassportCard(const App *app)
{
    static PassportCountry countries[SHOWN_MAX];
    int n = (int)passportCountries(&app->passport, countries, SHOWN_MAX);
    int shown = n > SHOWN_MAX ? SHOWN_MAX : n;
    int x = UI_PASSPORT_CARD_X, y = app->config.world_globe ? UI_PASSPORT_CARD_Y_GLOBE : UI_PASSPORT_CARD_Y_FLAT;
    gfxFillRoundRect(x, y, UI_PASSPORT_CARD_W, UI_PASSPORT_CARD_H, 14, COLOR_SURFACE);
    gfxFillRect(x, y + 16, 3, UI_PASSPORT_CARD_H - 32, COLOR_ACCENT);
    // The newest stamps, overlapping at the right, the newest on top.
    int flags = shown < CARD_FLAGS ? shown : CARD_FLAGS;
    int flags_w = flags ? CARD_FLAG_W + (flags - 1) * CARD_FLAG_STEP : 0;
    int fx = x + UI_PASSPORT_CARD_W - 16 - flags_w, fy = y + (UI_PASSPORT_CARD_H - CARD_FLAG_H) / 2;
    for (int i = 0; i < flags; i++) {
        const char *code = countries[shown - flags + i].code;
        gfxFillRoundRect(fx + i * CARD_FLAG_STEP - 2, fy - 2, CARD_FLAG_W + 4, CARD_FLAG_H + 4, 5, COLOR_SURFACE);
        if (!uiDrawFlag(fx + i * CARD_FLAG_STEP, fy, CARD_FLAG_W, CARD_FLAG_H, code))
            gfxFillRoundRect(fx + i * CARD_FLAG_STEP, fy, CARD_FLAG_W, CARD_FLAG_H, 4, COLOR_SURFACE_HI);
    }
    int text_w = (flags ? fx - 12 : x + UI_PASSPORT_CARD_W - 16) - (x + 20);
    char count[64];
    snprintf(count, sizeof(count), n == 1 ? T("%d country") : T("%d countries"), n);
    fontDrawClipped(FONT_BOLD, 22, x + 20, y + 12, text_w, T("Passport"), COLOR_TEXT);
    fontDrawClipped(FONT_REGULAR, 17, x + 20, y + 44, text_w - 30, count, COLOR_DIM);
    int said = fontTextWidth(FONT_REGULAR, 17, count);
    said = said > text_w - 30 ? text_w - 30 : said;
    uiDrawButton(x + 20 + said + 8, (float)(y + 54), "LS", COLOR_SURFACE_HI, COLOR_DIM);
    uiHitAdd(x, y, UI_PASSPORT_CARD_W, UI_PASSPORT_CARD_H, HIT_PASSPORT, 0);
}
