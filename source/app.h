// State of the application shared by input handling (app.c) and drawing
// (ui*.c).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "notice.h"
#include "background.h"
#include "globe.h"
#include "mapzoom.h"
#include "kinetic.h"
#include "categories.h"
#include "config.h"
#include "directory.h"
#include "library.h"
#include "passport.h"
#include "podcast.h"
#include "podstore.h"
#include "radio.h"
#include "stations.h"
#include "sysinfo.h"
#include "touch.h"

typedef enum {
    VIEW_WORLD,  // The map: countries lit by their stations.
    VIEW_POPULAR,
    VIEW_SEARCH,
    VIEW_FAVORITES,
    VIEW_RECENT,
    VIEW_PODCASTS,
    VIEW_NOW_PLAYING,
    VIEW_COUNT,
} View;

// What the Podcasts tab shows. Its three modes (left and right): the
// podcasts subscribed to, the region's top podcasts, and the genres; and
// from them, a search's or a genre's podcasts, a podcast's episodes, or the
// regions to choose from.
typedef enum {
    POD_PAGE_SUBS,
    POD_PAGE_TOP,
    POD_PAGE_GENRES,
    POD_PAGE_RESULTS,
    POD_PAGE_EPISODES,
    POD_PAGE_REGIONS,
    POD_PAGES,
} PodPage;
#define POD_MODES 3  // The first pages: Yours, Top, Genres.

// What the fetcher's search or chart answers are for.
typedef enum {
    POD_WAIT_NONE,
    POD_WAIT_TOP,
    POD_WAIT_RESULTS,
} PodWait;

#define EPISODE_BACK_S 15   // L, and the button left of play: back this far.
#define EPISODE_AHEAD_S 30  // R: ahead this far.

// How Search finds stations: by name (typed), or by browsing a category.
typedef enum {
    SEARCH_NAME,
    SEARCH_GENRE,
    SEARCH_COUNTRY,
    SEARCH_LANGUAGE,
    SEARCH_MODES,
} SearchMode;

// What a directory answer is for.
typedef enum {
    TARGET_POPULAR,
    TARGET_NAME_RESULTS,
    TARGET_CATEGORY_RESULTS,
    TARGET_CATEGORIES,  // Of waiting_mode.
    TARGET_RANDOM,      // A random country's stations: one of them plays.
} Target;

#define SEARCH_MAX 64
#define CONFIG_FOLDER_MAX 128

// A panel over the view: the + menu, its "Exit KaiRadio?", Settings,
// About, or the passport (from the World view). Only it takes input.
typedef enum {
    OVERLAY_NONE,
    OVERLAY_MENU,
    OVERLAY_EXIT,
    OVERLAY_SETTINGS,
    OVERLAY_ABOUT,
    OVERLAY_PASSPORT,
} Overlay;

// The + menu's items.
typedef enum {
    MENU_SETTINGS,
    MENU_EXIT,  // Asks "Exit KaiRadio?".
    MENU_COUNT,
} MenuItem;

// The answers to "Exit KaiRadio?".
typedef enum {
    EXIT_CONFIRM,
    EXIT_CANCEL,
    EXIT_COUNT,
} ExitItem;

