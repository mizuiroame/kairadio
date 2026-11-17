#include "ui_now.h"

#include <stdio.h>
#include <string.h>

#include "ui_buttons.h"
#include "ui_common.h"
#include "ui_hit.h"

#define LOGO_X 96
#define LOGO_Y 128
#define LOGO_BIG 360
#define INFO_X (LOGO_X + LOGO_BIG + 64)
#define INFO_W (GFX_WIDTH - MARGIN - INFO_X)
#define CONTROLS_Y 470

static void drawBack(void)
{
    int x = MARGIN, y = SUBTITLE_Y - 4;
    gfxLine(x + 14, y + 6, x + 6, y + 15, 3, COLOR_TEXT);
    gfxLine(x + 6, y + 15, x + 14, y + 24, 3, COLOR_TEXT);
    fontDraw(FONT_REGULAR, 18, x + 22, y + 4, "Back", COLOR_TEXT);
    uiHitAdd(0, y - 8, 120, 46, HIT_BACK, 0);
}

static void drawEmpty(void)
{
    fontDrawCentered(FONT_BOLD, 28, GFX_WIDTH / 2.0f, 300.0f, "Nothing is playing.", COLOR_TEXT);
    fontDrawCentered(FONT_REGULAR, 20, GFX_WIDTH / 2.0f, 345.0f, "Pick a station in Popular, Search or Favorites.",
                     COLOR_DIM);
}

// The song title over up to two lines, cut at a space when it is long.
static void drawTitle(const char *title, int y)
{
    if (fontTextWidth(FONT_BOLD, 30, title) <= INFO_W) {
        fontDraw(FONT_BOLD, 30, INFO_X, y, title, COLOR_TEXT);
        return;
    }
    char first[256];
    snprintf(first, sizeof(first), "%s", title);
    size_t cut = strlen(first);
    while (cut > 0) {
        while (cut > 0 && first[cut] != ' ')
            cut--;
        first[cut] = '\0';
        if (fontTextWidth(FONT_BOLD, 30, first) <= INFO_W)
            break;
    }
    if (cut == 0) {
        fontDrawClipped(FONT_BOLD, 30, INFO_X, y, INFO_W, title, COLOR_TEXT);
        return;
    }
    fontDraw(FONT_BOLD, 30, INFO_X, y, first, COLOR_TEXT);
    fontDrawClipped(FONT_BOLD, 30, INFO_X, y + 40, INFO_W, title + cut + 1, COLOR_TEXT);
}

void uiDrawNowPlaying(const App *app, const RadioStatus *radio, uint64_t now_ns)
{
    drawBack();
    if (!radio->has_station) {
        drawEmpty();
        return;
    }
    const Station *s = &radio->station;
    gfxFillRoundRect(LOGO_X - 6, LOGO_Y - 6, LOGO_BIG + 12, LOGO_BIG + 12, LOGO_BIG / 6 + 6, COLOR_SURFACE);
    uiDrawLogo(LOGO_X, LOGO_Y, LOGO_BIG, s);

    int y = LOGO_Y + 4;
    bool active = radio->state != RADIO_STOPPED && radio->state != RADIO_FAILED;
    // State: a red LIVE pill while playing, else a word for what happens.
    if (radio->state == RADIO_PLAYING) {
        uiDrawPill(INFO_X, y, "LIVE", COLOR_LIVE, COLOR_TEXT);
        uiDrawLiveBars(INFO_X + uiPillWidth("LIVE") + 14, y + 6, 18, now_ns, COLOR_LIVE);
    } else {
        uiDrawPill(INFO_X, y, radioStateLabel(radio->state), COLOR_SURFACE_HI,
                   radio->state == RADIO_FAILED ? COLOR_ERROR : COLOR_DIM);
    }
    y += 50;
    fontDrawClipped(FONT_REGULAR, 22, INFO_X, y, INFO_W, s->name, COLOR_ACCENT);
    y += 40;
    if (radio->state == RADIO_FAILED)
        fontDrawClipped(FONT_REGULAR, 20, INFO_X, y, INFO_W, radio->error, COLOR_ERROR);
    else
        drawTitle(radio->title[0] ? radio->title : s->name, y);
    y += 96;
    char details[200];
    stationDetails(s, details, sizeof(details));
    fontDrawClipped(FONT_REGULAR, 18, INFO_X, y, INFO_W, details, COLOR_DIM);
    y += 36;
    char format[64];
    uiFormatLine(radio, format, sizeof(format));
    if (format[0])
        uiDrawPill(INFO_X, y, format, COLOR_SURFACE_HI, COLOR_DIM);

    // Controls: previous, play/stop, next, favorite.
    int cy = CONTROLS_Y + 40;
    int x = INFO_X + 26;
    bool can_skip = app->play_list.count > 1;
    uiDrawSkip(x, cy, false, can_skip ? COLOR_TEXT : COLOR_FAINT);
    if (can_skip)
        uiHitAdd(x - 36, cy - 40, 72, 80, HIT_PREV, 0);
    x += 96;
    uiDrawPlayButton((float)x, (float)cy, 38, active, COLOR_ACCENT, COLOR_BG);
    uiHitAdd(x - 44, cy - 44, 88, 88, HIT_PLAY, 0);
    x += 96;
    uiDrawSkip(x, cy, true, can_skip ? COLOR_TEXT : COLOR_FAINT);
    if (can_skip)
        uiHitAdd(x - 36, cy - 40, 72, 80, HIT_NEXT, 0);
    x += 80;
    bool favorite = libraryIsFavorite(&app->library, s);
    const char *label = favorite ? "Favorite" : "Add to favorites";
    int w = uiPillWidth(label) + 34;
    gfxFillRoundRect(x, cy - 20, w, 40, 20, favorite ? gfxMix(COLOR_SURFACE_HI, COLOR_LIVE, 60) : COLOR_SURFACE_HI);
    uiDrawHeart(x + 24.0f, (float)cy, 20, favorite, favorite ? COLOR_LIVE : COLOR_DIM);
    fontDrawMiddle(FONT_BOLD, 15, x + 44, (float)cy, label, favorite ? COLOR_TEXT : COLOR_DIM);
    uiHitAdd(x - 6, cy - 30, w + 12, 60, HIT_FAVORITE, -1);
}
