#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "logos.h"
#include "ui_buttons.h"
#include "ui_common.h"
#include "ui_hit.h"
#include "ui_map.h"
#include "ui_now.h"
#include "util.h"

#define TAB_GAP 28
#define TAB_TEXT_MIDDLE 28.0f
#define HINT_GAP 9
#define ROW_LOGO 50
#define HEART_W 64
#define BATTERY_LOW 15
#define COLOR_CHARGING GFX_RGB(0x5B, 0xD6, 0x7A)
#define BAR_GAP 76
#define BAR_PREV_X (GFX_WIDTH / 2 - BAR_GAP)
#define BAR_CY (MINI_Y + 46)

static const char *const VIEW_NAMES[VIEW_COUNT] = {"World", "Popular", "Search", "Favorites", "Recent", "Now Playing"};

size_t uiFirstVisible(size_t cursor, size_t count)
{
    if (count <= UI_LIST_ROWS || cursor < UI_LIST_ROWS / 2)
        return 0;
    size_t first = cursor - UI_LIST_ROWS / 2;
    return first + UI_LIST_ROWS > count ? count - UI_LIST_ROWS : first;
}

// --- Header ---

static void drawTabs(const App *app)
{
    int widths[VIEW_COUNT], total = 0;
    for (int i = 0; i < VIEW_COUNT; i++) {
        widths[i] = fontTextWidth((int)app->view == i ? FONT_BOLD : FONT_REGULAR, 20, VIEW_NAMES[i]);
        total += widths[i] + (i > 0 ? TAB_GAP : 0);
    }
    int x = (GFX_WIDTH - total) / 2;
    uiDrawButton(x - HINT_GAP - uiButtonWidth("ZL"), TAB_TEXT_MIDDLE, "ZL", COLOR_SURFACE_HI, COLOR_DIM);
    for (int i = 0; i < VIEW_COUNT; i++) {
        bool active = (int)app->view == i;
        fontDraw(active ? FONT_BOLD : FONT_REGULAR, 20, x, 18, VIEW_NAMES[i], active ? COLOR_TEXT : COLOR_DIM);
        uiHitAdd(x - TAB_GAP / 2, 4, widths[i] + TAB_GAP, HEADER_H - 8, HIT_TAB, i);
        if (active)
            gfxFillRoundRect(x, 48, widths[i], 3, 1, COLOR_ACCENT);
        x += widths[i] + TAB_GAP;
    }
    uiDrawButton(x - TAB_GAP + HINT_GAP, TAB_TEXT_MIDDLE, "ZR", COLOR_SURFACE_HI, COLOR_DIM);
}

static void drawBattery(int x, int y, int percent, bool charging)
{
    Color fill = charging ? COLOR_CHARGING : percent <= BATTERY_LOW ? COLOR_ERROR : COLOR_TEXT;
    gfxFillRoundRect(x, y, 30, 16, 4, COLOR_DIM);
    gfxFillRoundRect(x + 2, y + 2, 26, 12, 3, COLOR_BG);
    gfxFillRoundRect(x + 31, y + 5, 3, 6, 1, COLOR_DIM);
    int inner = 22 * (percent < 0 ? 0 : percent > 100 ? 100 : percent) / 100;
    if (inner > 0)
        gfxFillRoundRect(x + 4, y + 4, inner, 8, 2, fill);
    if (charging) {  // A lightning bolt over the battery.
        float cx = (float)x + 15.0f, cy = (float)y + 8.0f;
        // Two halves that overlap in the middle, so no seam shows between them.
        gfxTriangle(cx + 2.0f, cy - 8.0f, cx - 5.0f, cy + 1.5f, cx + 1.5f, cy + 1.5f, COLOR_BG);
        gfxTriangle(cx - 2.0f, cy + 8.0f, cx + 5.0f, cy - 1.5f, cx - 1.5f, cy - 1.5f, COLOR_BG);
        gfxTriangle(cx + 1.5f, cy - 6.0f, cx - 3.5f, cy + 1.0f, cx + 1.0f, cy + 1.0f, COLOR_TEXT);
        gfxTriangle(cx - 1.5f, cy + 6.0f, cx + 3.5f, cy - 1.0f, cx - 1.0f, cy - 1.0f, COLOR_TEXT);
    }
}

