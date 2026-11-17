#include "library.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define FILE_MAX (2u << 20)

static void pathOf(const Library *lib, const char *name, char *out, size_t size)
{
    snprintf(out, size, "%s/%s", lib->folder, name);
}

static void loadList(const Library *lib, const char *name, StationList *out)
{
    char path[320];
    pathOf(lib, name, path, sizeof(path));
    out->items = NULL;
    out->count = 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    char *text = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long size = ftell(f);
        if (size > 0 && (unsigned long)size <= FILE_MAX && fseek(f, 0, SEEK_SET) == 0 &&
            (text = malloc((size_t)size + 1)) != NULL) {
            size_t got = fread(text, 1, (size_t)size, f);
            text[got] = '\0';
        }
    }
    fclose(f);
    if (text && !stationsParse(text, out))
        stationListFree(out);
    free(text);
}

// Creates each missing folder of path ("sdmc:/switch/kairadio/config").
static void makeFolders(const char *path)
{
    char partial[256];
    snprintf(partial, sizeof(partial), "%s", path);
    for (char *p = strchr(partial, '/'); p; p = strchr(p + 1, '/')) {
        if (p == partial || p[-1] == ':')
            continue;
        *p = '\0';
        mkdir(partial, 0777);
        *p = '/';
    }
    mkdir(partial, 0777);
}

// Writes to a temporary file first, so a failed write keeps the old list.
static void saveList(const Library *lib, const char *name, const StationList *list)
{
    char *json = stationsToJson(list);
    if (!json)
        return;
    makeFolders(lib->folder);
    char path[320], temp[328];
    pathOf(lib, name, path, sizeof(path));
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    FILE *f = fopen(temp, "wb");
    bool ok = f && fputs(json, f) >= 0;
    if (f)
        ok = fclose(f) == 0 && ok;
    if (ok) {
        remove(path);
        ok = rename(temp, path) == 0;
    }
    if (!ok)
        remove(temp);
    free(json);
}

void libraryLoad(Library *lib, const char *folder)
{
    snprintf(lib->folder, sizeof(lib->folder), "%s", folder);
    loadList(lib, "favorites.json", &lib->favorites);
    loadList(lib, "recent.json", &lib->recent);
    while (lib->recent.count > RECENT_MAX)
        stationListRemove(&lib->recent, lib->recent.count - 1);
}

void libraryFree(Library *lib)
{
    stationListFree(&lib->favorites);
    stationListFree(&lib->recent);
}

bool libraryIsFavorite(const Library *lib, const Station *station)
{
    return stationListFind(&lib->favorites, station) >= 0;
}

bool libraryToggleFavorite(Library *lib, const Station *station)
{
    long i = stationListFind(&lib->favorites, station);
    bool now;
    if (i >= 0) {
        stationListRemove(&lib->favorites, (size_t)i);
        now = false;
    } else {
        now = stationListAppend(&lib->favorites, station);
    }
    saveList(lib, "favorites.json", &lib->favorites);
    return now;
}

void libraryPlayed(Library *lib, const Station *station)
{
    long i = stationListFind(&lib->recent, station);
    if (i == 0)
        return;
    if (i > 0)
        stationListRemove(&lib->recent, (size_t)i);
    if (!stationListAppend(&lib->recent, station))
        return;
    // Appended at the end: move it to the top.
    Station top = lib->recent.items[lib->recent.count - 1];
    memmove(&lib->recent.items[1], &lib->recent.items[0], (lib->recent.count - 1) * sizeof(Station));
    lib->recent.items[0] = top;
    while (lib->recent.count > RECENT_MAX)
        stationListRemove(&lib->recent, lib->recent.count - 1);
    saveList(lib, "recent.json", &lib->recent);
}
