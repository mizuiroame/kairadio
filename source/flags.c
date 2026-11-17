#include "flags.h"

#include <string.h>

const FlagEntry *flagFind(const char *code)
{
    if (!code || !code[0] || !code[1] || code[2])
        return NULL;
    char key[3] = {code[0], code[1], 0};
    for (int i = 0; i < 2; i++)
        if (key[i] >= 'a' && key[i] <= 'z')
            key[i] = (char)(key[i] - 'a' + 'A');
    size_t lo = 0, hi = FLAG_COUNT;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(FLAG_INDEX[mid].code, key);
        if (c == 0)
            return &FLAG_INDEX[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}