static void drawStatus(const App *app)
{
    const SysInfo *sys = &app->sys;
    int x = GFX_WIDTH - MARGIN;
    if (sys->has_battery) {
        char percent[8];
        snprintf(percent, sizeof(percent), "%d%%", sys->battery_percent);
        x -= 34;
        drawBattery(x, 20, sys->battery_percent, sys->charging);
        x -= fontTextWidth(FONT_REGULAR, 17, percent) + 8;
        fontDraw(FONT_REGULAR, 17, x, 19, percent, sys->charging ? COLOR_CHARGING : COLOR_DIM);
        x -= 20;
    }
    if (sys->has_time) {
        char clock[16];
        utilFormatClock(clock, sizeof(clock), sys->hour, sys->minute, true);
        x -= fontTextWidth(FONT_BOLD, 18, clock);
        fontDraw(FONT_BOLD, 18, x, 18, clock, COLOR_TEXT);
    }
}

// A cross before the title: the touch way out (+ with the buttons).
static void drawHeader(const App *app)
{
    float cx = MARGIN + 10.0f, cy = 32.0f;
    gfxLine(cx - 8, cy - 8, cx + 8, cy + 8, 3.0f, COLOR_DIM);
    gfxLine(cx - 8, cy + 8, cx + 8, cy - 8, 3.0f, COLOR_DIM);
    uiHitAdd(0, 0, MARGIN + 34, HEADER_H, HIT_EXIT, 0);
    fontDraw(FONT_BOLD, 26, MARGIN + 34, 16, "KaiRadio", COLOR_ACCENT);
    drawTabs(app);
    drawStatus(app);
}

// --- Lists ---

static bool isPlayingStation(const RadioStatus *radio, const Station *s)
{
    if (!radio->has_station || radio->state == RADIO_STOPPED || radio->state == RADIO_FAILED)
        return false;
    return radio->station.uuid[0] ? strcmp(radio->station.uuid, s->uuid) == 0 : strcmp(radio->station.url, s->url) == 0;
}

static void drawRow(const App *app, const Station *s, size_t index, int y, bool selected, const RadioStatus *radio,
                    uint64_t now_ns)
{
    if (selected) {
        gfxFillRoundRect(MARGIN - 12, y + 2, GFX_WIDTH - 2 * (MARGIN - 12), UI_ROW_H - 4, 10, COLOR_SURFACE_HI);
        gfxFillRect(MARGIN - 12, y + 14, 3, UI_ROW_H - 28, COLOR_ACCENT);
    }
    uiHitAdd(MARGIN - 12, y, GFX_WIDTH - 2 * (MARGIN - 12) - HEART_W, UI_ROW_H, HIT_ROW, (int)index);
    uiDrawLogo(MARGIN, y + (UI_ROW_H - ROW_LOGO) / 2, ROW_LOGO, s);
    int text_x = MARGIN + ROW_LOGO + 16;
    int text_w = GFX_WIDTH - MARGIN - HEART_W - text_x - 16;
    bool playing = isPlayingStation(radio, s);
    if (playing) {
        uiDrawLiveBars(text_x, y + 14, 18, now_ns, COLOR_ACCENT);
        text_x += 30;
        text_w -= 30;
    }
    fontDrawClipped(FONT_BOLD, 22, text_x, y + 9, text_w, s->name, playing ? COLOR_ACCENT : COLOR_TEXT);
    char details[200];
    stationDetails(s, details, sizeof(details));
    fontDrawClipped(FONT_REGULAR, 17, playing ? text_x - 30 : text_x, y + 38, text_w + (playing ? 30 : 0), details,
                    COLOR_DIM);

    bool favorite = libraryIsFavorite(&app->library, s);
    int heart_x = GFX_WIDTH - MARGIN - HEART_W;
    uiDrawHeart(heart_x + HEART_W / 2.0f, y + UI_ROW_H / 2.0f, 26, favorite, favorite ? COLOR_LIVE : COLOR_FAINT);
    uiHitAdd(heart_x, y, HEART_W + MARGIN - 12, UI_ROW_H, HIT_FAVORITE, (int)index);
}

static void drawScrollbar(size_t first, size_t count)
{
    if (count <= UI_LIST_ROWS)
        return;
    int track_h = UI_LIST_ROWS * UI_ROW_H;
    int thumb_h = track_h * UI_LIST_ROWS / (int)count;
    if (thumb_h < 24)
        thumb_h = 24;
    int thumb_y = UI_LIST_Y + (int)((long)(track_h - thumb_h) * (long)first / (long)(count - UI_LIST_ROWS));
    gfxFillRoundRect(GFX_WIDTH - 12, UI_LIST_Y, 4, track_h, 2, COLOR_SURFACE);
    gfxFillRoundRect(GFX_WIDTH - 12, thumb_y, 4, thumb_h, 2, COLOR_FAINT);
}