typedef struct {
    View view;
    View list_view;             // The list view Now Playing goes back to.
    size_t cursor[VIEW_COUNT];  // Selected row of each station list.

    StationList popular;
    bool popular_asked;

    // Search.
    SearchMode search_mode;
    char query[SEARCH_MAX];
    StationList name_results;
    CategoryList categories[SEARCH_MODES];  // Genres, countries, languages (not SEARCH_NAME).
    bool categories_asked[SEARCH_MODES];
    size_t category_cursor[SEARCH_MODES];
    CategorySort category_sort[SEARCH_MODES];  // How each list of them is sorted (Y changes it).
    bool category_open;                     // Showing the stations of open_category.
    bool category_from_map;                 // Opened from the World view: Back goes there.
    Category open_category;

    // The World view: the country chosen, in categories[SEARCH_COUNTRY].
    size_t map_cursor;
    StationList random_results;  // The random country's stations: one played, L and R go through the rest.
    char weather_code[4];        // The country picked when its weather was last asked for, and since when.
    uint64_t weather_picked_ns;
    unsigned random_seed;
    // As a globe: which way it faces, where it's turning to, and its spin after a flick.
    GlobeView globe_view, globe_target;
    bool globe_turning;          // Turning to globe_target (a country chosen).
    Kinetic globe_spin;
    bool globe_dragging;
    uint64_t globe_touched_ns;   // The last time it was touched or turned: it turns by itself after a while.
    uint64_t globe_frame_ns;
    // Zoom (app_map.c): the flat map's (the globe's is in globe_view), and two fingers on the screen.
    MapZoom map_zoom;
    bool pinching;               // Two fingers: zooming.
    bool touch_held;             // After two fingers, until every finger is up: no tap, no drag.
    float pinch_distance;        // The fingers' distance when they came down, and the zoom then.
    float pinch_zoom;
    float pinch_x, pinch_y;      // Their middle last frame: the map follows it.
    StationList category_results;

    // The directory request that the app waits for, and what it is for.
    unsigned waiting;
    Target waiting_target;
    SearchMode waiting_mode;
    bool waiting_active;
    DirectoryStatus directory;
    // Requests a newer one replaced before they were answered: asked again
    // when what they fill shows (leaving World while it lit up, say).
    bool stale_popular, stale_categories[SEARCH_MODES], stale_category_results, stale_name_results;

    Library library;
    Passport passport;           // The countries and stations listened to.
    int passport_row;            // The passport's first row of stamps showing.

    // Podcasts (app_podcast.c).
    PodStore pods;
    PodPage pod_page;
    PodPage pod_from;              // Where the episodes page goes back to,
    PodPage pod_results_from;      // the results page,
    PodPage pod_regions_from;      // and the regions page.
    size_t pod_cursor[POD_PAGES];
    char pod_query[SEARCH_MAX];
    PodcastResults pod_results;    // A search's, or a genre's top podcasts.
    char pod_results_title[PODCAST_TITLE_MAX];  // What they are ("Podcasts for "x"", "News").
    int pod_genre;                 // The genre shown (an index of the genres), or -1 for a search.
    PodcastResults pod_top;        // The region's top podcasts,
    char pod_top_region[3];        // for this region,
    bool pod_top_failed;           // and whether they couldn't be had.
    PodWait pod_wait;              // What the fetcher's answer is for,
    unsigned pod_search;           // its request,
    bool pod_searching, pod_search_failed;  // and how the results' one went.
    PodcastResult pod_open;        // The podcast whose episodes show.
    Feed pod_feed;                 // Its episodes while it isn't subscribed to.
    bool pod_loading, pod_load_failed;
    char pod_error[256];           // Why the search or the feed failed.
    uint64_t pod_saved_ns;         // When the playing episode's place was last kept.
    char pod_marked[LINK_URL_MAX + PODCAST_GUID_MAX];  // The episode last marked heard by playing to its end.

    // The list the playing station was chosen from, for previous and next.
    StationList play_list;
    size_t play_index;

    // Settings, the + menu and the timers.
    Config config;
    char config_folder[CONFIG_FOLDER_MAX];
    Overlay overlay;
    int menu_cursor;             // MenuItem, or ExitItem in the exit question.
    int settings_page;           // SettingsPage.
    int settings_cursor;         // SettingRow, on settings_page.
    char backgrounds[BACKGROUND_MAX_IMAGES][BACKGROUND_NAME_MAX];  // The images there were when Settings opened.
    size_t background_count;
    int sleep_minutes;           // The sleep timer as set; 0 is off.
    uint64_t sleep_until_ns;     // When it stops the radio; 0 for never.
    bool screen_off;             // Turned off after a while idle; any input turns it on.
    uint64_t last_input_ns;
    unsigned update_seen;        // The update status's serial already said on screen.
    bool update_manual;          // Settings asked: say "up to date" and failures too.

    SysInfo sys;
    TouchTracker touch;
    Kinetic kinetic;   // A list following a finger, or gliding on after it.
    bool swallow_tap;  // A press stopped a glide: its tap doesn't open anything.
    Notices notices;             // Messages, shown one at a time in a bubble at the top.
    NoticeView notice_views[NOTICE_STACK + 1];  // The bubbles to draw this frame,
    int notice_count;            // and how many.
    bool quit;
} App;

