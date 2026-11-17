// Host tests for the modules that do not touch Switch services.
// Build and run with: make -C tests
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "directory.h"
#include "fake_net.h"
#include "stations.h"
#include "util.h"

int g_failures;
int g_checks;

static void testUtil(void)
{
    char out[16];
    utilFormatClock(out, sizeof(out), 21, 7, true);
    CHECK(strcmp(out, "21:07") == 0);
    utilFormatClock(out, sizeof(out), 0, 5, false);
    CHECK(strcmp(out, "12:05 AM") == 0);
    utilFormatTime(out, sizeof(out), 3725);
    CHECK(strcmp(out, "1:02:05") == 0);
    CHECK(utilCompareNames("abc", "ABD") < 0);
    const char *s = "\xC3\xA9x";
    CHECK(utilNextCodePoint(&s) == 0xE9 && *s == 'x');
}

static bool sameStation(const Station *a, const Station *b)
{
    return strcmp(a->uuid, b->uuid) == 0 && strcmp(a->name, b->name) == 0 && strcmp(a->url, b->url) == 0 &&
           strcmp(a->favicon, b->favicon) == 0 && strcmp(a->country, b->country) == 0 &&
           strcmp(a->countrycode, b->countrycode) == 0 && strcmp(a->language, b->language) == 0 &&
           strcmp(a->tags, b->tags) == 0 && strcmp(a->codec, b->codec) == 0 && a->bitrate == b->bitrate &&
           a->hls == b->hls;
}

static const char DIRECTORY_JSON[] =
    "[{\"stationuuid\":\"1c33\",\"name\":\"  FIP \",\"url\":\"http://old\",\"url_resolved\":\"https://icecast/fip.aac\","
    "\"favicon\":\"https://fip/logo.png\",\"country\":\"France\",\"countrycode\":\"FR\",\"language\":\"french\","
    "\"tags\":\"jazz,  eclectic ,world,pop\",\"codec\":\"AAC\",\"bitrate\":192,\"hls\":0},"
    "{\"stationuuid\":\"2\",\"name\":\"No address\",\"url\":\"\",\"url_resolved\":\"\"},"
    "{\"stationuuid\":\"3\",\"name\":\"Plain\",\"url\":\"http://plain/stream\",\"url_resolved\":\"rtsp://x\","
    "\"favicon\":\"data:image/png;base64,AAAA\",\"codec\":\"UNKNOWN\",\"bitrate\":\"64\",\"hls\":1},"
    "{\"name\":\"\",\"url\":\"http://nameless\"}, 5, \"text\"]";

void testStations(void)
{
    StationList list;
    CHECK(stationsParse(DIRECTORY_JSON, &list));
    CHECK(list.count == 2);
    if (list.count == 2) {
        const Station *a = &list.items[0], *b = &list.items[1];
        CHECK(strcmp(a->name, "FIP") == 0);
        CHECK(strcmp(a->url, "https://icecast/fip.aac") == 0);
        CHECK(strcmp(a->favicon, "https://fip/logo.png") == 0);
        CHECK(a->bitrate == 192 && !a->hls);
        CHECK(strcmp(b->url, "http://plain/stream") == 0);  // url_resolved isn't http(s).
        CHECK(b->favicon[0] == '\0');                         // Not an http(s) logo.
        CHECK(b->codec[0] == '\0' && b->bitrate == 64 && b->hls);

        char details[128];
        stationDetails(a, details, sizeof(details));
        CHECK(strcmp(details, "France \xC2\xB7 AAC 192 kbps \xC2\xB7 jazz, eclectic, world") == 0);
        stationDetails(b, details, sizeof(details));
        CHECK(strcmp(details, "HLS 64 kbps") == 0);

        // Favorites round-trip through the same JSON.
        char *json = stationsToJson(&list);
        CHECK(json != NULL);
        StationList again;
        CHECK(json && stationsParse(json, &again));
        CHECK(again.count == 2 && sameStation(&again.items[0], &list.items[0]));
        CHECK(again.count == 2 && sameStation(&again.items[1], &list.items[1]));
        free(json);

        CHECK(stationListFind(&again, &list.items[1]) == 1);
        stationListRemove(&again, 0);
        CHECK(again.count == 1 && stationListFind(&again, &list.items[0]) == -1);
        CHECK(stationListAppend(&again, &list.items[0]) && again.count == 2);
        CHECK(stationListFind(&again, &list.items[0]) == 1);
        stationListFree(&again);
    }
    stationListFree(&list);

    CHECK(!stationsParse("{\"not\":\"an array\"}", &list) && list.count == 0);
    CHECK(!stationsParse("[{", &list) && list.count == 0);
    CHECK(stationsParse("[]", &list) && list.count == 0);
    stationListFree(&list);

    // A name longer than the field is cut on a character boundary.
    char long_json[600];
    char name[300];
    for (int i = 0; i < 149; i++)
        memcpy(name + i * 2, "\xC3\xA9", 2);
    name[298] = '\0';
    snprintf(long_json, sizeof(long_json), "[{\"name\":\"%s\",\"url\":\"http://x\"}]", name);
    CHECK(stationsParse(long_json, &list) && list.count == 1);
    if (list.count == 1)
        CHECK(strlen(list.items[0].name) == STATION_NAME_MAX - 1 - 1);
    stationListFree(&list);
}

