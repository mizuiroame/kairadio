#include "hls.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// Segments from the live end where playback starts, as players usually do.
#define LIVE_EDGE_SEGMENTS 3

static bool startsWith(const char *s, size_t len, const char *prefix)
{
    size_t n = strlen(prefix);
    return len >= n && strncmp(s, prefix, n) == 0;
}

// The value of an attribute in an attribute list ("BANDWIDTH=128000,...").
// False if it isn't there, or doesn't fit (a cut URI would be another one).
static bool attribute(const char *list, size_t len, const char *name, char *out, size_t size)
{
    size_t n = strlen(name);
    for (size_t i = 0; i + n < len; i++) {
        bool at_start = i == 0 || list[i - 1] == ',' || list[i - 1] == ':';
        if (!at_start || strncmp(list + i, name, n) != 0 || list[i + n] != '=')
            continue;
        const char *v = list + i + n + 1;
        size_t left = len - (i + n + 1);
        size_t vlen;
        if (left > 0 && *v == '"') {
            v++;
            left--;
            const char *q = memchr(v, '"', left);
            vlen = q ? (size_t)(q - v) : left;
        } else {
            const char *c = memchr(v, ',', left);
            vlen = c ? (size_t)(c - v) : left;
        }
        if (vlen >= size)
            return false;
        memcpy(out, v, vlen);
        out[vlen] = '\0';
        return true;
    }
    return false;
}

static bool addSegment(HlsPlaylist *p, const char *url, long sequence, size_t *capacity)
{
    if (p->count == HLS_SEGMENTS_MAX) {
        // A live list only needs its newest segments; a huge one can't use up memory.
        memmove(p->segments, p->segments + 1, (p->count - 1) * sizeof(HlsSegment));
        p->count--;
    }
    if (p->count == *capacity) {
        size_t grown = *capacity ? *capacity * 2 : 16;
        HlsSegment *items = realloc(p->segments, grown * sizeof(HlsSegment));
        if (!items)
            return false;
        p->segments = items;
        *capacity = grown;
    }
    HlsSegment *s = &p->segments[p->count++];
    snprintf(s->url, sizeof(s->url), "%s", url);
    s->sequence = sequence;
    return true;
}

// Keeps the variant if it is a better pick than the one so far.
static void considerVariant(HlsPlaylist *p, const char *url, long bandwidth, long max_bandwidth, bool *have)
{
    bool better;
    if (!*have)
        better = true;
    else if (bandwidth <= max_bandwidth && p->variant_bandwidth > max_bandwidth)
        better = true;  // The first one that fits.
    else if (bandwidth <= max_bandwidth)
        better = bandwidth > p->variant_bandwidth;
    else
        better = p->variant_bandwidth > max_bandwidth && bandwidth < p->variant_bandwidth;
    if (better) {
        snprintf(p->variant, sizeof(p->variant), "%s", url);
        p->variant_bandwidth = bandwidth;
        *have = true;
    }
}

bool hlsParse(const char *body, const char *base, long max_bandwidth, HlsPlaylist *out)
{
    memset(out, 0, sizeof(*out));
    const char *s = body;
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        s += 3;
    while (isspace((unsigned char)*s))
        s++;
    if (strncmp(s, "#EXTM3U", 7) != 0)
        return false;

    size_t capacity = 0;
    long sequence = 0;
    bool next_is_variant = false, have_variant = false;
    long next_bandwidth = 0;
    char resolved[LINK_URL_MAX], value[64];
    while (*s) {
        size_t len = strcspn(s, "\r\n");
        const char *line = s;
        s += len;
        while (*s == '\r' || *s == '\n')
            s++;
        while (len > 0 && isspace((unsigned char)line[len - 1]))
            len--;
        if (len == 0)
            continue;
        if (line[0] == '#') {
            if (startsWith(line, len, "#EXT-X-STREAM-INF:")) {
                next_is_variant = true;
                next_bandwidth = attribute(line + 18, len - 18, "BANDWIDTH", value, sizeof(value)) ? atol(value) : 0;
                out->master = true;
            } else if (startsWith(line, len, "#EXT-X-MEDIA-SEQUENCE:")) {
                sequence = strtol(line + 22, NULL, 10);
                if (sequence < 0 || sequence > HLS_SEQUENCE_MAX)
                    sequence = 0;  // Room to count on without overflowing.
            } else if (startsWith(line, len, "#EXT-X-TARGETDURATION:")) {
                double seconds = atof(line + 22);
                // Only a sane wait between rereads: "inf" or 1e300 would never wake up.
                out->target_duration = seconds >= 0 && seconds <= HLS_TARGET_MAX_S ? seconds : HLS_TARGET_MAX_S;
            } else if (startsWith(line, len, "#EXT-X-ENDLIST")) {
                out->ended = true;
            } else if (startsWith(line, len, "#EXT-X-MAP:")) {
                // The init segment that goes before the fMP4 segments. A byte
                // range of a file isn't fetched, so a map with one is left out.
                out->fragmented_mp4 = true;
                char uri[LINK_URL_MAX];
                out->map[0] = '\0';
                if (attribute(line + 11, len - 11, "URI", uri, sizeof(uri)) &&
                    !attribute(line + 11, len - 11, "BYTERANGE", value, sizeof(value)) &&
                    !linkResolve(base, uri, out->map, sizeof(out->map)))
                    out->map[0] = '\0';
            } else if (startsWith(line, len, "#EXT-X-KEY:")) {
                out->encrypted = !(attribute(line + 11, len - 11, "METHOD", value, sizeof(value)) &&
                                   strcasecmp(value, "NONE") == 0);
            }
            continue;
        }
        char ref[LINK_URL_MAX];
        bool usable = len < sizeof(ref);
        if (usable) {
            memcpy(ref, line, len);
            ref[len] = '\0';
            usable = linkResolve(base, ref, resolved, sizeof(resolved));
        }
        if (!usable) {
            next_is_variant = false;
            continue;
        }
        if (next_is_variant) {
            considerVariant(out, resolved, next_bandwidth, max_bandwidth, &have_variant);
            next_is_variant = false;
        } else if (!out->master) {
            if (!addSegment(out, resolved, sequence, &capacity)) {
                hlsFree(out);
                return false;
            }
            sequence++;
        }
    }
    if (out->master) {
        free(out->segments);
        out->segments = NULL;
        out->count = 0;
        return have_variant;
    }
    return true;
}

void hlsFree(HlsPlaylist *p)
{
    free(p->segments);
    p->segments = NULL;
    p->count = 0;
}

size_t hlsFirstSegment(const HlsPlaylist *p, long last_sequence)
{
    if (last_sequence < 0)
        return p->count > LIVE_EDGE_SEGMENTS ? p->count - LIVE_EDGE_SEGMENTS : 0;
    // The next one, or after a gap, the oldest still listed.
    for (size_t i = 0; i < p->count; i++)
        if (p->segments[i].sequence > last_sequence)
            return i;
    return p->count;  // Nothing new yet.
}