// The stations a view shows now, or NULL (Now Playing, or Search browsing
// categories).
const StationList *appList(const App *app, View view);
// True while Search shows a list of genres, countries or languages.
bool appBrowsingCategories(const App *app);
// True while the app waits for the directory to fill what the view shows.
bool appLoading(const App *app);
// True if the last request for what the view shows failed.
bool appFailed(const App *app);

void appInit(App *app, const char *config_folder);
void appFree(App *app);

// A short message in the notice bubble at the top: something to know, done, or failed.
void appNotify(App *app, NoticeKind kind, const char *message, uint64_t now_ns);
// The same, as something to know.
void appToast(App *app, const char *message, uint64_t now_ns);
// Somewhere unexpected: a random country (on the globe, with a few stations),
// the globe turned to it, and one of its stations playing.
void appRandomStation(App *app, uint64_t now_ns);

// Once per frame, before drawing: takes directory answers.
void appUpdate(App *app, uint64_t now_ns);

// app_update.c: Settings > Updates. Start: the daily look, if due (nro_path: argv[0]).
void appStartUpdates(App *app, const char *nro_path);
void appStopUpdates(void);
// Once per frame: what the look or the install found, said on screen.
void appUpdatesTick(App *app, uint64_t now_ns);
// A on "Look for an update": looks, or installs the version found.
void appUpdatesActivate(App *app, uint64_t now_ns);
// The daily look on or off.
void appUpdatesToggle(App *app, uint64_t now_ns);
// What the Updates rows show.
void appUpdatesValue(const App *app, int row, char *out, size_t size);

// Buttons pressed this frame (with auto-repeat), and touch input.
void appHandleButtons(App *app, uint64_t pressed, uint64_t now_ns);
void appHandleTouch(App *app, bool touching, int x, int y, uint64_t now_ns);
// The World view's globe, each frame: turning to a country, gliding, or turning slowly by itself.
void appGlobeTick(App *app, uint64_t now_ns);

// app_map.c: the World view's zoom. The right stick up and down zoom in and
// out on the map; returns pressed without them.
uint64_t appMapZoomButtons(App *app, uint64_t pressed);
// Two fingers on the screen, each frame (count: how many touch it; the first
// two at (x0, y0) and (x1, y1), layout pixels). True while they own the
// touch: from two fingers down until every finger is up, nothing else gets it.
bool appMapPinch(App *app, int count, float x0, float y0, float x1, float y1);
// A finger dragging the zoomed flat map moves it. True if it took the event.
bool appMapTouch(App *app, const TouchEvent *e);
// After the stick picks a country on the zoomed flat map: it's moved into view.
void appMapFollowPick(App *app);
// A tap on the flat map at screen (x, y): the map's point there, as mapToLayout's.
void appMapTapPoint(const App *app, float x, float y, float *mx, float *my);
// What a tap at (x, y) does, against the last frame's targets. For the
// preview's checks too. Returns false if nothing is there.
bool appTap(App *app, int x, int y, uint64_t now_ns);

void appSetView(App *app, View view);

// Plays a station of a list, keeping the list for previous and next.
void appPlayFrom(App *app, const StationList *list, size_t index);
// The passport, over the World view (the left stick's press or its button).
void appOpenPassport(App *app);
// Its stamps rows up (less than 0) or down, kept within them (app_settings.c).
void appScrollPassport(App *app, int rows);

