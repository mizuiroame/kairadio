#include "podstore.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "cJSON.h"

#define SUBS_FILE "podcasts.json"
#define MARKS_FILE "podcast_marks.json"
#define FILE_MAX (8u << 20)
#define NUMBER_MAX 9007199254740991.0  // The largest a JSON number keeps exactly.

// --- Reading and writing JSON ---

static void copyString(const cJSON *item, const char *key, char *out, size_t size)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(item, key);
    out[0] = '\0';
    if (!cJSON_IsString(v) || !v->valuestring || strlen(v->valuestring) >= size)
        return;  // What doesn't fit wasn't written by KaiRadio: left out.
    strcpy(out, v->valuestring);
}

static int64_t number(const cJSON *item, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(item, key);
    if (!cJSON_IsNumber(v) || v->valuedouble < 0 || v->valuedouble > NUMBER_MAX)
        return 0;
    return (int64_t)v->valuedouble;
}

static bool isWebUrl(const char *url)
{
    return strncasecmp(url, "http://", 7) == 0 || strncasecmp(url, "https://", 8) == 0;
}

static bool readEpisode(const cJSON *item, Episode *e)
{
    memset(e, 0, sizeof(*e));
    copyString(item, "guid", e->guid, sizeof(e->guid));
    copyString(item, "title", e->title, sizeof(e->title));
    copyString(item, "url", e->url, sizeof(e->url));
    copyString(item, "type", e->type, sizeof(e->type));
    e->bytes = number(item, "bytes");
    e->published = number(item, "published");
    int64_t duration = number(item, "duration");
    e->duration = duration < 1000000 ? (int)duration : 0;
    return e->guid[0] && isWebUrl(e->url);
}

static bool readSub(const cJSON *item, Subscription *sub)
{
    memset(sub, 0, sizeof(*sub));
    copyString(item, "feed", sub->feed, sizeof(sub->feed));
    copyString(item, "title", sub->title, sizeof(sub->title));
    copyString(item, "author", sub->author, sizeof(sub->author));
    copyString(item, "image", sub->image, sizeof(sub->image));
    if (sub->image[0] && !isWebUrl(sub->image))
        sub->image[0] = '\0';
    sub->checked = number(item, "checked");
    sub->seen = number(item, "seen");
    if (!isWebUrl(sub->feed) || !sub->title[0])
        return false;
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(item, "episodes");
    int n = cJSON_IsArray(list) ? cJSON_GetArraySize(list) : 0;
    if (n > PODSTORE_SAVED_EPISODES)
        n = PODSTORE_SAVED_EPISODES;
    sub->episodes = n > 0 ? calloc((size_t)n, sizeof(Episode)) : NULL;
    if (n > 0 && !sub->episodes)
        return false;
    const cJSON *e;
    cJSON_ArrayForEach(e, list)
    {
        if ((int)sub->count >= n)
            break;
        if (cJSON_IsObject(e) && readEpisode(e, &sub->episodes[sub->count]))
            sub->count++;
    }
    return true;
}

static void freeSubs(PodStore *s)
{
    for (size_t i = 0; i < s->sub_count; i++)
        free(s->subs[i].episodes);
    free(s->subs);
    s->subs = NULL;
    s->sub_count = 0;
}

bool podStoreReadSubs(PodStore *s, const char *json)
{
    freeSubs(s);
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return false;
    }
    int n = cJSON_GetArraySize(root);
    if (n > PODSTORE_SUBS_MAX)
        n = PODSTORE_SUBS_MAX;
    s->subs = n > 0 ? calloc((size_t)n, sizeof(Subscription)) : NULL;
    bool ok = n == 0 || s->subs;
    const cJSON *item;
    cJSON_ArrayForEach(item, root)
    {
        if (!ok || (int)s->sub_count >= n)
            break;
        Subscription *sub = &s->subs[s->sub_count];
        if (cJSON_IsObject(item) && readSub(item, sub) && podStoreFind(s, sub->feed) < 0)
            s->sub_count++;
        else
            free(sub->episodes);
    }
    cJSON_Delete(root);
    return ok;
}

