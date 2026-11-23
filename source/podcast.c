#include "podcast.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "xmlread.h"

#define SEARCH_BASE "https://itunes.apple.com/search?media=podcast&entity=podcast&limit=50&term="
#define FEED_COLLECT_MAX 1000  // Episodes read from a feed before the newest are kept.
#define FIELD_MAX LINK_URL_MAX

static bool isWebUrl(const char *url)
{
    return strncasecmp(url, "http://", 7) == 0 || strncasecmp(url, "https://", 8) == 0;
}

// Copies text into out: runs of spaces and line breaks become one space, the
// ends are trimmed, and it's cut between characters, never inside one.
static void copyText(const char *text, char *out, size_t size)
{
    size_t n = 0;
    bool space = false;
    for (const unsigned char *p = (const unsigned char *)text; *p;) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            space = n > 0;
            p++;
            continue;
        }
        size_t len = *p >= 0xF0 ? 4 : *p >= 0xE0 ? 3 : *p >= 0xC0 ? 2 : 1;
        for (size_t i = 1; i < len; i++)
            if (!p[i])
                len = i;  // A sequence cut short by the text's end.
        if (n + (space ? 1 : 0) + len >= size)
            break;
        if (space)
            out[n++] = ' ';
        memcpy(out + n, p, len);
        n += len;
        p += len;
        space = false;
    }
    out[n] = '\0';
}

// An address: kept whole (trimmed), or not at all.
static void copyUrl(const char *text, char *out, size_t size)
{
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
        text++;
    size_t length = strlen(text);
    while (length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\t' || text[length - 1] == '\r' ||
                          text[length - 1] == '\n'))
        length--;
    if (length >= size || !isWebUrl(text) || memchr(text, ' ', length)) {
        out[0] = '\0';
        return;
    }
    memcpy(out, text, length);
    out[length] = '\0';
}

bool podcastSearchUrl(const char *words, char *out, size_t size)
{
    size_t n = strlen(SEARCH_BASE);
    if (!words || n >= size)
        return false;
    memcpy(out, SEARCH_BASE, n);
    bool any = false;
    for (const unsigned char *p = (const unsigned char *)words; *p; p++) {
        bool keep = isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~';
        if (*p != ' ')
            any = true;
        if (keep) {
            if (n + 1 >= size)
                return false;
            out[n++] = (char)*p;
        } else {
            if (n + 3 >= size)
                return false;
            snprintf(out + n, 4, "%%%02X", *p);
            n += 3;
        }
    }
    out[n] = '\0';
    return any;
}

