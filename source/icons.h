// Genre icons (from Lucide, made by tools/make_icons.py): 8-bit masks in
// data/icons.bin, found by name. Pure lookup; ui.c draws them.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Each icon is ICON_SMALL x ICON_SMALL bytes (for 1080p), then
// ICON_LARGE x ICON_LARGE (for bigger screens), both for a 28-pixel layout box.
#define ICON_SMALL 42
#define ICON_LARGE 84
#define ICON_LAYOUT 28

typedef struct {
    const char *name;  // Lucide's name ("guitar-electric").
    uint32_t offset;   // Its small mask in data/icons.bin; the large one follows.
} IconEntry;

// icons_index.c (generated), sorted by name.
extern const IconEntry ICON_INDEX[];
extern const size_t ICON_COUNT;

const IconEntry *iconFind(const char *name);
