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

void uiDrawPassportButton(const App *app, int x)
{
    PassportCountry none;
    int n = (int)passportCountries(&app->passport, &none, 0);
    char label[64];
    snprintf(label, sizeof(label), "%s  %d", T("Passport"), n);
    int y = SUBTITLE_Y - 6, w = uiPillWidth(label);
    uiDrawPill(x, y, label, COLOR_SURFACE_HI, COLOR_TEXT);
    uiDrawButton(x + w + 8, (float)y + 15, "LS", COLOR_SURFACE_HI, COLOR_DIM);
    uiHitAdd(x - 4, y - 6, w + 8 + 22 + 8, 42, HIT_PASSPORT, 0);
}