void testDirectory(void)
{
    char path[512];
    CHECK(directoryPath(DIR_POPULAR, NULL, path, sizeof(path)) &&
          strcmp(path, "/json/stations/topclick/150?hidebroken=true") == 0);
    CHECK(directoryPath(DIR_SEARCH, "jazz & blues", path, sizeof(path)) &&
          strcmp(path, "/json/stations/search?name=jazz%20%26%20blues&limit=150&hidebroken=true&order=clickcount&reverse=true") == 0);
    CHECK(directoryPath(DIR_COUNTRY, "MX", path, sizeof(path)) && strstr(path, "countrycode=MX&") != NULL);
    CHECK(directoryPath(DIR_TAG, "caf\xC3\xA9", path, sizeof(path)) && strstr(path, "tag=caf%C3%A9&tagExact=true") != NULL);
    CHECK(directoryPath(DIR_LANGUAGE, "spanish", path, sizeof(path)) &&
          strstr(path, "language=spanish&languageExact=true") != NULL);
    CHECK(directoryPath(DIR_GENRES, NULL, path, sizeof(path)) &&
          strcmp(path, "/json/tags?order=stationcount&reverse=true&hidebroken=true&limit=300") == 0);
    CHECK(directoryPath(DIR_COUNTRIES, NULL, path, sizeof(path)) && strncmp(path, "/json/countries?", 16) == 0);
    CHECK(directoryPath(DIR_LANGUAGES, NULL, path, sizeof(path)) && strncmp(path, "/json/languages?", 16) == 0);
    CHECK(directoryListsCategories(DIR_GENRES) && !directoryListsCategories(DIR_TAG));
    CHECK(!directoryPath(DIR_SEARCH, "", path, sizeof(path)));
    CHECK(!directoryPath(DIR_SEARCH, "x", path, 10));

    // The worker: the server list, then the query; then a failing one.
    fakeNetReset();
    fakeNetAnswer("/json/servers", "[{\"name\":\"de1.api.radio-browser.info\"}]", 200);
    fakeNetAnswer("/topclick/", DIRECTORY_JSON, 200);
    fakeNetAnswer("name=down", "", 503);
    CHECK(directoryInit());
    DirectoryStatus status;
    directoryRequest(DIR_POPULAR, NULL);
    directoryGetStatus(&status);
    unsigned generation = status.generation;
    CHECK(status.state == DIR_LOADING);
    for (int i = 0; i < 500 && status.state == DIR_LOADING; i++) {
        usleep(2000);
        directoryGetStatus(&status);
    }
    CHECK(status.state == DIR_READY);
    StationList list;
    CHECK(!directoryTake(generation + 1, &list));
    CHECK(directoryTake(generation, &list) && list.count == 2);
    stationListFree(&list);

    directoryRequest(DIR_SEARCH, "down");
    do {
        usleep(2000);
        directoryGetStatus(&status);
    } while (status.state == DIR_LOADING);
    CHECK(status.state == DIR_FAILED && strstr(status.error, "503") != NULL);
    CHECK(!directoryTake(status.generation, &list));

    // Categories come back as categories.
    fakeNetAnswer("/json/countries", "[{\"name\":\"Mexico\",\"iso_3166_1\":\"MX\",\"stationcount\":812}]", 200);
    directoryRequest(DIR_COUNTRIES, NULL);
    do {
        usleep(2000);
        directoryGetStatus(&status);
    } while (status.state == DIR_LOADING);
    CategoryList countries;
    CHECK(status.state == DIR_READY);
    CHECK(directoryTakeCategories(status.generation, &countries) && countries.count == 1);
    if (countries.count == 1)
        CHECK(strcmp(countries.items[0].code, "MX") == 0 && countries.items[0].count == 812);
    categoryListFree(&countries);
    directoryCountClick("1c33");
    directoryExit();
    fakeNetReset();
}

