#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "background.h"
#include "theme.h"

#define CONFIG_NAME "settings.txt"
#define PATH_MAX_LEN 256

static int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void configDefaults(Config *c)
{
    memset(c, 0, sizeof(*c));
    c->accent = 0;
    c->background_blur = BLUR_MEDIUM;
    c->clock_24h = true;
    c->start_view = START_AUTO;
}

static void applyLine(Config *c, char *line)
{
    line[strcspn(line, "\r\n")] = '\0';
    char *eq = strchr(line, '=');
    if (!eq)
        return;
    *eq = '\0';
    const char *key = line, *value = eq + 1;
    if (strcmp(key, "accent") == 0)
        c->accent = clampInt(atoi(value), 0, THEME_ACCENT_COUNT - 1);
    else if (strcmp(key, "background") == 0 && strlen(value) < sizeof(c->background) && !strchr(value, '/'))
        strcpy(c->background, value);
    else if (strcmp(key, "background_blur") == 0)
        c->background_blur = clampInt(atoi(value), 0, BLUR_COUNT - 1);
    else if (strcmp(key, "clock24") == 0)
        c->clock_24h = strcmp(value, "0") != 0;
    else if (strcmp(key, "screen_off") == 0)
        c->screen_off_minutes = clampInt(atoi(value), 0, 60);
    else if (strcmp(key, "start_view") == 0)
        c->start_view = clampInt(atoi(value), 0, START_COUNT - 1);
    else if (strcmp(key, "resume") == 0)
        c->resume = strcmp(value, "1") == 0;
}

void configLoad(Config *c, const char *folder)
{
    configDefaults(c);
    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/%s", folder, CONFIG_NAME);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[CONFIG_BACKGROUND_MAX + 32];
    while (fgets(line, sizeof(line), f)) {
        if (!strchr(line, '\n') && !feof(f)) {
            // Too long: drop the whole line so its tail isn't read as a new one.
            int ch;
            while ((ch = fgetc(f)) != EOF && ch != '\n') {
            }
            continue;
        }
        applyLine(c, line);
    }
    fclose(f);
}

bool configSave(const Config *c, const char *folder)
{
    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s", folder);
    char *slash = strrchr(path, '/');
    if (slash && slash != path) {
        *slash = '\0';
        mkdir(path, 0777);  // sdmc:/config, if it isn't there yet.
    }
    mkdir(folder, 0777);
    snprintf(path, sizeof(path), "%s/%s", folder, CONFIG_NAME);
    FILE *f = fopen(path, "w");
    if (!f)
        return false;
    fprintf(f, "accent=%d\nbackground_blur=%d\nclock24=%d\nscreen_off=%d\nstart_view=%d\nresume=%d\n", c->accent,
            c->background_blur, c->clock_24h ? 1 : 0, c->screen_off_minutes, c->start_view, c->resume ? 1 : 0);
    // A line break or a slash in the name would make another setting, or leave the folder.
    if (c->background[0] && !strpbrk(c->background, "\r\n/"))
        fprintf(f, "background=%s\n", c->background);
    return fclose(f) == 0;
}