static const char *stringField(const cJSON *item, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(item, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

static bool readResult(const cJSON *item, PodcastResult *r)
{
    memset(r, 0, sizeof(*r));
    const char *name = stringField(item, "collectionName");
    copyText(name[0] ? name : stringField(item, "trackName"), r->title, sizeof(r->title));
    copyText(stringField(item, "artistName"), r->author, sizeof(r->author));
    copyUrl(stringField(item, "feedUrl"), r->feed, sizeof(r->feed));
    const char *image = stringField(item, "artworkUrl600");
    copyUrl(image[0] ? image : stringField(item, "artworkUrl100"), r->image, sizeof(r->image));
    copyText(stringField(item, "primaryGenreName"), r->genre, sizeof(r->genre));
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "collectionId");
    r->id = cJSON_IsNumber(id) && id->valuedouble > 0 && id->valuedouble < 1e15 ? (long long)id->valuedouble : 0;
    const cJSON *count = cJSON_GetObjectItemCaseSensitive(item, "trackCount");
    r->episodes = cJSON_IsNumber(count) && count->valuedouble > 0 && count->valuedouble < 1e6 ? count->valueint : 0;
    return r->title[0] && r->feed[0];
}

bool podcastParseSearch(const char *json, PodcastResults *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    const cJSON *results = cJSON_GetObjectItemCaseSensitive(root, "results");
    if (!cJSON_IsArray(results)) {
        cJSON_Delete(root);
        return false;
    }
    int n = cJSON_GetArraySize(results);
    if (n > PODCAST_SEARCH_LIMIT)
        n = PODCAST_SEARCH_LIMIT;
    out->items = n > 0 ? calloc((size_t)n, sizeof(PodcastResult)) : NULL;
    if (n > 0 && !out->items) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON *item;
    cJSON_ArrayForEach(item, results)
    {
        if ((int)out->count >= n)
            break;
        if (cJSON_IsObject(item) && readResult(item, &out->items[out->count]))
            out->count++;
    }
    cJSON_Delete(root);
    return true;
}

void podcastResultsFree(PodcastResults *r)
{
    free(r->items);
    memset(r, 0, sizeof(*r));
}

// --- Charts ---

static bool isCountryCode(const char *cc)
{
    return cc && strlen(cc) == 2 && cc[0] >= 'a' && cc[0] <= 'z' && cc[1] >= 'a' && cc[1] <= 'z';
}

bool podcastChartUrl(const char *cc, int genre, char *out, size_t size)
{
    if (!isCountryCode(cc) || genre < 0 || genre > 99999)
        return false;
    int n = genre ? snprintf(out, size, "https://itunes.apple.com/%s/rss/toppodcasts/limit=%d/genre=%d/json", cc,
                             PODCAST_CHART_MAX, genre)
                  : snprintf(out, size, "https://itunes.apple.com/%s/rss/toppodcasts/limit=%d/json", cc,
                             PODCAST_CHART_MAX);
    return n > 0 && (size_t)n < size;
}

int podcastParseChartIds(const char *json, long long *ids, int max)
{
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    const cJSON *feed = cJSON_GetObjectItemCaseSensitive(root, "feed");
    const cJSON *entries = cJSON_GetObjectItemCaseSensitive(feed, "entry");
    int n = 0;
    const cJSON *one = cJSON_IsObject(entries) ? entries : NULL;  // A chart of one isn't a list.
    const cJSON *entry = one ? one : cJSON_IsArray(entries) ? entries->child : NULL;
    for (; entry && n < max; entry = one ? NULL : entry->next) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(entry, "id");
        const cJSON *attributes = cJSON_GetObjectItemCaseSensitive(id, "attributes");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(attributes, "im:id");
        if (!cJSON_IsString(value) || !value->valuestring)
            continue;
        char *end;
        long long v = strtoll(value->valuestring, &end, 10);
        if (*end || v <= 0 || v >= 1000000000000000LL)
            continue;
        bool seen = false;
        for (int i = 0; i < n && !seen; i++)
            seen = ids[i] == v;
        if (!seen)
            ids[n++] = v;
    }
    cJSON_Delete(root);
    return n;
}

bool podcastLookupUrl(const long long *ids, int count, char *out, size_t size)
{
    int n = snprintf(out, size, "https://itunes.apple.com/lookup?entity=podcast&id=");
    if (count <= 0 || n < 0 || (size_t)n >= size)
        return false;
    for (int i = 0; i < count; i++) {
        int m = snprintf(out + n, size - (size_t)n, "%s%lld", i ? "," : "", ids[i]);
        if (m < 0 || (size_t)(n + m) >= size)
            return false;
        n += m;
    }
    return true;
}

void podcastOrderByIds(PodcastResults *results, const long long *ids, int count)
{
    size_t kept = 0;
    for (int i = 0; i < count; i++)
        for (size_t j = kept; j < results->count; j++)
            if (results->items[j].id == ids[i]) {
                PodcastResult t = results->items[kept];
                results->items[kept] = results->items[j];
                results->items[j] = t;
                kept++;
                break;
            }
    results->count = kept;
}

// --- Feeds ---

typedef enum {
    FIELD_NONE,
    FIELD_TITLE,
    FIELD_AUTHOR,
    FIELD_IMAGE_URL,
    FIELD_ITEM_TITLE,
    FIELD_GUID,
    FIELD_DATE,
    FIELD_DURATION,
} Field;

typedef struct {
    Feed *feed;
    size_t capacity;
    Episode item;
    bool in_item, in_image, has_audio, failed;
    int depth, channel_depth, item_depth, image_depth, field_depth;
    Field field;
    char text[FIELD_MAX];
    size_t text_length;
} FeedReader;

static bool isVideo(const char *type)
{
    return strncasecmp(type, "video/", 6) == 0;
}