void testCategories(void)
{
    CategoryList list;
    CHECK(categoriesParse("[{\"name\":\"hip hop\",\"stationcount\":120},{\"name\":\"\",\"stationcount\":5},"
                          "{\"name\":\"empty\",\"stationcount\":0},{\"name\":\"spanish\",\"iso_639\":\"es\","
                          "\"stationcount\":\"7\"},{\"name\":\"Mexico\",\"iso_3166_1\":\"MX\",\"stationcount\":3}]",
                          &list));
    CHECK(list.count == 2);
    char label[CATEGORY_NAME_MAX];
    if (list.count == 2) {
        categoryLabel(&list.items[0], label, sizeof(label));
        CHECK(strcmp(label, "Hip Hop") == 0 && list.items[0].code[0] == '\0');
        CHECK(strcmp(list.items[1].name, "Mexico") == 0 && strcmp(list.items[1].code, "MX") == 0);
    }
    categoryListFree(&list);
    CHECK(!categoriesParse("{}", &list) && list.count == 0);

    // Sorting: by stations either way (ties by name), or by name without a leading "The".
    Category items[] = {{"Mexico", "MX", 800}, {"The Netherlands", "NL", 300}, {"brazil", "BR", 800},
                        {"Japan", "JP", 700}};
    CategoryList sorted = {items, 4};
    categorySort(&sorted, SORT_MOST);
    CHECK(strcmp(items[0].code, "BR") == 0 && strcmp(items[1].code, "MX") == 0 && strcmp(items[3].code, "NL") == 0);
    categorySort(&sorted, SORT_FEWEST);
    CHECK(strcmp(items[0].code, "NL") == 0 && strcmp(items[1].code, "JP") == 0 && strcmp(items[2].code, "BR") == 0);
    categorySort(&sorted, SORT_AZ);
    CHECK(strcmp(items[0].code, "BR") == 0 && strcmp(items[1].code, "JP") == 0 && strcmp(items[2].code, "MX") == 0 &&
          strcmp(items[3].code, "NL") == 0);
    categorySort(&sorted, SORT_ZA);
    CHECK(strcmp(items[0].code, "NL") == 0 && strcmp(items[3].code, "BR") == 0);
    CHECK(strcmp(categorySortLabel(SORT_AZ), "A to Z") == 0 && strcmp(categorySortLabel(SORT_COUNT), "") == 0);
    CategoryList empty = {NULL, 0};
    categorySort(&empty, SORT_AZ);  // Nothing to do, and no crash.
}

int main(void)
{
    testUtil();
    testIcy();
    testLinks();
    testStations();
    testRing();
    testHls();
    testDirectory();
    testLogos();
    testCategories();
    testLibrary();
    testWorldMap();
    testFlags();
    testIcons();
    testSettings();
    testAppRoot();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
