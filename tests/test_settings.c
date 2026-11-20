// Settings: kept on the card, the rows' steps, the accent colors and the
// background image; and logos claiming to be huge, now that stb's own limit
// is set for backgrounds.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "background.h"
#include "check.h"
#include "config.h"
#include "logo.h"
#include "settings.h"
#include "theme.h"

#define RGB(r, g, b) (0xFF000000u | (uint32_t)(b) << 16 | (uint32_t)(g) << 8 | (uint32_t)(r))

static void testConfig(void)
{
    const char *folder = TMP_ROOT "/settings";
    system("rm -rf " TMP_ROOT "/settings");
    mkdir(TMP_ROOT, 0777);
    Config c;
    configLoad(&c, folder);  // No file: the defaults.
    CHECK(c.accent == 0 && c.background_blur == BLUR_MEDIUM && c.clock_24h && !c.resume && c.start_view == START_AUTO);

    c.accent = 3;
    snprintf(c.background, sizeof(c.background), "%s", "beach.jpg");
    c.background_blur = BLUR_HIGH;
    c.clock_24h = false;
    c.screen_off_minutes = 5;
    c.start_view = START_POPULAR;
    c.resume = true;
    CHECK(configSave(&c, folder));
    Config back;
    configLoad(&back, folder);
    CHECK(memcmp(&c, &back, sizeof(c)) == 0);

    // Out of range values are clamped; a name with a slash (out of the folder) is dropped.
    FILE *f = fopen(TMP_ROOT "/settings/settings.txt", "w");
    if (f) {
        fprintf(f, "accent=99\nbackground=../../secret.png\nscreen_off=500\nstart_view=-3\nnonsense\nresume=1\n");
        fclose(f);
    }
    configLoad(&back, folder);
    CHECK(back.accent == THEME_ACCENT_COUNT - 1 && back.background[0] == '\0' && back.screen_off_minutes == 60);
    CHECK(back.start_view == START_AUTO && back.resume);
}

static void testRows(void)
{
    CHECK(settingsSleepStep(0, 1) == 5 && settingsSleepStep(7, -1) == 5 && settingsSleepStep(7, 1) == 10);
    CHECK(settingsSleepStep(0, -1) == 0 && settingsSleepStep(SLEEP_MAX_MINUTES, 1) == SLEEP_MAX_MINUTES);
    CHECK(settingsScreenOffStep(0, 1) == 1 && settingsScreenOffStep(0, -1) == 30 && settingsScreenOffStep(30, 1) == 0);
    for (int row = 0; row < SETTING_COUNT; row++)
        CHECK(settingsRowLabel(row)[0] && settingsRowHelp(row)[0]);
    CHECK(!settingsRowEditable(SETTING_ABOUT) && settingsRowEditable(SETTING_ACCENT));
    CHECK(settingsRowSection(SETTING_ACCENT) && settingsRowSection(SETTING_SLEEP) && !settingsRowSection(SETTING_CLOCK));
    CHECK(strcmp(settingsStartName(START_WORLD), "World") == 0 && strcmp(settingsStartName(99), "Automatic") == 0);
}

static void testTheme(void)
{
    CHECK(strcmp(themeAccentName(0), "Sky blue") == 0 && themeAccentName(THEME_ACCENT_COUNT)[0] == '\0');
    uint32_t sky = themeAccentColor(0, 0, false);
    CHECK(sky == RGB(0x4F, 0xC3, 0xF7) && themeAccentColor(1, 0, false) != sky);
    CHECK(themeAccentColor(THEME_ACCENT_FROM_LOGO, 0, false) == sky);  // No logo color: the default.
    uint32_t red = themeAccentColor(THEME_ACCENT_FROM_LOGO, RGB(0x80, 0x10, 0x10), true);
    CHECK((red & 0xFF) > 0xD0 && (red >> 8 & 0xFF) < 0x80);              // A dark red made vivid.
    CHECK(themeVivid(RGB(0x80, 0x80, 0x80)) == sky);                      // Gray has no hue.

    // A logo on its white tile: white and gray don't count; enough color does.
    uint32_t logo[100];
    for (int i = 0; i < 100; i++)
        logo[i] = i < 30 ? RGB(0x10, 0x40, 0xE0) : RGB(0xFF, 0xFF, 0xFF);
    uint32_t color = 0;
    CHECK(themeLogoColor(logo, 100, &color) && color == RGB(0x10, 0x40, 0xE0));
    for (int i = 0; i < 100; i++)
        logo[i] = i < 2 ? RGB(0xE0, 0x10, 0x10) : RGB(0x30, 0x30, 0x30);
    CHECK(!themeLogoColor(logo, 100, &color));  // 2% color: a gray logo.
    CHECK(!themeLogoColor(NULL, 0, &color));
}