static void drawMessage(const char *line1, const char *line2, bool retry)
{
    int y = UI_LIST_Y + 60;
    fontDrawCentered(FONT_BOLD, 24, GFX_WIDTH / 2.0f, (float)y, line1, COLOR_TEXT);
    if (line2 && line2[0])
        fontDrawCentered(FONT_REGULAR, 19, GFX_WIDTH / 2.0f, y + 40.0f, line2, COLOR_DIM);
    if (retry) {
        const char *label = "Try again";
        int w = uiPillWidth(label);
        int x = (GFX_WIDTH - w) / 2;
        uiDrawPill(x, y + 76, label, COLOR_ACCENT, COLOR_BG);
        uiHitAdd(x - 12, y + 66, w + 24, 50, HIT_RETRY, 0);
    }
}

// --- Search ---

static const char *const MODE_NAMES[SEARCH_MODES] = {"Name", "Genre", "Country", "Language"};
static const char *const MODE_PLURALS[SEARCH_MODES] = {"", "genres", "countries", "languages"};

// The ways to search, as pills; returns where the space after them starts.
static int drawSearchModes(const App *app)
{
    int x = MARGIN, y = SUBTITLE_Y - 6;
    uiDrawButton(x, y + 17.0f, "<", COLOR_SURFACE_HI, COLOR_DIM);
    x += uiButtonWidth("<") + 8;
    for (int i = 0; i < SEARCH_MODES; i++) {
        bool on = (int)app->search_mode == i;
        int w = uiPillWidth(MODE_NAMES[i]);
        uiDrawPill(x, y + 2, MODE_NAMES[i], on ? COLOR_ACCENT : COLOR_SURFACE_HI, on ? COLOR_BG : COLOR_DIM);
        uiHitAdd(x - 4, y - 6, w + 8, 46, HIT_SEARCH_MODE, i);
        x += w + 8;
    }
    uiDrawButton(x, y + 17.0f, ">", COLOR_SURFACE_HI, COLOR_DIM);
    return x + uiButtonWidth(">") + 24;
}

static void drawSearchBox(const App *app, int x)
{
    int w = GFX_WIDTH - MARGIN - x, y = SUBTITLE_Y - 6;
    gfxFillRoundRect(x, y, w, 34, 17, COLOR_SURFACE_HI);
    float cx = x + 22.0f, cy = y + 16.0f;  // A magnifier.
    gfxArc(cx - 2, cy - 2, 7, 2.5f, 0, 6.2832f, COLOR_DIM);
    gfxLine(cx + 3, cy + 3, cx + 8, cy + 8, 3, COLOR_DIM);
    const char *shown = app->query[0] ? app->query : "Search by station name";
    fontDrawClipped(FONT_REGULAR, 18, x + 40, y + 6, w - 80, shown, app->query[0] ? COLOR_TEXT : COLOR_DIM);
    uiDrawButton(x + w - 30, y + 17.0f, "X", COLOR_BG, COLOR_DIM);
    uiHitAdd(x, y - 4, w, 42, HIT_SEARCH, 0);
}

// "< Back  Jazz · 1,234 stations" while a category is open.
static void drawCategoryBack(const App *app, int x)
{
    int y = SUBTITLE_Y - 4;
    gfxLine(x + 14, y + 6, x + 6, y + 15, 3, COLOR_TEXT);
    gfxLine(x + 6, y + 15, x + 14, y + 24, 3, COLOR_TEXT);
    int back_w = fontDraw(FONT_REGULAR, 18, x + 22, y + 4, "Back", COLOR_TEXT);
    uiHitAdd(x - 8, y - 8, back_w + 40, 46, HIT_BACK, 0);
    char label[CATEGORY_NAME_MAX], text[CATEGORY_NAME_MAX + 32];
    categoryLabel(&app->open_category, label, sizeof(label));
    snprintf(text, sizeof(text), "%s \xC2\xB7 %d stations", label, app->open_category.count);
    int tx = x + 22 + back_w + 20;
    fontDrawClipped(FONT_BOLD, 18, tx, y + 4, GFX_WIDTH - MARGIN - tx, text, COLOR_ACCENT);
}