bool podStoreReadMarks(PodStore *s, const char *json)
{
    free(s->marks);
    s->marks = NULL;
    s->mark_count = 0;
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return false;
    }
    int n = cJSON_GetArraySize(root);
    if (n > PODSTORE_MARKS_MAX)
        n = PODSTORE_MARKS_MAX;
    s->marks = n > 0 ? calloc((size_t)n, sizeof(EpisodeMark)) : NULL;
    bool ok = n == 0 || s->marks;
    const cJSON *item;
    cJSON_ArrayForEach(item, root)
    {
        if (!ok || (int)s->mark_count >= n)
            break;
        char key[24];
        copyString(item, "key", key, sizeof(key));
        char *end;
        uint64_t k = strtoull(key, &end, 16);
        if (strlen(key) != 16 || *end)
            continue;
        EpisodeMark *m = &s->marks[s->mark_count++];
        m->key = k;
        int64_t position = number(item, "position");
        m->position = position < 1000000 ? (int)position : 0;
        m->played = number(item, "played") != 0;
        m->updated = number(item, "updated");
    }
    cJSON_Delete(root);
    return ok;
}

static bool addEpisode(cJSON *list, const Episode *e)
{
    cJSON *o = cJSON_CreateObject();
    if (!o || !cJSON_AddItemToArray(list, o))
        return false;
    return cJSON_AddStringToObject(o, "guid", e->guid) && cJSON_AddStringToObject(o, "title", e->title) &&
           cJSON_AddStringToObject(o, "url", e->url) && cJSON_AddStringToObject(o, "type", e->type) &&
           cJSON_AddNumberToObject(o, "bytes", (double)e->bytes) &&
           cJSON_AddNumberToObject(o, "published", (double)e->published) &&
           cJSON_AddNumberToObject(o, "duration", e->duration);
}

char *podStoreSubsJson(const PodStore *s)
{
    cJSON *root = cJSON_CreateArray();
    bool ok = root != NULL;
    for (size_t i = 0; ok && i < s->sub_count; i++) {
        const Subscription *sub = &s->subs[i];
        cJSON *o = cJSON_CreateObject(), *list = NULL;
        ok = o && cJSON_AddItemToArray(root, o) && cJSON_AddStringToObject(o, "feed", sub->feed) &&
             cJSON_AddStringToObject(o, "title", sub->title) && cJSON_AddStringToObject(o, "author", sub->author) &&
             cJSON_AddStringToObject(o, "image", sub->image) &&
             cJSON_AddNumberToObject(o, "checked", (double)sub->checked) &&
             cJSON_AddNumberToObject(o, "seen", (double)sub->seen) &&
             (list = cJSON_AddArrayToObject(o, "episodes")) != NULL;
        for (size_t e = 0; ok && e < sub->count && e < PODSTORE_SAVED_EPISODES; e++)
            ok = addEpisode(list, &sub->episodes[e]);
    }
    char *text = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    return text;
}

char *podStoreMarksJson(const PodStore *s)
{
    cJSON *root = cJSON_CreateArray();
    bool ok = root != NULL;
    for (size_t i = 0; ok && i < s->mark_count; i++) {
        const EpisodeMark *m = &s->marks[i];
        char key[24];
        snprintf(key, sizeof(key), "%016" PRIx64, m->key);
        cJSON *o = cJSON_CreateObject();
        ok = o && cJSON_AddItemToArray(root, o) && cJSON_AddStringToObject(o, "key", key) &&
             cJSON_AddNumberToObject(o, "position", m->position) && cJSON_AddNumberToObject(o, "played", m->played) &&
             cJSON_AddNumberToObject(o, "updated", (double)m->updated);
    }
    char *text = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    return text;
}

// --- Files ---

