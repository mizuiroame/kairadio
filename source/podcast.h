// Podcasts: what Apple's podcast search (the iTunes Search API) answers, and
// a podcast's feed (RSS): its name, picture and episodes. Pure logic;
// podfetch.c asks the network.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link.h"

#define PODCAST_TITLE_MAX 160
#define PODCAST_TEXT_MAX 96
#define PODCAST_GUID_MAX 160
#define PODCAST_EPISODES_MAX 300   // The newest kept from a feed.
#define PODCAST_SEARCH_LIMIT 50
#define PODCAST_FEED_MAX (8 * 1024 * 1024)  // A feed bigger than this isn't read.

// One answer of a search.
typedef struct {
    char title[PODCAST_TITLE_MAX];
    char author[PODCAST_TEXT_MAX];
    char feed[LINK_URL_MAX];   // Its RSS feed (http or https).
    char image[LINK_URL_MAX];  // Its picture; may be empty.
    char genre[PODCAST_TEXT_MAX];
    int episodes;              // As the search says; 0 if unknown.
    long long id;              // Apple's id for it; 0 if unknown.
} PodcastResult;

typedef struct {
    PodcastResult *items;
    size_t count;
} PodcastResults;

typedef struct {
    char guid[PODCAST_GUID_MAX];    // The feed's id for it; its address when it has none.
    char title[PODCAST_TITLE_MAX];
    char url[LINK_URL_MAX];         // The audio (the enclosure).
    char type[32];                  // "audio/mpeg"...; may be empty.
    int64_t bytes;                  // 0 if unknown.
    int64_t published;              // Seconds since 1970 (UTC); 0 if unknown.
    int duration;                   // Seconds; 0 if unknown.
} Episode;

typedef struct {
    char title[PODCAST_TITLE_MAX];
    char author[PODCAST_TEXT_MAX];
    char image[LINK_URL_MAX];
    Episode *episodes;  // Newest first.
    size_t count;
} Feed;

// The search's address for these words. False if they don't fit or are empty.
bool podcastSearchUrl(const char *words, char *out, size_t size);
// Reads the search's JSON answer. Podcasts without a name or an http(s) feed
// are left out. False if it isn't the search's answer, or memory runs out.
bool podcastParseSearch(const char *json, PodcastResults *out);
void podcastResultsFree(PodcastResults *r);

// Apple's charts: the top podcasts of a country's store (cc: "mx", "us"...),
// of one genre (an Apple genre id; 0 for all). Two steps: the chart gives
// ids, a lookup of them gives their feeds.
#define PODCAST_CHART_MAX 50
bool podcastChartUrl(const char *cc, int genre, char *out, size_t size);
// The ids in the chart's JSON answer, in its order. Returns how many (up to max).
int podcastParseChartIds(const char *json, long long *ids, int max);
// The lookup's address for these ids.
bool podcastLookupUrl(const long long *ids, int count, char *out, size_t size);
// Puts results (the lookup's answer, read with podcastParseSearch) in the
// chart's order; ids not found are left out.
void podcastOrderByIds(PodcastResults *results, const long long *ids, int count);

// Reads a feed. Episodes without audio (an http(s) enclosure that isn't a
// video) are left out; the newest PODCAST_EPISODES_MAX are kept. False if
// it isn't an RSS feed with a channel, or memory runs out.
bool podcastParseFeed(const char *xml, size_t length, Feed *out);
void podcastFeedFree(Feed *f);

// "1:02:03", "62:03" or "3723" (itunes:duration) in seconds; 0 if unreadable.
int podcastParseDuration(const char *text);
// An RFC 822 date ("Wed, 02 Oct 2002 13:00:00 GMT", "+0200"...) in seconds
// since 1970 (UTC); 0 if unreadable.
int64_t podcastParseDate(const char *text);