static void openItemTag(FeedReader *f, const XmlReader *x)
{
    const char *name = x->name;
    if (!strcmp(name, "title"))
        f->field = FIELD_ITEM_TITLE;
    else if (!strcmp(name, "guid"))
        f->field = FIELD_GUID;
    else if (!strcmp(name, "pubDate"))
        f->field = FIELD_DATE;
    else if (!strcmp(name, "itunes:duration"))
        f->field = FIELD_DURATION;
    else if (!strcmp(name, "enclosure") && !f->has_audio) {
        char url[LINK_URL_MAX], type[sizeof(f->item.type)], length[32];
        xmlAttr(x, "url", url, sizeof(url));
        xmlAttr(x, "type", type, sizeof(type));
        xmlAttr(x, "length", length, sizeof(length));
        copyUrl(url, f->item.url, sizeof(f->item.url));
        if (f->item.url[0] && !isVideo(type)) {
            snprintf(f->item.type, sizeof(f->item.type), "%s", type);
            long long bytes = atoll(length);
            f->item.bytes = bytes > 0 ? bytes : 0;
            f->has_audio = true;
        } else {
            f->item.url[0] = '\0';
        }
    }
}

static void openTag(FeedReader *f, const XmlReader *x)
{
    f->depth++;
    if (f->field != FIELD_NONE)
        return;  // Markup inside a field (rare): its text still goes in.
    const char *name = x->name;
    if (!f->channel_depth) {
        if (!strcmp(name, "channel"))
            f->channel_depth = f->depth;
        return;
    }
    if (f->in_item) {
        if (f->depth == f->item_depth + 1)
            openItemTag(f, x);
        else if (!strcmp(name, "enclosure"))
            openItemTag(f, x);  // Some feeds wrap it.
    } else if (f->in_image) {
        if (f->depth == f->image_depth + 1 && !strcmp(name, "url") && !f->feed->image[0])
            f->field = FIELD_IMAGE_URL;
    } else if (f->depth == f->channel_depth + 1) {
        if (!strcmp(name, "item")) {
            f->in_item = true;
            f->item_depth = f->depth;
            f->has_audio = false;
            memset(&f->item, 0, sizeof(f->item));
        } else if (!strcmp(name, "image")) {
            f->in_image = true;
            f->image_depth = f->depth;
        } else if (!strcmp(name, "title") && !f->feed->title[0]) {
            f->field = FIELD_TITLE;
        } else if (!strcmp(name, "itunes:author") && !f->feed->author[0]) {
            f->field = FIELD_AUTHOR;
        } else if (!strcmp(name, "itunes:image")) {
            char href[LINK_URL_MAX];
            xmlAttr(x, "href", href, sizeof(href));
            copyUrl(href, f->feed->image, sizeof(f->feed->image));  // Preferred over <image>.
        }
    }
    if (f->field != FIELD_NONE) {
        f->field_depth = f->depth;
        f->text_length = 0;
        f->text[0] = '\0';
    }
}

static void endField(FeedReader *f)
{
    Feed *feed = f->feed;
    switch (f->field) {
    case FIELD_TITLE: copyText(f->text, feed->title, sizeof(feed->title)); break;
    case FIELD_AUTHOR: copyText(f->text, feed->author, sizeof(feed->author)); break;
    case FIELD_IMAGE_URL: copyUrl(f->text, feed->image, sizeof(feed->image)); break;
    case FIELD_ITEM_TITLE: copyText(f->text, f->item.title, sizeof(f->item.title)); break;
    case FIELD_GUID: copyText(f->text, f->item.guid, sizeof(f->item.guid)); break;
    case FIELD_DATE: f->item.published = podcastParseDate(f->text); break;
    case FIELD_DURATION: f->item.duration = podcastParseDuration(f->text); break;
    default: break;
    }
    f->field = FIELD_NONE;
}

