#include "link.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static bool startsWithNoCase(const char *s, const char *prefix)
{
    return strncasecmp(s, prefix, strlen(prefix)) == 0;
}

// True if the URL's path (before ? or #) ends in ext, ignoring case.
static bool urlHasExtension(const char *url, const char *ext)
{
    size_t end = strcspn(url, "?#");
    size_t len = strlen(ext);
    return end >= len && strncasecmp(url + end - len, ext, len) == 0;
}

static const char *skipSpace(const char *s, const char *end)
{
    if (end - s >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        s += 3;  // A UTF-8 byte order mark.
    while (s < end && isspace((unsigned char)*s))
        s++;
    return s;
}

static bool headHas(const char *head, size_t size, const char *needle)
{
    size_t n = strlen(needle);
    for (size_t i = 0; i + n <= size; i++)
        if (memcmp(head + i, needle, n) == 0)
            return true;
    return false;
}

static LinkKind m3uOrHls(const char *head, size_t size)
{
    return headHas(head, size, "#EXT-X-") ? LINK_HLS : LINK_M3U;
}

LinkKind linkKind(const char *url, const char *content_type, const char *head, size_t head_size)
{
    const char *end = head + head_size;
    const char *text = head_size ? skipSpace(head, end) : end;
    size_t text_size = (size_t)(end - text);
    if (text_size >= 10 && strncasecmp(text, "[playlist]", 10) == 0)
        return LINK_PLS;
    if (text_size >= 7 && strncmp(text, "#EXTM3U", 7) == 0)
        return m3uOrHls(text, text_size);

    const char *type = content_type ? content_type : "";
    if (startsWithNoCase(type, "audio/x-scpls") || startsWithNoCase(type, "application/pls"))
        return LINK_PLS;
    if (startsWithNoCase(type, "application/vnd.apple.mpegurl") || startsWithNoCase(type, "application/x-mpegurl") ||
        startsWithNoCase(type, "audio/x-mpegurl") || startsWithNoCase(type, "audio/mpegurl"))
        return m3uOrHls(text, text_size);
    if (startsWithNoCase(type, "audio/") || startsWithNoCase(type, "application/ogg") ||
        startsWithNoCase(type, "video/"))
        return LINK_AUDIO;

    // No useful type: a body of text lines with an address is a playlist.
    if (text_size >= 7 && (startsWithNoCase(text, "http://") || startsWithNoCase(text, "https://")))
        return LINK_M3U;
    if (urlHasExtension(url, ".pls"))
        return LINK_PLS;
    if (urlHasExtension(url, ".m3u8"))
        return m3uOrHls(text, text_size) == LINK_HLS || !head_size ? LINK_HLS : LINK_M3U;
    if (urlHasExtension(url, ".m3u"))
        return LINK_M3U;
    return LINK_AUDIO;
}

static bool copyTrimmed(const char *start, size_t len, char *out, size_t size)
{
    while (len > 0 && isspace((unsigned char)start[len - 1]))
        len--;
    while (len > 0 && isspace((unsigned char)*start)) {
        start++;
        len--;
    }
    if (len == 0 || len >= size)
        return false;
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

bool linkResolve(const char *base, const char *ref, char *out, size_t size)
{
    if (strstr(ref, "://"))
        return snprintf(out, size, "%s", ref) < (int)size;
    const char *scheme = strstr(base, "://");
    if (!scheme)
        return false;
    const char *host = scheme + 3;
    size_t host_end = (size_t)(host - base) + strcspn(host, "/?#");
    if (ref[0] == '/') {
        if (ref[1] == '/') {  // Same scheme, another host.
            size_t scheme_len = (size_t)(host - base) - 2;
            return snprintf(out, size, "%.*s%s", (int)scheme_len, base, ref) < (int)size;
        }
        return snprintf(out, size, "%.*s%s", (int)host_end, base, ref) < (int)size;
    }
    size_t path_end = host_end + strcspn(base + host_end, "?#");
    size_t dir = host_end;
    for (size_t i = host_end; i < path_end; i++)
        if (base[i] == '/')
            dir = i + 1;
    if (dir == host_end)  // "http://host" with no path.
        return snprintf(out, size, "%.*s/%s", (int)host_end, base, ref) < (int)size;
    return snprintf(out, size, "%.*s%s", (int)dir, base, ref) < (int)size;
}

// The next line of body: start and length, without the line break.
static const char *nextLine(const char *s, size_t *len)
{
    *len = strcspn(s, "\r\n");
    return s;
}

static const char *afterLine(const char *s, size_t len)
{
    s += len;
    while (*s == '\r' || *s == '\n')
        s++;
    return s;
}

bool linkFirstUrl(LinkKind kind, const char *body, const char *base, char *out, size_t size)
{
    char ref[LINK_URL_MAX];
    for (const char *s = body; *s;) {
        size_t len;
        const char *line = nextLine(s, &len);
        s = afterLine(s, len);
        while (len > 0 && isspace((unsigned char)*line)) {
            line++;
            len--;
        }
        if (kind == LINK_PLS) {
            // "File1=http://..." (any number).
            if (len < 5 || strncasecmp(line, "File", 4) != 0)
                continue;
            const char *eq = memchr(line, '=', len);
            if (!eq)
                continue;
            if (copyTrimmed(eq + 1, len - (size_t)(eq + 1 - line), ref, sizeof(ref)) && linkResolve(base, ref, out, size))
                return true;
        } else {
            if (len == 0 || line[0] == '#')
                continue;
            if (copyTrimmed(line, len, ref, sizeof(ref)) && linkResolve(base, ref, out, size))
                return true;
        }
    }
    return false;
}