// app_settings.c: the + menu, Settings and About.
void appOpenMenu(App *app);
// "Exit KaiRadio?": Exit or Cancel (+ there exits).
void appAskExit(App *app);
// A menu item chosen (MenuItem, or ExitItem in the exit question).
void appChooseMenuItem(App *app, int item);
void appOpenSettings(App *app);
// Back from a Settings page to the main one (on the row that opened it), or Settings closed.
void appSettingsBack(App *app);
void appHandleOverlay(App *app, uint64_t pressed, uint64_t now_ns);
// Steps a setting's value by delta (left -1, right +1) and saves the settings.
// Finds the languages (English, those in the app, config/lang's files) and uses the one chosen.
void appStartLanguages(App *app);
// Uses app->config.language (English if it's gone), and names it for Settings.
void appUseLanguage(App *app);
void appChangeSetting(App *app, int row, int delta, uint64_t now_ns);
// A on a row: a page or About opens, the others step forward.
void appActivateSetting(App *app, int row, uint64_t now_ns);
// The value a row shows ("Sky blue", "Off", "in 42 min").
void appSettingValue(const App *app, int row, uint64_t now_ns, char *out, size_t size);
// Checks the sleep timer: stops the radio when it runs out.
void appCheckSleep(App *app, uint64_t now_ns);
// Opens the stations of the country at index of categories[SEARCH_COUNTRY]
// (in Search), from the World view.
void appOpenMapCountry(App *app, size_t index);
// Plays a station at random from the country at index (a tap on the map).
void appRandomStationIn(App *app, size_t index, uint64_t now_ns);
void appSetSearchMode(App *app, SearchMode mode);

// app_podcast.c: the Podcasts tab, and episodes playing.
// Start: the podcasts kept on the card, and their feeds asked for anew.
void appPodcastsStart(App *app);
// The playing episode's place kept; the fetcher stopped.
void appPodcastsStop(App *app);
// Once per frame: searches and feeds that came, and the playing episode's place.
void appPodcastsUpdate(App *app, uint64_t now_ns);
void appPodcastsButtons(App *app, uint64_t pressed, uint64_t now_ns);
// A tap on one of the tab's targets (or the episode's controls). False if it isn't one.
bool appPodcastsTap(App *app, int kind, int arg, int x, int y, uint64_t now_ns);
// Up or down the page's list.
void appPodMove(App *app, long delta);
// Try again: the search, or the open podcast's feed.
void appPodcastsRetry(App *app);
// The episodes the episodes page lists (the subscription's, or the feed read for it).
const Episode *appPodEpisodes(const App *app, size_t *count);
// The open podcast's subscription, or -1.
int appPodOpenSub(const App *app);
// The rows the page lists now.
size_t appPodCount(const App *app);
// The region of the charts: the one chosen, or the language's.
const char *appPodRegion(const App *app);
// The genres and regions the tab offers.
typedef struct {
    int id;              // Apple's.
    const char *name;    // English (shown with T()).
    const char *icon;    // A word that picks its icon (categoryicons.h).
} PodGenre;
typedef struct {
    const char *code;    // "mx".
    const char *name;    // In its own language ("México").
} PodRegion;
extern const PodGenre POD_GENRES[];
extern const size_t POD_GENRE_COUNT;
extern const PodRegion POD_REGIONS[];
extern const size_t POD_REGION_COUNT;
// The name of a region's code, or the code.
const char *appPodRegionName(const char *code);
// True while the top podcasts of the region are asked for.
bool appPodTopLoading(const App *app);
// An episode playing (or paused, or ended): L, R and play act on it.
bool appEpisodeActive(void);
// Jumps by seconds in the episode playing.
void appEpisodeJump(App *app, double seconds);
// Pauses or goes on; from the start after its end; again after a failure.
void appEpisodeToggle(App *app);
// The next way to sort the genres, countries or languages shown, keeping the
// one selected selected.
void appCycleSort(App *app, uint64_t now_ns);
