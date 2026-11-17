#include "stations.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"

static void copyField(const cJSON *item, const char *key, char *out, size_t size)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(item, key);
    out[0] = '\0';
    if (!cJSON_IsString(v) || !v->valuestring)
        return;
    // Drop what doesn't fit rather than cut a URL in two; text is cut at a
    // character boundary.
    size_t len = strlen(v->valuestring);
    if (len >= size) {
        if (strcmp(key, "url") == 0 || strcmp(key, "url_resolved") == 0 || strcmp(key, "favicon") == 0)
            return;
        len = size - 1;
        while (len > 0 && ((unsigned char)v->valuestring[len] & 0xC0) == 0x80)
            len--;
    }
    memcpy(out, v->valuestring, len);
    out[len] = '\0';
    // Names and tags come with stray spaces and line breaks.
    for (char *c = out; *c; c++)
        if (*c == '\n' || *c == '\r' || *c == '\t')
            *c = ' ';
}

static int intField(const cJSON *item, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(item, key);
    if (cJSON_IsNumber(v))
        return v->valueint;
    if (cJSON_IsString(v) && v->valuestring)
        return atoi(v->valuestring);
    return 0;
}

static bool isWebUrl(const char *url)
{
    return strncasecmp(url, "http://", 7) == 0 || strncasecmp(url, "https://", 8) == 0;
}

static void trim(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && s[len - 1] == ' ')
        s[--len] = '\0';
    size_t lead = strspn(s, " ");
    memmove(s, s + lead, len - lead + 1);
}

static bool readStation(const cJSON *item, Station *s)
{
    memset(s, 0, sizeof(*s));
    copyField(item, "stationuuid", s->uuid, sizeof(s->uuid));
    copyField(item, "name", s->name, sizeof(s->name));
    copyField(item, "url_resolved", s->url, sizeof(s->url));
    if (!isWebUrl(s->url))
        copyField(item, "url", s->url, sizeof(s->url));
    copyField(item, "favicon", s->favicon, sizeof(s->favicon));
    copyField(item, "country", s->country, sizeof(s->country));
    copyField(item, "countrycode", s->countrycode, sizeof(s->countrycode));
    copyField(item, "language", s->language, sizeof(s->language));
    copyField(item, "tags", s->tags, sizeof(s->tags));
    copyField(item, "codec", s->codec, sizeof(s->codec));
    s->bitrate = intField(item, "bitrate");
    if (s->bitrate < 0 || s->bitrate > 100000)
        s->bitrate = 0;
    s->hls = intField(item, "hls") != 0;
    trim(s->name);
    if (!isWebUrl(s->favicon))
        s->favicon[0] = '\0';
    if (strcasecmp(s->codec, "UNKNOWN") == 0)
        s->codec[0] = '\0';
    return s->name[0] && isWebUrl(s->url);
}

bool stationsParse(const char *json, StationList *out)
{
    out->items = NULL;
    out->count = 0;
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return false;
    }
    int n = cJSON_GetArraySize(root);
    if (n > 0) {
        out->items = malloc((size_t)n * sizeof(Station));
        if (!out->items) {
            cJSON_Delete(root);
            return false;
        }
    }
    const cJSON *item;
    cJSON_ArrayForEach(item, root)
    {
        if (cJSON_IsObject(item) && readStation(item, &out->items[out->count]))
            out->count++;
    }
    cJSON_Delete(root);
    return true;
}

static bool addString(cJSON *obj, const char *key, const char *value)
{
    return cJSON_AddStringToObject(obj, key, value) != NULL;
}

char *stationsToJson(const StationList *list)
{
    cJSON *root = cJSON_CreateArray();
    if (!root)
        return NULL;
    for (size_t i = 0; i < list->count; i++) {
        const Station *s = &list->items[i];
        cJSON *obj = cJSON_CreateObject();
        if (!obj || !cJSON_AddItemToArray(root, obj) || !addString(obj, "stationuuid", s->uuid) ||
            !addString(obj, "name", s->name) || !addString(obj, "url_resolved", s->url) ||
            !addString(obj, "favicon", s->favicon) || !addString(obj, "country", s->country) ||
            !addString(obj, "countrycode", s->countrycode) || !addString(obj, "language", s->language) ||
            !addString(obj, "tags", s->tags) || !addString(obj, "codec", s->codec) ||
            !cJSON_AddNumberToObject(obj, "bitrate", s->bitrate) || !cJSON_AddNumberToObject(obj, "hls", s->hls)) {
            cJSON_Delete(root);
            return NULL;
        }
    }
    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    return text;
}

void stationListFree(StationList *list)
{
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

bool stationListAppend(StationList *list, const Station *station)
{
    Station *items = realloc(list->items, (list->count + 1) * sizeof(Station));
    if (!items)
        return false;
    items[list->count++] = *station;
    list->items = items;
    return true;
}

void stationListRemove(StationList *list, size_t index)
{
    if (index >= list->count)
        return;
    memmove(&list->items[index], &list->items[index + 1], (list->count - index - 1) * sizeof(Station));
    list->count--;
}

long stationListFind(const StationList *list, const Station *station)
{
    for (size_t i = 0; i < list->count; i++) {
        const Station *s = &list->items[i];
        if (station->uuid[0] ? strcmp(s->uuid, station->uuid) == 0 : strcmp(s->url, station->url) == 0)
            return (long)i;
    }
    return -1;
}

void stationDetails(const Station *station, char *out, size_t size)
{
    char format[48] = "";
    if (station->hls)
        snprintf(format, sizeof(format), "HLS");
    if (station->codec[0] && station->bitrate > 0)
        snprintf(format + strlen(format), sizeof(format) - strlen(format), "%s%s %d kbps", format[0] ? " " : "",
                 station->codec, station->bitrate);
    else if (station->codec[0])
        snprintf(format + strlen(format), sizeof(format) - strlen(format), "%s%s", format[0] ? " " : "",
                 station->codec);
    else if (station->bitrate > 0)
        snprintf(format + strlen(format), sizeof(format) - strlen(format), "%s%d kbps", format[0] ? " " : "",
                 station->bitrate);

    char tags[STATION_TEXT_MAX];
    snprintf(tags, sizeof(tags), "%s", station->tags);
    // At most three tags, with spaces after the commas.
    char nice[STATION_TEXT_MAX] = "";
    int count = 0;
    for (char *tok = strtok(tags, ","); tok && count < 3; tok = strtok(NULL, ",")) {
        trim(tok);
        if (!*tok)
            continue;
        size_t used = strlen(nice);
        snprintf(nice + used, sizeof(nice) - used, "%s%s", count ? ", " : "", tok);
        count++;
    }

    const char *parts[3] = {station->country, format, nice};
    size_t o = 0;
    out[0] = '\0';
    for (int i = 0; i < 3; i++) {
        if (!parts[i][0])
            continue;
        int n = snprintf(out + o, size - o, "%s%s", o ? " \xC2\xB7 " : "", parts[i]);
        if (n < 0 || (size_t)n >= size - o)
            break;
        o += (size_t)n;
    }
}