static char *readFile(const char *folder, const char *name)
{
    char path[320];
    snprintf(path, sizeof(path), "%s/%s", folder, name);
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
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
    return text;
}

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

// Through a temporary file, so a failed write keeps the old one.
static bool writeFile(const char *folder, const char *name, char *text)
{
    if (!text)
        return false;
    makeFolders(folder);
    char path[320], temp[328];
    snprintf(path, sizeof(path), "%s/%s", folder, name);
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    FILE *f = fopen(temp, "wb");
    bool ok = f && fputs(text, f) >= 0;
    if (f)
        ok = fclose(f) == 0 && ok;
    if (ok) {
        remove(path);
        ok = rename(temp, path) == 0;
    }
    if (!ok)
        remove(temp);
    free(text);
    return ok;
}

void podStoreLoad(PodStore *s, const char *folder)
{
    memset(s, 0, sizeof(*s));
    snprintf(s->folder, sizeof(s->folder), "%s", folder);
    char *subs = readFile(folder, SUBS_FILE), *marks = readFile(folder, MARKS_FILE);
    if (subs && !podStoreReadSubs(s, subs))
        freeSubs(s);
    if (marks && !podStoreReadMarks(s, marks)) {
        free(s->marks);
        s->marks = NULL;
        s->mark_count = 0;
    }
    free(subs);
    free(marks);
}

void podStoreFree(PodStore *s)
{
    freeSubs(s);
    free(s->marks);
    s->marks = NULL;
    s->mark_count = 0;
}

bool podStoreSaveSubs(const PodStore *s)
{
    return writeFile(s->folder, SUBS_FILE, podStoreSubsJson(s));
}

bool podStoreSaveMarks(const PodStore *s)
{
    return writeFile(s->folder, MARKS_FILE, podStoreMarksJson(s));
}

// --- Subscriptions ---

int podStoreFind(const PodStore *s, const char *feed)
{
    for (size_t i = 0; i < s->sub_count; i++)
        if (!strcmp(s->subs[i].feed, feed))
            return (int)i;
    return -1;
}

int podStoreSubscribe(PodStore *s, const PodcastResult *r, int64_t now)
{
    int there = podStoreFind(s, r->feed);
    if (there >= 0)
        return there;
    if (!isWebUrl(r->feed) || s->sub_count >= PODSTORE_SUBS_MAX)
        return -1;
    Subscription *grown = realloc(s->subs, (s->sub_count + 1) * sizeof(Subscription));
    if (!grown)
        return -1;
    s->subs = grown;
    memmove(&s->subs[1], &s->subs[0], s->sub_count * sizeof(Subscription));
    Subscription *sub = &s->subs[0];
    memset(sub, 0, sizeof(*sub));
    snprintf(sub->feed, sizeof(sub->feed), "%s", r->feed);
    snprintf(sub->title, sizeof(sub->title), "%s", r->title);
    snprintf(sub->author, sizeof(sub->author), "%s", r->author);
    snprintf(sub->image, sizeof(sub->image), "%s", r->image);
    sub->seen = now;
    s->sub_count++;
    podStoreSaveSubs(s);
    return 0;
}

bool podStoreUnsubscribe(PodStore *s, int index)
{
    if (index < 0 || (size_t)index >= s->sub_count)
        return false;
    free(s->subs[index].episodes);
    memmove(&s->subs[index], &s->subs[index + 1], (s->sub_count - (size_t)index - 1) * sizeof(Subscription));
    s->sub_count--;
    return podStoreSaveSubs(s);
}