static void endItem(FeedReader *f)
{
    f->in_item = false;
    if (!f->has_audio || f->feed->count >= FEED_COLLECT_MAX)
        return;
    if (!f->item.guid[0])
        copyText(f->item.url, f->item.guid, sizeof(f->item.guid));  // Its address (its start, if long).
    if (!f->item.title[0])
        snprintf(f->item.title, sizeof(f->item.title), "%s", f->item.guid);
    if (f->feed->count == f->capacity) {
        size_t capacity = f->capacity ? f->capacity * 2 : 32;
        Episode *grown = realloc(f->feed->episodes, capacity * sizeof(Episode));
        if (!grown) {
            f->failed = true;
            return;
        }
        f->feed->episodes = grown;
        f->capacity = capacity;
    }
    f->feed->episodes[f->feed->count++] = f->item;
}

static void closeTag(FeedReader *f)
{
    if (f->field != FIELD_NONE && f->depth == f->field_depth)
        endField(f);
    if (f->in_item && f->depth == f->item_depth)
        endItem(f);
    if (f->in_image && f->depth == f->image_depth)
        f->in_image = false;
    if (f->channel_depth && f->depth == f->channel_depth)
        f->channel_depth = -1;  // Done: anything after the channel is ignored.
    if (f->depth > 0)
        f->depth--;
}

bool podcastParseFeed(const char *xml, size_t length, Feed *out)
{
    memset(out, 0, sizeof(*out));
    if (!xml || length > PODCAST_FEED_MAX)
        return false;
    FeedReader f = {.feed = out};
    XmlReader x;
    xmlBegin(&x, xml, length);
    bool rss = false;
    for (XmlToken t; (t = xmlNext(&x)) != XML_END && !f.failed && f.channel_depth >= 0;) {
        if (t == XML_OPEN) {
            if (!strcmp(x.name, "rss"))
                rss = true;
            openTag(&f, &x);
        } else if (t == XML_CLOSE) {
            closeTag(&f);
        } else if (f.field != FIELD_NONE) {
            f.text_length = xmlTextAppend(&x, f.text, sizeof(f.text), f.text_length);
        }
    }
    if (f.failed || !rss || f.channel_depth == 0) {
        podcastFeedFree(out);
        return false;
    }
    // Newest first. Insertion keeps the feed's order for equal dates (and the undated
    // after the dated), which qsort wouldn't.
    if (out->count > 1) {
        size_t *order = malloc(out->count * sizeof(size_t));
        Episode *sorted = malloc(out->count * sizeof(Episode));
        if (!order || !sorted) {
            free(order);
            free(sorted);
            podcastFeedFree(out);
            return false;
        }
        for (size_t i = 0; i < out->count; i++)
            order[i] = i;
        for (size_t i = 1; i < out->count; i++) {
            size_t k = order[i], j = i;
            while (j > 0 && out->episodes[order[j - 1]].published < out->episodes[k].published) {
                order[j] = order[j - 1];
                j--;
            }
            order[j] = k;
        }
        for (size_t i = 0; i < out->count; i++)
            sorted[i] = out->episodes[order[i]];
        free(order);
        free(out->episodes);
        out->episodes = sorted;
    }
    if (out->count > PODCAST_EPISODES_MAX)
        out->count = PODCAST_EPISODES_MAX;
    return true;
}

void podcastFeedFree(Feed *f)
{
    free(f->episodes);
    memset(f, 0, sizeof(*f));
}

// --- Durations and dates ---

int podcastParseDuration(const char *text)
{
    if (!text)
        return 0;
    long parts[3] = {0};
    int count = 0;
    const char *p = text;
    while (*p == ' ')
        p++;
    while (count < 3) {
        if (!isdigit((unsigned char)*p))
            return 0;
        char *end;
        long v = strtol(p, &end, 10);
        if (v < 0 || v > 1000000)
            return 0;
        parts[count++] = v;
        p = end;
        if (*p == '.') {  // "3723.5": the fraction goes.
            p++;
            while (isdigit((unsigned char)*p))
                p++;
        }
        if (*p != ':')
            break;
        p++;
    }
    while (*p == ' ')
        p++;
    if (*p)
        return 0;
    long seconds = count == 3 ? parts[0] * 3600 + parts[1] * 60 + parts[2]
                 : count == 2 ? parts[0] * 60 + parts[1] : parts[0];
    return seconds > 0 && seconds < 1000000 ? (int)seconds : 0;
}