static void drawSearchHeader(const App *app)
{
    int x = drawSearchModes(app);
    if (app->search_mode == SEARCH_NAME)
        drawSearchBox(app, x);
    else if (app->category_open)
        drawCategoryBack(app, x);
    else
        fontDraw(FONT_REGULAR, 18, x, SUBTITLE_Y, "Pick one to see its stations", COLOR_DIM);
}

// A tile with the country or language code, or a note for a genre.
static void drawCategoryTile(int x, int y, int size, SearchMode mode, const Category *c)
{
    gfxFillRoundRect(x, y, size, size, size / 6, COLOR_SURFACE_HI);
    if (mode == SEARCH_GENRE || !c->code[0]) {
        float cx = x + size / 2.0f, cy = y + size / 2.0f;  // A note.
        gfxFillCircle((int)(cx - 4), (int)(cy + 8), 7, COLOR_ACCENT);
        gfxFillRect((int)(cx + 1), (int)(cy - 12), 3, 21, COLOR_ACCENT);
        gfxLine(cx + 2, cy - 12, cx + 10, cy - 6, 3, COLOR_ACCENT);
        return;
    }
    char code[8];
    snprintf(code, sizeof(code), "%s", c->code);
    for (char *p = code; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 'a' + 'A');
    fontDrawCentered(FONT_BOLD, 18, x + size / 2.0f, y + size / 2.0f, code, COLOR_ACCENT);
}

static void drawCategories(const App *app)
{
    SearchMode mode = app->search_mode;
    const CategoryList *list = &app->categories[mode];
    if (list->count == 0) {
        char line[64];
        snprintf(line, sizeof(line), "Loading %s\xE2\x80\xA6", MODE_PLURALS[mode]);
        if (appFailed(app))
            drawMessage("The station directory can't be reached.", app->directory.error, true);
        else
            drawMessage(line, "", false);
        return;
    }
    uiHitAdd(0, UI_LIST_Y, GFX_WIDTH, UI_LIST_ROWS * UI_ROW_H, HIT_LIST, 0);
    size_t cursor = app->category_cursor[mode];
    size_t first = uiFirstVisible(cursor, list->count);
    for (size_t i = first; i < list->count && i < first + UI_LIST_ROWS; i++) {
        const Category *c = &list->items[i];
        int y = UI_LIST_Y + (int)(i - first) * UI_ROW_H;
        if (i == cursor) {
            gfxFillRoundRect(MARGIN - 12, y + 2, GFX_WIDTH - 2 * (MARGIN - 12), UI_ROW_H - 4, 10, COLOR_SURFACE_HI);
            gfxFillRect(MARGIN - 12, y + 14, 3, UI_ROW_H - 28, COLOR_ACCENT);
        }
        uiHitAdd(MARGIN - 12, y, GFX_WIDTH - 2 * (MARGIN - 12), UI_ROW_H, HIT_CATEGORY, (int)i);
        drawCategoryTile(MARGIN, y + (UI_ROW_H - ROW_LOGO) / 2, ROW_LOGO, mode, c);
        char label[CATEGORY_NAME_MAX], count[32];
        categoryLabel(c, label, sizeof(label));
        snprintf(count, sizeof(count), "%d station%s", c->count, c->count == 1 ? "" : "s");
        int text_x = MARGIN + ROW_LOGO + 16;
        fontDrawClipped(FONT_BOLD, 22, text_x, y + 9, GFX_WIDTH - MARGIN - text_x - 160, label, COLOR_TEXT);
        fontDraw(FONT_REGULAR, 17, text_x, y + 38, count, COLOR_DIM);
        // A chevron: it opens.
        float cx = GFX_WIDTH - MARGIN - 20.0f, cy = y + UI_ROW_H / 2.0f;
        gfxLine(cx - 4, cy - 8, cx + 4, cy, 3, COLOR_FAINT);
        gfxLine(cx + 4, cy, cx - 4, cy + 8, 3, COLOR_FAINT);
    }
    drawScrollbar(first, list->count);
}

// --- Lists ---

// The line under the tabs: what the list is.
static void drawSubtitle(const App *app, const StationList *list)
{
    char text[160];
    switch (app->view) {
    case VIEW_POPULAR: snprintf(text, sizeof(text), "The most played stations"); break;
    case VIEW_FAVORITES:
        snprintf(text, sizeof(text), "%zu favorite%s", list->count, list->count == 1 ? "" : "s");
        break;
    default: snprintf(text, sizeof(text), "Played lately"); break;
    }
    fontDraw(FONT_REGULAR, 18, MARGIN, SUBTITLE_Y, text, COLOR_DIM);
    if (app->view == VIEW_POPULAR)
        fontDraw(FONT_REGULAR, 15, GFX_WIDTH - MARGIN - fontTextWidth(FONT_REGULAR, 15, "From radio-browser.info"),
                 SUBTITLE_Y + 2, "From radio-browser.info", COLOR_FAINT);
}

