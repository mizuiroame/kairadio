// Country flags: the index, the lookup, and every PNG in data/flags.bin.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "flags.h"
#include "image_config.h"
#include "stb_image.h"

#define FLAGS_BIN "../data/flags.bin"  // From tests/, where the tests run.

static unsigned char *readFile(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = malloc((size_t)*size);
    if (data && fread(data, 1, (size_t)*size, f) != (size_t)*size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    return data;
}

void testFlags(void)
{
    CHECK(flagFind("MX") && strcmp(flagFind("MX")->code, "MX") == 0);
    CHECK(flagFind("jp") == flagFind("JP") && flagFind("JP") != NULL);
    CHECK(flagFind("ZZ") == NULL && flagFind("") == NULL && flagFind(NULL) == NULL && flagFind("MEX") == NULL);
    CHECK(FLAG_COUNT > 240);

    // Sorted, one after another in the file, each a PNG of the right size.
    long size = 0;
    unsigned char *blob = readFile(FLAGS_BIN, &size);
    CHECK(blob != NULL);
    uint32_t next = 0;
    int bad = 0;
    for (size_t i = 0; i < FLAG_COUNT; i++) {
        const FlagEntry *e = &FLAG_INDEX[i];
        CHECK(i == 0 || strcmp(FLAG_INDEX[i - 1].code, e->code) < 0);
        CHECK(e->offset == next);
        next = e->offset + e->size;
        if (!blob || next > (uint32_t)size)
            continue;
        int w = 0, h = 0, comp = 0;
        unsigned char *pixels = stbi_load_from_memory(blob + e->offset, (int)e->size, &w, &h, &comp, 4);
        if (!pixels || w != FLAG_IMAGE_W || h != FLAG_IMAGE_H)
            bad++;
        stbi_image_free(pixels);
    }
    CHECK(bad == 0);
    CHECK(next == (uint32_t)size);
    free(blob);
}