static void testBackground(void)
{
    CHECK(strcmp(backgroundBlurName(BLUR_OFF), "Off") == 0 && strcmp(backgroundBlurName(99), "Medium") == 0);
    // A 4x2 picture, left red and right blue, filling 8x8: cropped to its middle, dimmed over the base.
    uint8_t rgba[4 * 2 * 4];
    for (int i = 0; i < 8; i++) {
        bool left = i % 4 < 2;
        uint8_t px[4] = {left ? 255 : 0, 0, left ? 0 : 255, 255};
        memcpy(rgba + i * 4, px, 4);
    }
    uint32_t out[8 * 8];
    CHECK(backgroundMake(rgba, 4, 2, out, 8, 8, RGB(0, 0, 0), 255, BLUR_OFF));
    CHECK((out[0] & 0xFF) > 0xC0 && (out[7] >> 16 & 0xFF) > 0xC0);
    CHECK(backgroundMake(rgba, 4, 2, out, 8, 8, RGB(0, 0, 0), 128, BLUR_OFF));
    CHECK((out[0] & 0xFF) < 0x90);  // Dimmed by half.
    for (int blur = BLUR_LOW; blur < BLUR_COUNT; blur++)  // Blurred: red and blue meet in the middle.
        CHECK(backgroundMake(rgba, 4, 2, out, 8, 8, RGB(0, 0, 0), 255, blur) && (out[4] & 0xFF) > 0 &&
              (out[3] >> 16 & 0xFF) > 0);

    // The folder: images only, by name; none and no folder load as "no background".
    const char *folder = TMP_ROOT "/settings";
    mkdir(TMP_ROOT "/settings/Backgrounds", 0777);
    const char *files[] = {"b.png", "A.jpg", "notes.txt", ".hidden.png", "c.JPEG"};
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        char path[256];
        snprintf(path, sizeof(path), "%s/Backgrounds/%s", folder, files[i]);
        FILE *f = fopen(path, "w");
        if (f)
            fclose(f);
    }
    char names[8][BACKGROUND_NAME_MAX];
    size_t n = backgroundList(folder, names, 8);
    CHECK(n == 3 && strcmp(names[0], "A.jpg") == 0 && strcmp(names[1], "b.png") == 0 && strcmp(names[2], "c.JPEG") == 0);
    CHECK(backgroundLoad(folder, "", BLUR_MEDIUM) && backgroundPixels() == NULL);
    CHECK(!backgroundLoad(folder, "b.png", BLUR_MEDIUM) && backgroundPixels() == NULL);  // Empty: not an image.
    CHECK(!backgroundLoad(folder, "../settings.txt", BLUR_MEDIUM));
    CHECK(backgroundList(TMP_ROOT "/nowhere", names, 8) == 0);
}

// A PNG that says it's 5000 pixels wide: refused before its pixels are made.
static void testHugeLogo(void)
{
    static const unsigned char HEADER[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x13, 0x88, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    uint32_t *px = malloc(LOGO_SIZE * LOGO_SIZE * sizeof(uint32_t));
    CHECK(px && !logoDecode(HEADER, sizeof(HEADER), px));
    free(px);
}

void testSettings(void)
{
    testConfig();
    testRows();
    testTheme();
    testBackground();
    testHugeLogo();
}