static void drawEmpty(const App *app)
{
    switch (app->view) {
    case VIEW_SEARCH:
        if (app->search_mode != SEARCH_NAME)
            drawMessage("No stations here right now.", "Go back and pick another one.", false);
        else
            drawMessage(app->query[0] ? "No stations found." : "Find a station by its name.",
                        app->query[0] ? "Try another name." : "Press X or tap the box above.", false);
        break;
    case VIEW_FAVORITES:
        drawMessage("No favorites yet.", "Press Y or tap the heart on a station to keep it here.", false);
        break;
    default: drawMessage("Nothing played yet.", "The stations you play show up here.", false); break;
    }
}

static void drawList(const App *app, const RadioStatus *radio, uint64_t now_ns)
{
    if (app->view == VIEW_SEARCH) {
        drawSearchHeader(app);
        if (appBrowsingCategories(app)) {
            drawCategories(app);
            return;
        }
    }
    const StationList *list = appList(app, app->view);
    if (app->view != VIEW_SEARCH)
        drawSubtitle(app, list);
    if (list->count == 0) {
        if (appLoading(app))
            drawMessage("Loading stations\xE2\x80\xA6", "", false);
        else if (appFailed(app))
            drawMessage("The station directory can't be reached.", app->directory.error, true);
        else
            drawEmpty(app);
        return;
    }
    uiHitAdd(0, UI_LIST_Y, GFX_WIDTH, UI_LIST_ROWS * UI_ROW_H, HIT_LIST, 0);
    size_t cursor = app->cursor[app->view];
    size_t first = uiFirstVisible(cursor, list->count);
    for (size_t i = first; i < list->count && i < first + UI_LIST_ROWS; i++)
        drawRow(app, &list->items[i], i, UI_LIST_Y + (int)(i - first) * UI_ROW_H, i == cursor, radio, now_ns);
    drawScrollbar(first, list->count);
}

// --- World ---

static void drawWorld(const App *app, uint64_t now_ns)
{
    fontDraw(FONT_REGULAR, 18, MARGIN, SUBTITLE_Y, "Stations around the world", COLOR_DIM);
    fontDraw(FONT_REGULAR, 15, GFX_WIDTH - MARGIN - fontTextWidth(FONT_REGULAR, 15, "From radio-browser.info"),
             SUBTITLE_Y + 2, "From radio-browser.info", COLOR_FAINT);
    uiDrawMap(app, now_ns);
    if (app->categories[SEARCH_COUNTRY].count > 0)
        return;
    if (appFailed(app))
        drawMessage("The station directory can't be reached.", app->directory.error, true);
    else
        drawMessage("Lighting up the world\xE2\x80\xA6", "", false);
}

// --- The player bar ---

