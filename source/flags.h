// Country flags (from flag-icons, made by tools/make_flags.py): small PNGs
// in data/flags.bin, found by country code. Pure lookup; ui_flag.c draws them.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Each flag's size, 4:3: twice the framebuffer size of a 40 x 30 layout box.
#define FLAG_IMAGE_W 120
#define FLAG_IMAGE_H 90

typedef struct {
    char code[3];     // ISO 3166-1 alpha-2, capitals.
    uint32_t offset;  // Its PNG in data/flags.bin.
    uint32_t size;
} FlagEntry;

// flags_index.c (generated), sorted by code.
extern const FlagEntry FLAG_INDEX[];
extern const size_t FLAG_COUNT;

// The flag of a country code (any case), or NULL if there is none.
const FlagEntry *flagFind(const char *code);
