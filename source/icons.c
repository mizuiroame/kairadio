#include "icons.h"

#include <string.h>

const IconEntry *iconFind(const char *name)
{
    if (!name)
        return NULL;
    size_t lo = 0, hi = ICON_COUNT;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(ICON_INDEX[mid].name, name);
        if (c == 0)
            return &ICON_INDEX[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}
