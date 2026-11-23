// The podcasts kept on the card: the ones subscribed to, with their newest
// episodes (so the Podcasts tab shows them before the network answers), and
// where each episode was left and whether it was heard. Two JSON files in
// KaiRadio's folder; nothing of it leaves the console.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "podcast.h"

#define PODSTORE_SUBS_MAX 100
#define PODSTORE_SAVED_EPISODES 50  // Per podcast, on the card; a refresh brings the rest.
#define PODSTORE_MARKS_MAX 3000     // Episodes remembered; the oldest go first.
#define PODSTORE_PLAYED_LEFT 30     // Seconds from the end that count as heard.

typedef struct {
    char feed[LINK_URL_MAX];  // Its id here.
    char title[PODCAST_TITLE_MAX];
    char author[PODCAST_TEXT_MAX];
    char image[LINK_URL_MAX];
    int64_t checked;          // When its feed was last read (seconds since 1970); 0: never.
    int64_t seen;             // When its page was last opened: newer episodes are new.
    Episode *episodes;        // Newest first.
    size_t count;
} Subscription;

typedef struct {
    uint64_t key;      // podStoreKey(feed, guid).
    int position;      // Seconds in.
    bool played;
    int64_t updated;   // When it was last set.
} EpisodeMark;

typedef struct {
    Subscription *subs;
    size_t sub_count;
    EpisodeMark *marks;
    size_t mark_count;
    char folder[256];
} PodStore;

// Loads both files from folder (created when saving). Missing or damaged files
// give an empty store.
void podStoreLoad(PodStore *s, const char *folder);
void podStoreFree(PodStore *s);
// Write the files (through a temporary file). False if the card couldn't take them.
bool podStoreSaveSubs(const PodStore *s);
bool podStoreSaveMarks(const PodStore *s);

// The subscription with this feed, or -1.
int podStoreFind(const PodStore *s, const char *feed);
// Subscribes (at the top) and saves. Returns its index, or -1 (memory, the
// limit, or a result without a feed). Subscribing again returns the one there.
int podStoreSubscribe(PodStore *s, const PodcastResult *r, int64_t now);
// Unsubscribes and saves. Its marks are kept, for a subscription again.
bool podStoreUnsubscribe(PodStore *s, int index);
// Takes a refreshed feed for a subscription: its name when it has one, its
// picture when the subscription had none, and its episodes (copied). Returns how many are newer than the
// newest it had, or -1 if memory ran out (the old episodes then stay).
// Doesn't save.
int podStoreTakeFeed(PodStore *s, int index, const Feed *feed, int64_t now);
// Episodes published after its page was last opened.
int podStoreNewCount(const Subscription *sub);
// Its page was opened now.
void podStoreSeen(PodStore *s, int index, int64_t now);

uint64_t podStoreKey(const char *feed, const char *guid);
// Where an episode was left; false (0, not played) if it never was.
bool podStoreMark(const PodStore *s, const char *feed, const char *guid, int *position, bool *played);
// Sets where it was left. Within PODSTORE_PLAYED_LEFT of duration (when
// known) it's heard, and starts over next time. Doesn't save.
void podStoreSetPosition(PodStore *s, const char *feed, const char *guid, int position, int duration, int64_t now);
// Marks it heard or not (not heard: from the start). Doesn't save.
void podStoreSetPlayed(PodStore *s, const char *feed, const char *guid, bool played, int64_t now);

// The files' text, for tests. NULL if memory runs out; free() it.
char *podStoreSubsJson(const PodStore *s);
char *podStoreMarksJson(const PodStore *s);
// Read them; false (and nothing kept) if the text isn't one.
bool podStoreReadSubs(PodStore *s, const char *json);
bool podStoreReadMarks(PodStore *s, const char *json);