int podStoreTakeFeed(PodStore *s, int index, const Feed *feed, int64_t now)
{
    if (index < 0 || (size_t)index >= s->sub_count)
        return -1;
    Subscription *sub = &s->subs[index];
    Episode *copy = feed->count ? malloc(feed->count * sizeof(Episode)) : NULL;
    if (feed->count && !copy)
        return -1;
    if (feed->count)
        memcpy(copy, feed->episodes, feed->count * sizeof(Episode));
    int64_t newest = sub->count ? sub->episodes[0].published : 0;
    int fresh = 0;
    for (size_t i = 0; i < feed->count; i++)
        if (sub->count && feed->episodes[i].published > newest)
            fresh++;
    free(sub->episodes);
    sub->episodes = copy;
    sub->count = feed->count;
    if (feed->title[0])
        snprintf(sub->title, sizeof(sub->title), "%s", feed->title);
    if (feed->author[0])
        snprintf(sub->author, sizeof(sub->author), "%s", feed->author);
    if (feed->image[0] && !sub->image[0])  // The search's picture is kept: it's small; a feed's may be huge.
        snprintf(sub->image, sizeof(sub->image), "%s", feed->image);
    sub->checked = now;
    return fresh;
}

int podStoreNewCount(const Subscription *sub)
{
    int n = 0;
    for (size_t i = 0; i < sub->count; i++)
        if (sub->episodes[i].published > sub->seen)
            n++;
    return n;
}

void podStoreSeen(PodStore *s, int index, int64_t now)
{
    if (index >= 0 && (size_t)index < s->sub_count)
        s->subs[index].seen = now;
}

// --- Where each episode was left ---

uint64_t podStoreKey(const char *feed, const char *guid)
{
    uint64_t h = 0xcbf29ce484222325ull;  // FNV-1a.
    for (const unsigned char *p = (const unsigned char *)feed; *p; p++)
        h = (h ^ *p) * 0x100000001b3ull;
    h = (h ^ '\n') * 0x100000001b3ull;
    for (const unsigned char *p = (const unsigned char *)guid; *p; p++)
        h = (h ^ *p) * 0x100000001b3ull;
    return h;
}

static EpisodeMark *findMark(const PodStore *s, uint64_t key)
{
    for (size_t i = 0; i < s->mark_count; i++)
        if (s->marks[i].key == key)
            return &s->marks[i];
    return NULL;
}

bool podStoreMark(const PodStore *s, const char *feed, const char *guid, int *position, bool *played)
{
    const EpisodeMark *m = findMark(s, podStoreKey(feed, guid));
    *position = m ? m->position : 0;
    *played = m && m->played;
    return m != NULL;
}

// The mark for this episode, made (in place of the oldest, at the limit) if
// it has none. NULL only if memory runs out.
static EpisodeMark *markFor(PodStore *s, const char *feed, const char *guid)
{
    uint64_t key = podStoreKey(feed, guid);
    EpisodeMark *m = findMark(s, key);
    if (m)
        return m;
    if (s->mark_count >= PODSTORE_MARKS_MAX) {
        size_t oldest = 0;
        for (size_t i = 1; i < s->mark_count; i++)
            if (s->marks[i].updated < s->marks[oldest].updated)
                oldest = i;
        m = &s->marks[oldest];
    } else {
        EpisodeMark *grown = realloc(s->marks, (s->mark_count + 1) * sizeof(EpisodeMark));
        if (!grown)
            return NULL;
        s->marks = grown;
        m = &s->marks[s->mark_count++];
    }
    memset(m, 0, sizeof(*m));
    m->key = key;
    return m;
}

void podStoreSetPosition(PodStore *s, const char *feed, const char *guid, int position, int duration, int64_t now)
{
    EpisodeMark *m = markFor(s, feed, guid);
    if (!m)
        return;
    if (position < 0)
        position = 0;
    bool done = duration > 0 && position >= duration - PODSTORE_PLAYED_LEFT;
    m->played = done || m->played;
    m->position = done ? 0 : position;
    m->updated = now;
}

void podStoreSetPlayed(PodStore *s, const char *feed, const char *guid, bool played, int64_t now)
{
    EpisodeMark *m = markFor(s, feed, guid);
    if (!m)
        return;
    m->played = played;
    m->position = 0;
    m->updated = now;
}
