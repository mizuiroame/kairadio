// HLS playlists (RFC 8216), as far as live radio needs: a master playlist
// names the variants (bitrates); a media playlist names the segments, which
// are fetched one after another. Pure logic.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "link.h"

typedef struct {
    char url[LINK_URL_MAX];
    long sequence;  // Media sequence number.
} HlsSegment;

typedef struct {
    bool master;                 // A list of variants; see variant.
    char variant[LINK_URL_MAX];  // The chosen variant, resolved.
    long variant_bandwidth;      // bits/s; 0 if not given.
    double target_duration;      // Seconds.
    bool ended;                  // #EXT-X-ENDLIST: no more segments will come.
    bool fragmented_mp4;         // #EXT-X-MAP: fMP4 segments, which KaiRadio can't read.
    bool encrypted;              // #EXT-X-KEY with a method other than NONE.
    HlsSegment *segments;
    size_t count;
} HlsPlaylist;

// Parses body (from base). For a master playlist, picks the variant with the
// highest bandwidth up to max_bandwidth (bits/s), or the lowest if all are
// above it. False if it isn't an HLS playlist or memory runs out.
bool hlsParse(const char *body, const char *base, long max_bandwidth, HlsPlaylist *out);
void hlsFree(HlsPlaylist *p);

// The index of the first segment to fetch: when starting (last_sequence < 0),
// a few segments from the live end; then the one after last_sequence, or
// after a gap the oldest still listed. count when there's nothing new.
size_t hlsFirstSegment(const HlsPlaylist *p, long last_sequence);
