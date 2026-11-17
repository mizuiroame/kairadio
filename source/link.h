// What a station's address leads to: the audio itself, a playlist file
// (.pls or .m3u) that names the stream, or an HLS playlist of segments.
// Pure logic.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define LINK_URL_MAX 1024

typedef enum {
    LINK_AUDIO,
    LINK_PLS,
    LINK_M3U,
    LINK_HLS,
} LinkKind;

// Decides from the response's Content-Type (may be NULL or empty) and its
// first bytes (head, may be empty). The URL's extension is the last hint.
LinkKind linkKind(const char *url, const char *content_type, const char *head, size_t head_size);

// The first stream address in a .pls or .m3u body, resolved against base
// (the playlist's own address). False if there is none or it doesn't fit.
bool linkFirstUrl(LinkKind kind, const char *body, const char *base, char *out, size_t size);

// Resolves ref against base: absolute URLs stay, "/x" is on base's host, and
// anything else is next to base's last '/'. False if it doesn't fit.
bool linkResolve(const char *base, const char *ref, char *out, size_t size);
