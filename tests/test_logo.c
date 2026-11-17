// Station logos, and the favorites and recent lists on the card.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "library.h"
#include "logo.h"

// 4x2 RGBA: the left half opaque red, the right half transparent.
static const unsigned char PNG[] = {
    0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x04,0x00,0x00,
    0x00,0x02,0x08,0x06,0x00,0x00,0x00,0x7F,0xA8,0x7D,0x63,0x00,0x00,0x00,0x13,0x49,0x44,0x41,0x54,0x78,0x9C,0x63,
    0xF8,0xCF,0xC0,0xF0,0x1F,0x84,0x19,0x60,0x00,0x5D,0x00,0x00,0xA7,0x7A,0x07,0xF9,0xC1,0x3A,0x36,0xB3,0x00,0x00,
    0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82,
};

static uint32_t at(const uint32_t *px, int x, int y)
{
    return px[y * LOGO_SIZE + x];
}

static void checkLogo(const uint32_t *px)
{
    // Wider than tall: centered, with white above and below.
    CHECK(at(px, LOGO_SIZE / 2, 2) == 0xFFFFFFFFu);
    CHECK(at(px, 2, LOGO_SIZE / 2) == 0xFFFFFFFFu);
    // The left part red, the transparent part laid on white.
    CHECK(at(px, LOGO_SIZE / 4, LOGO_SIZE / 2) == 0xFF0000FFu);
    CHECK(at(px, LOGO_SIZE * 3 / 4, LOGO_SIZE / 2) == 0xFFFFFFFFu);
}

void testLogos(void)
{
    uint32_t *px = malloc(LOGO_SIZE * LOGO_SIZE * sizeof(uint32_t));
    CHECK(logoDecode(PNG, sizeof(PNG), px));
    checkLogo(px);

    // The same PNG inside an ICO, after a smaller entry that isn't a PNG.
    unsigned char ico[6 + 32 + 8 + sizeof(PNG)];
    memset(ico, 0, sizeof(ico));
    ico[2] = 1;
    ico[4] = 2;
    unsigned char *e = ico + 6;
    e[0] = 1, e[1] = 1, e[8] = 8, e[12] = 38;          // 1x1, 8 bytes at 38: not a PNG.
    e += 16;
    e[0] = 4, e[1] = 2, e[8] = sizeof(PNG), e[12] = 46;  // 4x2, the PNG at 46.
    memcpy(ico + 46, PNG, sizeof(PNG));
    memset(px, 0, LOGO_SIZE * LOGO_SIZE * sizeof(uint32_t));
    CHECK(logoDecode(ico, sizeof(ico), px));
    checkLogo(px);

    CHECK(!logoDecode((const unsigned char *)"<svg></svg>", 11, px));
    CHECK(!logoDecode(PNG, 20, px));  // Cut short.
    CHECK(!logoDecode(NULL, 0, px));
    unsigned char bad_ico[22] = {0, 0, 1, 0, 1, 0};
    bad_ico[6 + 12] = 200;  // Offset past the end.
    CHECK(!logoDecode(bad_ico, sizeof(bad_ico), px));
    free(px);
}

static Station named(const char *uuid, const char *name)
{
    Station s;
    memset(&s, 0, sizeof(s));
    snprintf(s.uuid, sizeof(s.uuid), "%s", uuid);
    snprintf(s.name, sizeof(s.name), "%s", name);
    snprintf(s.url, sizeof(s.url), "http://%s.example/live", uuid);
    return s;
}

void testLibrary(void)
{
    const char *folder = TMP_ROOT "/config/kairadio";
    system("rm -rf " TMP_ROOT);
    Library lib;
    libraryLoad(&lib, folder);
    CHECK(lib.favorites.count == 0 && lib.recent.count == 0);
    Station a = named("a", "Alpha"), b = named("b", "Beta");
    CHECK(libraryToggleFavorite(&lib, &a));
    CHECK(libraryToggleFavorite(&lib, &b));
    CHECK(!libraryToggleFavorite(&lib, &a));
    CHECK(libraryIsFavorite(&lib, &b) && !libraryIsFavorite(&lib, &a));
    for (int i = 0; i < RECENT_MAX + 5; i++) {
        char uuid[8];
        snprintf(uuid, sizeof(uuid), "r%d", i);
        Station s = named(uuid, "Recent");
        libraryPlayed(&lib, &s);
    }
    libraryPlayed(&lib, &a);
    libraryPlayed(&lib, &b);
    libraryPlayed(&lib, &a);  // Back to the top, not twice.
    CHECK(lib.recent.count == RECENT_MAX);
    CHECK(strcmp(lib.recent.items[0].uuid, "a") == 0 && strcmp(lib.recent.items[1].uuid, "b") == 0);
    libraryFree(&lib);

    // Saved on the card: loaded back the same.
    libraryLoad(&lib, folder);
    CHECK(lib.favorites.count == 1 && strcmp(lib.favorites.items[0].name, "Beta") == 0);
    CHECK(lib.recent.count == RECENT_MAX && strcmp(lib.recent.items[0].uuid, "a") == 0);
    libraryFree(&lib);

    // A damaged file gives an empty list.
    FILE *f = fopen(TMP_ROOT "/config/kairadio/favorites.json", "w");
    if (f) {
        fputs("[{\"name\":", f);
        fclose(f);
    }
    libraryLoad(&lib, folder);
    CHECK(lib.favorites.count == 0 && lib.recent.count == RECENT_MAX);
    libraryFree(&lib);
}