static void drawMiniPlayer(const App *app, const RadioStatus *radio, uint64_t now_ns)
{
    gfxFillRect(0, MINI_Y, GFX_WIDTH, MINI_H, COLOR_SURFACE);
    gfxFillRect(0, MINI_Y, GFX_WIDTH, 3, COLOR_FAINT);
    uiHitAdd(0, MINI_Y, GFX_WIDTH, MINI_H, HIT_MINI_PLAYER, 0);
    bool has = radio->has_station;
    int logo = 64;
    int text_x = MARGIN;
    if (has) {
        uiDrawLogo(MARGIN - 8, MINI_Y + 14, logo, &radio->station);
        text_x = MARGIN + logo + 8;
    }
    int text_w = BAR_PREV_X - 48 - text_x;
    fontDrawClipped(FONT_BOLD, 22, text_x, MINI_Y + 16, text_w, has ? radio->station.name : "Pick a station",
                    has ? COLOR_TEXT : COLOR_DIM);
    bool toast = app->toast[0] && now_ns < app->toast_until_ns;
    const char *second = toast                              ? app->toast
                         : radio->state == RADIO_FAILED      ? radio->error
                         : radio->state == RADIO_PLAYING && radio->title[0] ? radio->title
                         : has                               ? radioStateLabel(radio->state)
                         : app->view == VIEW_WORLD           ? "Open a country to see its stations."
                                                             : "Press A or tap one to play it.";
    Color second_color = toast || radio->state == RADIO_FAILED ? COLOR_ERROR : COLOR_DIM;
    fontDrawClipped(FONT_REGULAR, 19, text_x, MINI_Y + 50, text_w, second, second_color);

    bool active = radio->state != RADIO_STOPPED && radio->state != RADIO_FAILED;
    int play_x = BAR_PREV_X + BAR_GAP;
    // Controls that can't act are drawn faint and take no taps.
    bool can_skip = app->play_list.count > 1;
    const StationList *shown = appList(app, app->view);
    bool can_play = has || (shown && shown->count > 0);
    uiDrawSkip(BAR_PREV_X, BAR_CY, false, can_skip ? COLOR_TEXT : COLOR_FAINT);
    uiDrawSkip(play_x + BAR_GAP, BAR_CY, true, can_skip ? COLOR_TEXT : COLOR_FAINT);
    uiDrawPlayButton((float)play_x, (float)BAR_CY, 26, active, can_play ? COLOR_ACCENT : COLOR_SURFACE_HI,
                     can_play ? COLOR_BG : COLOR_FAINT);
    if (can_skip) {
        uiHitAdd(BAR_PREV_X - 36, MINI_Y + 6, 72, MINI_H - 6, HIT_PREV, 0);
        uiHitAdd(play_x + BAR_GAP - 36, MINI_Y + 6, 72, MINI_H - 6, HIT_NEXT, 0);
    }
    if (can_play)
        uiHitAdd(play_x - 36, MINI_Y + 6, 72, MINI_H - 6, HIT_PLAY, 0);

    // Right side: what's playing, as a pill, and the heart.
    int x = GFX_WIDTH - MARGIN;
    if (has) {
        bool favorite = libraryIsFavorite(&app->library, &radio->station);
        uiDrawHeart(x - 22.0f, (float)BAR_CY, 26, favorite, favorite ? COLOR_LIVE : COLOR_DIM);
        uiHitAdd(x - 52, MINI_Y + 6, 52 + MARGIN, MINI_H - 6, HIT_FAVORITE, -1);
        x -= 60;
    }
    char format[64];
    uiFormatLine(radio, format, sizeof(format));
    if (active && radio->state == RADIO_PLAYING) {
        int w = uiPillWidth("LIVE");
        x -= w;
        uiDrawPill(x, BAR_CY - 15, "LIVE", COLOR_LIVE, COLOR_TEXT);
        x -= 10;
    }
    if (format[0] && has) {
        int w = uiPillWidth(format);
        if (x - w > play_x + BAR_GAP + 40) {
            x -= w;
            uiDrawPill(x, BAR_CY - 15, format, COLOR_SURFACE_HI, COLOR_DIM);
        }
    }
}

static const char *footerHelp(const App *app)
{
    switch (app->view) {
    case VIEW_WORLD: return "[<>^v] country  [A] open  [X] now playing  [L][R] station  [-] play/stop  [+] exit";
    case VIEW_NOW_PLAYING:
        return "[A] play/stop  [Y] favorite  [L][R] station  [B] back  [ZL][ZR] tabs  [+] exit";
    case VIEW_SEARCH:
        if (appBrowsingCategories(app))
            return "[A] open  [<>] name, genre, country, language  [ZL][ZR] tabs  [+] exit";
        if (app->category_open)
            return "[A] play  [Y] favorite  [B] back  [<>] search by  [L][R] station  [+] exit";
        return "[X] type a name  [A] play  [Y] favorite  [<>] search by  [L][R] station  [+] exit";
    default:
        return "[A] play  [Y] favorite  [X] now playing  [L][R] station  [-] play/stop  [+] exit";
    }
}

void uiDraw(const App *app, const RadioStatus *radio, uint64_t now_ns)
{
    uiHitReset();
    gfxClear(COLOR_BG);
    drawHeader(app);
    if (app->view == VIEW_NOW_PLAYING) {
        uiDrawNowPlaying(app, radio, now_ns);
    } else {
        if (app->view == VIEW_WORLD)
            drawWorld(app, now_ns);
        else
            drawList(app, radio, now_ns);
        drawMiniPlayer(app, radio, now_ns);
    }
    gfxFillRect(0, FOOTER_Y, GFX_WIDTH, GFX_HEIGHT - FOOTER_Y, COLOR_BG);
    uiDrawHelp(MARGIN, FOOTER_Y + 28.0f, GFX_WIDTH - 2 * MARGIN, footerHelp(app));
    logosEndFrame();
}