static int64_t daysFromCivil(int64_t y, int m, int d)  // Days since 1970-01-01 (Howard Hinnant's).
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int monthOf(const char *p)
{
    static const char *const MONTHS[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                         "jul", "aug", "sep", "oct", "nov", "dec"};
    for (int i = 0; i < 12; i++)
        if (!strncasecmp(p, MONTHS[i], 3))
            return i + 1;
    return 0;
}

// The zone after the time, in seconds east of UTC.
static long zoneOf(const char *p)
{
    while (*p == ' ')
        p++;
    if ((*p == '+' || *p == '-') && isdigit((unsigned char)p[1])) {
        int digits = 0;
        long hhmm = 0;
        for (const char *q = p + 1; isdigit((unsigned char)*q) && digits < 4; q++, digits++)
            hhmm = hhmm * 10 + (*q - '0');
        if (digits <= 2)
            hhmm *= 100;
        long seconds = (hhmm / 100) * 3600 + (hhmm % 100) * 60;
        return *p == '-' ? -seconds : seconds;
    }
    static const struct { const char *name; int hours; } ZONES[] = {
        {"EST", -5}, {"EDT", -4}, {"CST", -6}, {"CDT", -5}, {"MST", -7}, {"MDT", -6}, {"PST", -8}, {"PDT", -7}};
    for (size_t i = 0; i < sizeof(ZONES) / sizeof(ZONES[0]); i++)
        if (!strncasecmp(p, ZONES[i].name, 3))
            return ZONES[i].hours * 3600L;
    return 0;  // GMT, UT, Z, or unknown.
}

static int64_t toSeconds(long year, int month, long day, long h, long m, long s, long zone)
{
    if (year < 1970 || year > 2200 || month < 1 || month > 12 || day < 1 || day > 31 || h < 0 || h > 23 || m < 0 ||
        m > 59 || s < 0 || s > 60)
        return 0;
    return daysFromCivil(year, month, (int)day) * 86400 + h * 3600 + m * 60 + s - zone;
}

// "2023-01-05T10:00:00Z", which some feeds use instead.
static int64_t isoDate(const char *p)
{
    long y, mo, d, h = 0, mi = 0, s = 0;
    int used = 0;
    if (sscanf(p, "%4ld-%2ld-%2ld%n", &y, &mo, &d, &used) != 3)
        return 0;
    p += used;
    if (*p == 'T' || *p == ' ') {
        int more = 0;
        if (sscanf(p + 1, "%2ld:%2ld%n", &h, &mi, &more) == 2) {
            p += 1 + more;
            if (*p == ':') {
                int sec = 0;
                if (sscanf(p + 1, "%2ld%n", &s, &sec) == 1)
                    p += 1 + sec;
            }
            while (*p == '.' || isdigit((unsigned char)*p))
                p++;
        }
    }
    return toSeconds(y, (int)mo, d, h, mi, s, *p == 'Z' ? 0 : zoneOf(p));
}

int64_t podcastParseDate(const char *text)
{
    if (!text)
        return 0;
    const char *p = text;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2]) &&
        isdigit((unsigned char)p[3]) && p[4] == '-')
        return isoDate(p);
    if (isalpha((unsigned char)*p)) {  // The day's name: "Wed,".
        while (isalpha((unsigned char)*p))
            p++;
        if (*p == ',')
            p++;
    }
    char *end;
    long day = strtol(p, &end, 10);
    if (end == p)
        return 0;
    p = end;
    while (*p == ' ' || *p == '-')
        p++;
    int month = monthOf(p);
    if (!month)
        return 0;
    while (isalpha((unsigned char)*p))
        p++;
    while (*p == ' ' || *p == '-')
        p++;
    long year = strtol(p, &end, 10);
    if (end == p)
        return 0;
    if (end - p <= 2)
        year += year < 70 ? 2000 : 1900;
    p = end;
    long h = 0, m = 0, s = 0;
    int used = 0;
    if (sscanf(p, " %2ld:%2ld%n", &h, &m, &used) == 2) {
        p += used;
        if (*p == ':') {
            int sec = 0;
            if (sscanf(p + 1, "%2ld%n", &s, &sec) == 1)
                p += 1 + sec;
        }
    }
    return toSeconds(year, month, day, h, m, s, zoneOf(p));
}
