#include "ui_podcast.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lang.h"
#include "podfetch.h"
#include "ui.h"
#include "ui_buttons.h"
#include "ui_common.h"
#include "ui_flag.h"
#include "ui_hit.h"

#define ROW_LOGO 50
#define TEXT_X (MARGIN + ROW_LOGO + 16)
#define HEARD_W 64
#define BAR_CY (MINI_Y + 46)
#define BAR_GAP 76
#define NOW_LOGO_X 96
#define NOW_LOGO_Y 128
#define NOW_LOGO 360
#define NOW_X (NOW_LOGO_X + NOW_LOGO + 64)
#define NOW_W (GFX_WIDTH - MARGIN - NOW_X)
#define COLOR_HEARD GFX_RGB(0x5B, 0xD6, 0x7A)

void uiFormatTime(double seconds, char *out, size_t size)
{
    long s = seconds > 0 ? (long)seconds : 0;
    if (s >= 3600)
        snprintf(out, size, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
    else
        snprintf(out, size, "%ld:%02ld", s / 60, s % 60);
}

// A podcast's picture, or its initial on a color when there's none yet.
static void drawPicture(int x, int y, int size, const char *image, const char *name)
{
    Station s;
    memset(&s, 0, sizeof(s));
    snprintf(s.favicon, sizeof(s.favicon), "%s", image);
    snprintf(s.name, sizeof(s.name), "%.120s", name);  // Only its first letter shows.
    uiDrawLogo(x, y, size, &s);
}

// "< Back" at x; returns where the space after it starts.
static int drawBack(int x)
{
    int y = SUBTITLE_Y - 4;
    gfxLine(x + 14, y + 6, x + 6, y + 15, 3, COLOR_TEXT);
    gfxLine(x + 6, y + 15, x + 14, y + 24, 3, COLOR_TEXT);
    int w = fontDraw(FONT_REGULAR, 18, x + 22, y + 4, T("Back"), COLOR_TEXT);
    uiHitAdd(x - 8, y - 8, w + 40, 46, HIT_POD_BACK, 0);
    return x + 22 + w + 20;
}

// The pill that finds a podcast, at the right; returns where it starts.
static int drawFindButton(void)
{
    const char *label = T("Find a podcast");
    int w = uiPillWidth(label);
    int x = GFX_WIDTH - MARGIN - w, y = SUBTITLE_Y - 4;
    int button_x = x - 8 - uiButtonWidth("X");
    uiDrawButton(button_x, y + 15.0f, "X", COLOR_SURFACE_HI, COLOR_DIM);
    uiDrawPill(x, y, label, COLOR_ACCENT, COLOR_BG);
    uiHitAdd(button_x - 8, y - 8, GFX_WIDTH - button_x + 8, 46, HIT_POD_SEARCH, 0);
    return button_x - 16;
}

// "Subscribe" or "Subscribed", as a pill at x (right-aligned to right); returns its left.
static int drawSubscribe(int right, int y, bool subscribed, int arg)
{
    const char *label = subscribed ? T("Subscribed") : T("Subscribe");
    int w = uiPillWidth(label);
    int x = right - w;
    uiDrawPill(x, y, label, subscribed ? COLOR_SURFACE_HI : COLOR_ACCENT, subscribed ? COLOR_TEXT : COLOR_BG);
    uiHitAdd(x - 8, y - 10, w + 16, 50, HIT_POD_SUBSCRIBE, arg);
    return x;
}

static void drawChevron(int y)
{
    float cx = GFX_WIDTH - MARGIN - 20.0f, cy = y + UI_ROW_H / 2.0f;
    gfxLine(cx - 4, cy - 8, cx + 4, cy, 3, COLOR_FAINT);
    gfxLine(cx + 4, cy, cx - 4, cy + 8, 3, COLOR_FAINT);
}

// --- The modes: Yours, Top, Genres ---

static const char *const MODE_NAMES[POD_MODES] = {N_("Yours"), N_("Top"), N_("Genres")};

// The modes as pills, as Search's ways to search; returns where the space after them starts.
static int drawModes(const App *app)
{
    int x = MARGIN, y = SUBTITLE_Y - 6;
    uiDrawButton(x, y + 17.0f, "<", COLOR_SURFACE_HI, COLOR_DIM);
    x += uiButtonWidth("<") + 8;
    for (int i = 0; i < POD_MODES; i++) {
        bool on = (int)app->pod_page == i;
        int w = uiPillWidth(T(MODE_NAMES[i]));
        uiDrawPill(x, y + 2, T(MODE_NAMES[i]), on ? COLOR_ACCENT : COLOR_SURFACE_HI, on ? COLOR_BG : COLOR_DIM);
        if (!on)
            uiHitAdd(x - 4, y - 6, w + 8, 46, HIT_POD_MODE, i);
        x += w + 8;
    }
    uiDrawButton(x, y + 17.0f, ">", COLOR_SURFACE_HI, COLOR_DIM);
    return x + uiButtonWidth(">") + 24;
}

// The region's flag and name, as a pill ending at right; returns its left.
static int drawRegion(const App *app, int right, bool with_y)
{
    const char *code = appPodRegion(app);
    const char *name = appPodRegionName(code);
    int w = uiPillWidth(name) + 34, x = right - w, y = SUBTITLE_Y - 4;
    gfxFillRoundRect(x, y, w, 30, 15, COLOR_SURFACE_HI);
    char upper[3] = {(char)(code[0] - 'a' + 'A'), (char)(code[1] - 'a' + 'A'), 0};
    int text_x = uiDrawFlag(x + 12, y + 8, 20, 15, upper) ? x + 40 : x + 16;
    fontDrawMiddle(FONT_BOLD, 15, text_x, y + 15.0f, name, COLOR_TEXT);
    int left = x;
    if (with_y) {
        left = x - 8 - uiButtonWidth("Y");
        uiDrawButton(left, y + 15.0f, "Y", COLOR_SURFACE_HI, COLOR_DIM);
    }
    uiHitAdd(left - 8, y - 8, right - left + 16, 46, HIT_POD_REGION, 0);
    return left;
}

// The header of the modes: their pills, the region (Top and Genres), and the find button.
static int drawModesHeader(const App *app)
{
    int x = drawModes(app);
    int right = drawFindButton();
    if (app->pod_page == POD_PAGE_TOP || app->pod_page == POD_PAGE_GENRES)
        right = drawRegion(app, right, app->pod_page == POD_PAGE_GENRES) - 16;
    (void)x;
    return right;
}

// --- Your podcasts ---

static void drawSubs(const App *app)
{
    const PodStore *s = &app->pods;
    int left = drawModes(app);
    int right = drawFindButton();
    if (podFetchFeedsPending() > 0)
        fontDrawClipped(FONT_REGULAR, 16, left, SUBTITLE_Y + 1, right - left, T("Looking for new episodes\xE2\x80\xA6"),
                        COLOR_FAINT);
    if (s->sub_count == 0) {
        uiDrawListMessage(T("No podcasts yet."), T("Look in Top or Genres, or press X to find one."), false);
        return;
    }
    uiHitAdd(0, UI_LIST_Y, GFX_WIDTH, UI_LIST_ROWS * UI_ROW_H, HIT_LIST, 0);
    size_t cursor = app->pod_cursor[POD_PAGE_SUBS];
    int offset;
    size_t first = uiListBegin(&app->pod_cursor[POD_PAGE_SUBS], cursor, s->sub_count, &offset);
    for (size_t i = first; i < s->sub_count && i <= first + UI_LIST_ROWS; i++) {
        const Subscription *sub = &s->subs[i];
        int y = UI_LIST_Y + (int)(i - first) * UI_ROW_H - offset;
        if (i == cursor)
            uiDrawRowSelection(y);
        uiHitAdd(MARGIN - 12, y, GFX_WIDTH - 2 * (MARGIN - 12), UI_ROW_H, HIT_POD_ROW, (int)i);
        drawPicture(MARGIN, y + (UI_ROW_H - ROW_LOGO) / 2, ROW_LOGO, sub->image, sub->title);
        int text_w = GFX_WIDTH - MARGIN - 60 - TEXT_X;
        fontDrawClipped(FONT_BOLD, 22, TEXT_X, y + 9, text_w, sub->title, COLOR_TEXT);
        int fresh = podStoreNewCount(sub), x = TEXT_X;
        if (fresh > 0) {
            char label[48];
            snprintf(label, sizeof(label), T("%d new"), fresh);
            x += fontDraw(FONT_BOLD, 17, x, y + 38, label, COLOR_ACCENT) + 12;
        }
        fontDrawClipped(FONT_REGULAR, 17, x, y + 38, TEXT_X + text_w - x, sub->author, COLOR_DIM);
        drawChevron(y);
    }
    uiListEnd();
    uiDrawScrollbar(first, s->sub_count);
}

// --- A search's answers ---

// Podcasts with a Subscribe pill each (a search's, a genre's, or the top ones, ranked).
static void drawPodcastRows(const App *app, const PodcastResults *r, PodPage page, bool ranked)
{
    uiHitAdd(0, UI_LIST_Y, GFX_WIDTH, UI_LIST_ROWS * UI_ROW_H, HIT_LIST, 0);
    size_t cursor = app->pod_cursor[page];
    int offset;
    size_t first = uiListBegin(&app->pod_cursor[page], cursor, r->count, &offset);
    int rank_w = ranked ? 40 : 0;
    for (size_t i = first; i < r->count && i <= first + UI_LIST_ROWS; i++) {
        const PodcastResult *p = &r->items[i];
        int y = UI_LIST_Y + (int)(i - first) * UI_ROW_H - offset;
        if (i == cursor)
            uiDrawRowSelection(y);
        uiHitAdd(MARGIN - 12, y, GFX_WIDTH - 2 * (MARGIN - 12), UI_ROW_H, HIT_POD_ROW, (int)i);
        if (ranked) {
            char rank[24];
            snprintf(rank, sizeof(rank), "%zu", i + 1);
            fontDrawCentered(FONT_BOLD, 20, MARGIN + 14.0f, y + UI_ROW_H / 2.0f, rank, i < 3 ? COLOR_ACCENT : COLOR_DIM);
        }
        drawPicture(MARGIN + rank_w, y + (UI_ROW_H - ROW_LOGO) / 2, ROW_LOGO, p->image, p->title);
        int pill_x = drawSubscribe(GFX_WIDTH - MARGIN, y + 17, podStoreFind(&app->pods, p->feed) >= 0, (int)i);
        int text_x = TEXT_X + rank_w, text_w = pill_x - 24 - text_x;
        fontDrawClipped(FONT_BOLD, 22, text_x, y + 9, text_w, p->title, COLOR_TEXT);
        char details[PODCAST_TEXT_MAX * 2 + 64], count[40] = "";
        if (p->episodes > 0)
            snprintf(count, sizeof(count), p->episodes == 1 ? T("%d episode") : T("%d episodes"), p->episodes);
        snprintf(details, sizeof(details), "%s%s%s%s%s", p->author, p->genre[0] ? " \xC2\xB7 " : "", p->genre,
                 count[0] ? " \xC2\xB7 " : "", count);
        fontDrawClipped(FONT_REGULAR, 17, text_x, y + 38, text_w, details, COLOR_DIM);
    }
    uiListEnd();
    uiDrawScrollbar(first, r->count);
}

static void drawResults(const App *app)
{
    int x = drawBack(MARGIN), right = drawFindButton();
    fontDrawClipped(FONT_BOLD, 18, x, SUBTITLE_Y, right - x, app->pod_results_title, COLOR_ACCENT);
    const PodcastResults *r = &app->pod_results;
    bool genre = app->pod_genre >= 0;
    if (app->pod_searching && app->pod_wait == POD_WAIT_RESULTS)
        uiDrawListMessage(T("Looking for podcasts\xE2\x80\xA6"), "", false);
    else if (app->pod_search_failed)
        uiDrawListMessage(T("The podcast search can't be reached."), app->pod_error, true);
    else if (r->count == 0)
        uiDrawListMessage(T("No podcasts found."), genre ? T("Try another region.") : T("Try other words."), false);
    else
        drawPodcastRows(app, r, POD_PAGE_RESULTS, genre);
}

static void drawTop(const App *app)
{
    drawModesHeader(app);
    const PodcastResults *r = &app->pod_top;
    if (app->pod_top_failed)
        uiDrawListMessage(T("The podcast charts can't be reached."), app->pod_error, true);
    else if (r->count == 0)
        uiDrawListMessage(T("Loading the top podcasts\xE2\x80\xA6"), "", false);
    else
        drawPodcastRows(app, r, POD_PAGE_TOP, true);
}

static void drawGenres(const App *app)
{
    drawModesHeader(app);
    uiHitAdd(0, UI_LIST_Y, GFX_WIDTH, UI_LIST_ROWS * UI_ROW_H, HIT_LIST, 0);
    size_t cursor = app->pod_cursor[POD_PAGE_GENRES];
    int offset;
    size_t first = uiListBegin(&app->pod_cursor[POD_PAGE_GENRES], cursor, POD_GENRE_COUNT, &offset);
    for (size_t i = first; i < POD_GENRE_COUNT && i <= first + UI_LIST_ROWS; i++) {
        int y = UI_LIST_Y + (int)(i - first) * UI_ROW_H - offset;
        if (i == cursor)
            uiDrawRowSelection(y);
        uiHitAdd(MARGIN - 12, y, GFX_WIDTH - 2 * (MARGIN - 12), UI_ROW_H, HIT_POD_ROW, (int)i);
        uiDrawGenreTile(MARGIN, y + (UI_ROW_H - ROW_LOGO) / 2, ROW_LOGO, POD_GENRES[i].icon);
        fontDrawClipped(FONT_BOLD, 22, TEXT_X, y + 9, GFX_WIDTH - MARGIN - 60 - TEXT_X, T(POD_GENRES[i].name),
                        COLOR_TEXT);
        fontDrawClipped(FONT_REGULAR, 17, TEXT_X, y + 38, GFX_WIDTH - MARGIN - 60 - TEXT_X,
                        appPodRegionName(appPodRegion(app)), COLOR_DIM);
        drawChevron(y);
    }
    uiListEnd();
    uiDrawScrollbar(first, POD_GENRE_COUNT);
}

static void drawRegions(const App *app)
{
    int x = drawBack(MARGIN);
    fontDrawClipped(FONT_BOLD, 18, x, SUBTITLE_Y, GFX_WIDTH - MARGIN - x, T("Where the top podcasts come from"),
                    COLOR_ACCENT);
    const char *current = appPodRegion(app);
    uiHitAdd(0, UI_LIST_Y, GFX_WIDTH, UI_LIST_ROWS * UI_ROW_H, HIT_LIST, 0);
    size_t cursor = app->pod_cursor[POD_PAGE_REGIONS];
    int offset;
    size_t first = uiListBegin(&app->pod_cursor[POD_PAGE_REGIONS], cursor, POD_REGION_COUNT, &offset);
    for (size_t i = first; i < POD_REGION_COUNT && i <= first + UI_LIST_ROWS; i++) {
        const PodRegion *region = &POD_REGIONS[i];
        int y = UI_LIST_Y + (int)(i - first) * UI_ROW_H - offset;
        if (i == cursor)
            uiDrawRowSelection(y);
        uiHitAdd(MARGIN - 12, y, GFX_WIDTH - 2 * (MARGIN - 12), UI_ROW_H, HIT_POD_ROW, (int)i);
        char upper[3] = {(char)(region->code[0] - 'a' + 'A'), (char)(region->code[1] - 'a' + 'A'), 0};
        uiDrawFlag(MARGIN + 5, y + (UI_ROW_H - 30) / 2, 40, 30, upper);
        bool on = !strcmp(region->code, current);
        fontDrawMiddle(on ? FONT_BOLD : FONT_REGULAR, 22, TEXT_X, y + UI_ROW_H / 2.0f, region->name,
                       on ? COLOR_ACCENT : COLOR_TEXT);
        if (on) {  // A tick: the one in use.
            float cx = GFX_WIDTH - MARGIN - 20.0f, cy = y + UI_ROW_H / 2.0f;
            gfxLine(cx - 8, cy, cx - 2, cy + 6, 3, COLOR_ACCENT);
            gfxLine(cx - 2, cy + 6, cx + 9, cy - 6, 3, COLOR_ACCENT);
        }
    }
    uiListEnd();
    uiDrawScrollbar(first, POD_REGION_COUNT);
}

// --- A podcast's episodes ---

// "2026-11-22 · 45 min · 12 min left", or "· Heard".
static void episodeDetails(const App *app, const Episode *e, char *out, size_t size, bool *heard)
{
    char date[24] = "", length[32] = "", state[48] = "";
    if (e->published > 0) {
        time_t t = (time_t)e->published;
        struct tm tm;
        gmtime_r(&t, &tm);
        strftime(date, sizeof(date), "%Y-%m-%d", &tm);
    }
    int minutes = (e->duration + 30) / 60;
    if (e->duration > 0)
        snprintf(length, sizeof(length), minutes >= 60 ? T("%d h %02d min") : T("%d min"),
                 minutes >= 60 ? minutes / 60 : minutes, minutes % 60);
    int position;
    podStoreMark(&app->pods, app->pod_open.feed, e->guid, &position, heard);
    if (*heard)
        snprintf(state, sizeof(state), "%s", T("Heard"));
    else if (position > 0 && e->duration > position)
        snprintf(state, sizeof(state), T("%d min left"), (e->duration - position + 59) / 60);
    else if (position > 0)
        snprintf(state, sizeof(state), "%s", T("Started"));
    const char *parts[3] = {date, length, state};
    size_t n = 0;
    out[0] = '\0';
    for (int i = 0; i < 3; i++) {
        if (!parts[i][0])
            continue;
        n += (size_t)snprintf(out + n, n < size ? size - n : 0, "%s%s", n ? " \xC2\xB7 " : "", parts[i]);
        if (n >= size)
            break;
    }
}

static bool isPlayingEpisode(const RadioStatus *radio, const char *feed, const Episode *e)
{
    return radio->has_episode && !radio->ended && radio->state != RADIO_STOPPED && radio->state != RADIO_FAILED &&
           !strcmp(radio->episode.feed, feed) && !strcmp(radio->episode.guid, e->guid);
}

// A tick in a circle: heard, or not.
static void drawHeard(int y, bool heard, int index)
{
    float cx = GFX_WIDTH - MARGIN - HEARD_W / 2.0f, cy = y + UI_ROW_H / 2.0f;
    if (heard) {
        gfxFillCircle((int)cx, (int)cy, 13, COLOR_HEARD);
        gfxLine(cx - 6, cy + 1, cx - 2, cy + 5, 3, COLOR_BG);
        gfxLine(cx - 2, cy + 5, cx + 6, cy - 4, 3, COLOR_BG);
    } else {
        gfxArc(cx, cy, 12, 2.5f, 0, 6.2832f, COLOR_FAINT);
    }
    uiHitAdd(GFX_WIDTH - MARGIN - HEARD_W, y, HEARD_W + MARGIN - 12, UI_ROW_H, HIT_POD_HEARD, index);
}

static void drawEpisodes(const App *app, const RadioStatus *radio, uint64_t now_ns)
{
    int x = drawBack(MARGIN);
    bool subscribed = appPodOpenSub(app) >= 0;
    int pill_x = drawSubscribe(GFX_WIDTH - MARGIN, SUBTITLE_Y - 4, subscribed, -1);
    int button_x = pill_x - 8 - uiButtonWidth("Y");
    uiDrawButton(button_x, SUBTITLE_Y + 11.0f, "Y", COLOR_SURFACE_HI, COLOR_DIM);
    fontDrawClipped(FONT_BOLD, 18, x, SUBTITLE_Y, button_x - 16 - x, app->pod_open.title, COLOR_ACCENT);
    size_t count;
    const Episode *list = appPodEpisodes(app, &count);
    if (count == 0) {
        if (app->pod_loading)
            uiDrawListMessage(T("Loading episodes\xE2\x80\xA6"), "", false);
        else if (app->pod_load_failed)
            uiDrawListMessage(T("This podcast can't be read right now."), app->pod_error, true);
        else
            uiDrawListMessage(T("No episodes to play."), T("This podcast has nothing to listen to yet."), false);
        return;
    }
    uiHitAdd(0, UI_LIST_Y, GFX_WIDTH, UI_LIST_ROWS * UI_ROW_H, HIT_LIST, 0);
    size_t cursor = app->pod_cursor[POD_PAGE_EPISODES];
    int offset;
    size_t first = uiListBegin(&app->pod_cursor[POD_PAGE_EPISODES], cursor, count, &offset);
    for (size_t i = first; i < count && i <= first + UI_LIST_ROWS; i++) {
        const Episode *e = &list[i];
        int y = UI_LIST_Y + (int)(i - first) * UI_ROW_H - offset;
        if (i == cursor)
            uiDrawRowSelection(y);
        uiHitAdd(MARGIN - 12, y, GFX_WIDTH - 2 * (MARGIN - 12) - HEARD_W, UI_ROW_H, HIT_POD_ROW, (int)i);
        int text_x = MARGIN, text_w = GFX_WIDTH - MARGIN - HEARD_W - 16 - text_x;
        bool playing = isPlayingEpisode(radio, app->pod_open.feed, e);
        if (playing) {
            uiDrawLiveBars(text_x, y + 14, 18, now_ns, COLOR_ACCENT);
            text_x += 30;
            text_w -= 30;
        }
        bool heard;
        char details[160];
        episodeDetails(app, e, details, sizeof(details), &heard);
        bool fresh = e->published > 0 && subscribed && !heard &&
                     e->published > app->pods.subs[appPodOpenSub(app)].seen;
        fontDrawClipped(FONT_BOLD, 21, text_x, y + 9, text_w, e->title,
                        playing ? COLOR_ACCENT : heard ? COLOR_DIM : COLOR_TEXT);
        int dx = 0;
        if (fresh) {
            dx = fontDraw(FONT_BOLD, 17, MARGIN, y + 38, T("New"), COLOR_ACCENT) + 12;
        }
        fontDrawClipped(FONT_REGULAR, 17, MARGIN + dx, y + 38, GFX_WIDTH - MARGIN - HEARD_W - 16 - MARGIN - dx, details,
                        COLOR_DIM);
        drawHeard(y, heard, (int)i);
    }
    uiListEnd();
    uiDrawScrollbar(first, count);
}

void uiDrawPodcasts(const App *app, const RadioStatus *radio, uint64_t now_ns)
{
    switch (app->pod_page) {
    case POD_PAGE_TOP: drawTop(app); break;
    case POD_PAGE_GENRES: drawGenres(app); break;
    case POD_PAGE_RESULTS: drawResults(app); break;
    case POD_PAGE_EPISODES: drawEpisodes(app, radio, now_ns); break;
    case POD_PAGE_REGIONS: drawRegions(app); break;
    default: drawSubs(app); break;
    }
}

const char *uiPodcastsHelp(const App *app)
{
    switch (app->pod_page) {
    case POD_PAGE_TOP:
        return T("[A] episodes  [Y] subscribe  [<>] yours, top, genres  [X] find a podcast  [ZL][ZR] tabs  [+] menu");
    case POD_PAGE_GENRES:
        return T("[A] open  [Y] region  [<>] yours, top, genres  [X] find a podcast  [ZL][ZR] tabs  [+] menu");
    case POD_PAGE_RESULTS: return T("[A] episodes  [Y] subscribe  [X] search again  [B] back  [ZL][ZR] tabs  [+] menu");
    case POD_PAGE_EPISODES:
        return T("[A] play  [X] heard  [Y] subscribe  [B] back  [L][R] jump  [-] pause  [+] menu");
    case POD_PAGE_REGIONS: return T("[A] choose  [B] back  [ZL][ZR] tabs  [+] menu");
    default: return T("[A] episodes  [<>] yours, top, genres  [X] find a podcast  [ZL][ZR] tabs  [+] menu");
    }
}

// --- An episode playing ---

// Two bars: pausing.
static void drawPauseButton(float cx, float cy, float r, Color bg, Color fg)
{
    gfxFillCircle((int)cx, (int)cy, (int)r, bg);
    int w = (int)lroundf(r * 0.2f), h = (int)lroundf(r * 0.8f), gap = (int)lroundf(r * 0.16f);
    gfxFillRoundRect((int)cx - gap - w, (int)cy - h / 2, w, h, w / 3, fg);
    gfxFillRoundRect((int)cx + gap, (int)cy - h / 2, w, h, w / 3, fg);
}

// A round arrow with the seconds in it: back (seconds < 0) or ahead.
static void drawJump(int cx, int cy, int r, int seconds, Color color)
{
    bool ahead = seconds > 0;
    float start = ahead ? 4.4f : -1.3f, end = ahead ? 10.9f : 5.0f;
    gfxArc((float)cx, (float)cy, (float)r, 2.5f, start, end, color);
    float tip = ahead ? start : end;
    float tx = cx + r * cosf(tip), ty = cy + r * sinf(tip);
    float dir = ahead ? -1.0f : 1.0f;
    gfxTriangle(tx - 6, ty - 5 * dir, tx + 6, ty - 5 * dir, tx, ty + 6 * dir, color);
    char label[16];
    snprintf(label, sizeof(label), "%d", seconds < 0 ? -seconds : seconds);
    fontDrawCentered(FONT_BOLD, r * 3 / 4, (float)cx, (float)cy + 1, label, color);
}

static const char *episodeState(const RadioStatus *radio)
{
    if (radio->ended)
        return T("Played to the end");
    if (radio->state == RADIO_FAILED)
        return T("Can't play");
    if (radio->paused)
        return T("Paused");
    switch (radio->state) {
    case RADIO_CONNECTING: return T("Loading\xE2\x80\xA6");
    case RADIO_BUFFERING: return T("Buffering\xE2\x80\xA6");
    case RADIO_PLAYING: return T("Playing");
    default: return T("Stopped");
    }
}

static bool episodeRunning(const RadioStatus *radio)
{
    return !radio->paused && !radio->ended && radio->state != RADIO_STOPPED && radio->state != RADIO_FAILED;
}

// The progress bar from x to x + w at y, with the times under it when times.
static void drawProgress(const RadioStatus *radio, int x, int y, int w, int h, bool times)
{
    double fraction = radio->duration > 0 ? radio->position / radio->duration : 0;
    fraction = fraction < 0 ? 0 : fraction > 1 ? 1 : fraction;
    gfxFillRoundRect(x, y, w, h, h / 2, COLOR_SURFACE_HI);
    int filled = (int)(w * fraction);
    if (filled > 0)
        gfxFillRoundRect(x, y, filled < h ? h : filled, h, h / 2, COLOR_ACCENT);
    if (radio->duration > 0)
        uiHitAdd(x - 8, y - 18, w + 16, h + 36, HIT_SEEK, 0);
    if (!times)
        return;
    char at[16], length[16];
    uiFormatTime(radio->position, at, sizeof(at));
    uiFormatTime(radio->duration, length, sizeof(length));
    fontDraw(FONT_REGULAR, 17, x, y + h + 10, at, COLOR_DIM);
    if (radio->duration > 0)
        fontDraw(FONT_REGULAR, 17, x + w - fontTextWidth(FONT_REGULAR, 17, length), y + h + 10, length, COLOR_DIM);
}

static void drawControls(const RadioStatus *radio, int cx, int cy, int gap, float r)
{
    bool running = episodeRunning(radio);
    drawJump(cx - gap, cy, (int)(r * 0.62f), -EPISODE_BACK_S, COLOR_TEXT);
    uiHitAdd(cx - gap - 36, cy - 40, 72, 80, HIT_JUMP, -EPISODE_BACK_S);
    if (running)
        drawPauseButton((float)cx, (float)cy, r, COLOR_ACCENT, COLOR_BG);
    else
        uiDrawPlayButton((float)cx, (float)cy, r, false, COLOR_ACCENT, COLOR_BG);
    uiHitAdd(cx - (int)r - 6, cy - (int)r - 6, 2 * (int)r + 12, 2 * (int)r + 12, HIT_PLAY, 0);
    drawJump(cx + gap, cy, (int)(r * 0.62f), EPISODE_AHEAD_S, COLOR_TEXT);
    uiHitAdd(cx + gap - 36, cy - 40, 72, 80, HIT_JUMP, EPISODE_AHEAD_S);
}

void uiDrawEpisodeBar(const App *app, const RadioStatus *radio)
{
    (void)app;
    const RadioEpisode *e = &radio->episode;
    gfxFillRect(0, MINI_Y, GFX_WIDTH, MINI_H, COLOR_SURFACE);
    uiHitAdd(0, MINI_Y, GFX_WIDTH, MINI_H, HIT_MINI_PLAYER, 0);
    drawProgress(radio, 0, MINI_Y, GFX_WIDTH, 3, false);  // Along its top edge.
    int logo = 64;
    drawPicture(MARGIN - 8, MINI_Y + 14, logo, e->image, e->podcast);
    int text_x = MARGIN + logo + 8;
    int play_x = GFX_WIDTH / 2;
    int text_w = play_x - BAR_GAP - 48 - text_x;
    fontDrawClipped(FONT_BOLD, 22, text_x, MINI_Y + 16, text_w, e->title, COLOR_TEXT);
    char second[200];
    snprintf(second, sizeof(second), "%s \xC2\xB7 %s", e->podcast, episodeState(radio));
    fontDrawClipped(FONT_REGULAR, 19, text_x, MINI_Y + 50, text_w, second,
                    radio->state == RADIO_FAILED ? COLOR_ERROR : COLOR_DIM);
    drawControls(radio, play_x, BAR_CY, BAR_GAP, 26);
    char at[16], length[16], times[40];
    uiFormatTime(radio->position, at, sizeof(at));
    uiFormatTime(radio->duration, length, sizeof(length));
    snprintf(times, sizeof(times), radio->duration > 0 ? "%s / %s" : "%s", at, length);
    int w = uiPillWidth(times);
    uiDrawPill(GFX_WIDTH - MARGIN - w, BAR_CY - 15, times, COLOR_SURFACE_HI, COLOR_DIM);
}

// The episode's title over up to two lines.
static void drawTitle(const char *title, int y)
{
    if (fontTextWidth(FONT_BOLD, 30, title) <= NOW_W) {
        fontDraw(FONT_BOLD, 30, NOW_X, y, title, COLOR_TEXT);
        return;
    }
    char first[256];
    snprintf(first, sizeof(first), "%s", title);
    size_t cut = strlen(first);
    while (cut > 0) {
        while (cut > 0 && first[cut] != ' ')
            cut--;
        first[cut] = '\0';
        if (fontTextWidth(FONT_BOLD, 30, first) <= NOW_W)
            break;
    }
    if (cut == 0) {
        fontDrawClipped(FONT_BOLD, 30, NOW_X, y, NOW_W, title, COLOR_TEXT);
        return;
    }
    fontDraw(FONT_BOLD, 30, NOW_X, y, first, COLOR_TEXT);
    fontDrawClipped(FONT_BOLD, 30, NOW_X, y + 40, NOW_W, title + cut + 1, COLOR_TEXT);
}

void uiDrawEpisodeNow(const App *app, const RadioStatus *radio, uint64_t now_ns)
{
    (void)app;
    (void)now_ns;
    const RadioEpisode *e = &radio->episode;
    gfxFillRoundRect(NOW_LOGO_X - 6, NOW_LOGO_Y - 6, NOW_LOGO + 12, NOW_LOGO + 12, NOW_LOGO / 6 + 6, COLOR_SURFACE);
    drawPicture(NOW_LOGO_X, NOW_LOGO_Y, NOW_LOGO, e->image, e->podcast);
    int y = NOW_LOGO_Y + 4;
    uiDrawPill(NOW_X, y, episodeState(radio), radio->state == RADIO_PLAYING && !radio->paused ? COLOR_ACCENT
                                                                                                  : COLOR_SURFACE_HI,
               radio->state == RADIO_FAILED ? COLOR_ERROR
               : radio->state == RADIO_PLAYING && !radio->paused ? COLOR_BG
                                                                  : COLOR_DIM);
    y += 50;
    fontDrawClipped(FONT_REGULAR, 22, NOW_X, y, NOW_W, e->podcast, COLOR_ACCENT);
    y += 40;
    if (radio->state == RADIO_FAILED)
        fontDrawClipped(FONT_REGULAR, 20, NOW_X, y, NOW_W, T(radio->error), COLOR_ERROR);
    else
        drawTitle(e->title, y);
    y += 110;
    char format[64];
    uiFormatLine(radio, format, sizeof(format));
    if (format[0])
        uiDrawPill(NOW_X, y, format, COLOR_SURFACE_HI, COLOR_DIM);
    drawProgress(radio, NOW_X, 400, NOW_W, 8, true);
    drawControls(radio, NOW_X + NOW_W / 2, 510, 110, 38);
}
