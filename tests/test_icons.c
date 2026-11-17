// Genre icons and language letters: the rules, and the masks in data/icons.bin.
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "categoryicons.h"
#include "check.h"
#include "icons.h"

#define ICONS_BIN "../data/icons.bin"  // From tests/, where the tests run.

static bool iconIs(const char *tag, const char *icon)
{
    const char *got = genreIcon(tag);
    return got && strcmp(got, icon) == 0;
}

void testIcons(void)
{
    CHECK(iconIs("rock", "guitar-electric") && iconIs("Classic Rock", "guitar-electric"));
    CHECK(iconIs("heavy metal", "skull"));           // Metal before rock-ish words.
    CHECK(iconIs("k-pop", "sparkles") && iconIs("pop", "star"));  // The narrower rule first.
    CHECK(iconIs("dancehall", "sun") && iconIs("dance", "audio-waveform"));
    CHECK(iconIs("smooth jazz", "disc-3") && iconIs("lounge", "wine"));
    CHECK(iconIs("classical", "piano") && iconIs("hip hop", "mic-vocal") && iconIs("80s", "cassette-tape"));
    CHECK(iconIs("news", "newspaper") && iconIs("talk", "messages-square") && iconIs("salsa", "tree-palm"));
    CHECK(genreIcon("xyzzy") == NULL && genreIcon("") == NULL && genreIcon(NULL) == NULL);

    // Every icon the rules name is in the index, which is sorted.
    size_t rules = 0;
    for (const char *icon; (icon = genreIconAt(rules)) != NULL; rules++)
        CHECK(iconFind(icon) != NULL);
    CHECK(rules > 20 && iconFind("nope") == NULL && iconFind(NULL) == NULL);
    for (size_t i = 1; i < ICON_COUNT; i++)
        CHECK(strcmp(ICON_INDEX[i - 1].name, ICON_INDEX[i].name) < 0);

    // The masks: a small and a large one per icon, one after another, none blank.
    const size_t per_icon = ICON_SMALL * ICON_SMALL + ICON_LARGE * ICON_LARGE;
    FILE *f = fopen(ICONS_BIN, "rb");
    CHECK(f != NULL);
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        CHECK(size == (long)(ICON_COUNT * per_icon));
        unsigned char *blob = malloc((size_t)size);
        if (blob && fread(blob, 1, (size_t)size, f) == (size_t)size) {
            for (size_t i = 0; i < ICON_COUNT; i++) {
                CHECK(ICON_INDEX[i].offset == i * per_icon);
                long ink = 0;
                for (size_t b = 0; b < per_icon; b++)
                    ink += blob[ICON_INDEX[i].offset + b];
                CHECK(ink > 0);
            }
        }
        free(blob);
        fclose(f);
    }

    CHECK(strcmp(languageSample("es", "spanish"), "\xC3\x91") == 0);
    CHECK(strcmp(languageSample("", "english"), "Aa") == 0);     // No code: by name.
    CHECK(strcmp(languageSample("JA", ""), "\xE3\x81\x82") == 0); // Any case.
    CHECK(languageSample("xx", "klingon") == NULL && languageSample(NULL, NULL) == NULL);
}
