// The podcasts kept on the card: subscriptions, their episodes, and marks.
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "podstore.h"

static PodcastResult result(const char *title, const char *feed)
{
    PodcastResult r;
    memset(&r, 0, sizeof(r));
    snprintf(r.title, sizeof(r.title), "%s", title);
    snprintf(r.author, sizeof(r.author), "%s", "Kai");
    snprintf(r.feed, sizeof(r.feed), "%s", feed);
    snprintf(r.image, sizeof(r.image), "%s", "https://img.example/a.jpg");
    return r;
}

static Episode episode(const char *guid, int64_t published, int duration)
{
    Episode e;
    memset(&e, 0, sizeof(e));
    snprintf(e.guid, sizeof(e.guid), "%s", guid);
    snprintf(e.title, sizeof(e.title), "Episode %s", guid);
    snprintf(e.url, sizeof(e.url), "https://cdn.example/%s.mp3", guid);
    snprintf(e.type, sizeof(e.type), "%s", "audio/mpeg");
    e.published = published;
    e.duration = duration;
    e.bytes = 1000;
    return e;
}

static void testSubscriptions(void)
{
    PodStore s;
    podStoreLoad(&s, TMP_ROOT "/podstore");
    podStoreFree(&s);
    remove(TMP_ROOT "/podstore/podcasts.json");
    remove(TMP_ROOT "/podstore/podcast_marks.json");
    podStoreLoad(&s, TMP_ROOT "/podstore");
    CHECK(s.sub_count == 0 && s.mark_count == 0);

    PodcastResult a = result("Alpha", "https://feeds.example/a"), b = result("Beta", "https://feeds.example/b");
    CHECK(podStoreSubscribe(&s, &a, 1000) == 0 && podStoreSubscribe(&s, &b, 1000) == 0);
    CHECK(s.sub_count == 2 && !strcmp(s.subs[0].title, "Beta"));  // The newest at the top.
    CHECK(podStoreSubscribe(&s, &a, 2000) == 1 && s.sub_count == 2);  // Already there.
    PodcastResult bad = result("Bad", "file:///x");
    CHECK(podStoreSubscribe(&s, &bad, 1000) == -1);

    // A refresh: episodes, and how many are new since the last one.
    Episode list[3] = {episode("3", 3000, 600), episode("2", 2000, 600), episode("1", 500, 0)};
    Feed feed = {.episodes = list, .count = 3};
    snprintf(feed.title, sizeof(feed.title), "%s", "Alpha (renamed)");
    snprintf(feed.image, sizeof(feed.image), "%s", "https://feeds.example/huge-3000px.jpg");
    int alpha = podStoreFind(&s, "https://feeds.example/a");
    CHECK(alpha == 1 && podStoreTakeFeed(&s, alpha, &feed, 5000) == 0);  // The first read: none "new".
    CHECK(s.subs[alpha].count == 3 && s.subs[alpha].checked == 5000 &&
          !strcmp(s.subs[alpha].title, "Alpha (renamed)") && !strcmp(s.subs[alpha].author, "Kai"));
    CHECK(!strcmp(s.subs[alpha].image, "https://img.example/a.jpg"));  // The search's picture stays.
    CHECK(podStoreNewCount(&s.subs[alpha]) == 2);  // Published after it was subscribed (1000).
    podStoreSeen(&s, alpha, 4000);
    CHECK(podStoreNewCount(&s.subs[alpha]) == 0);
    Episode more[4] = {episode("4", 4500, 0), list[0], list[1], list[2]};
    Feed later = {.episodes = more, .count = 4};
    CHECK(podStoreTakeFeed(&s, alpha, &later, 6000) == 1 && podStoreNewCount(&s.subs[alpha]) == 1);
    CHECK(podStoreTakeFeed(&s, 9, &later, 6000) == -1);

    // Saved and read back.
    CHECK(podStoreSaveSubs(&s));
    PodStore back;
    podStoreLoad(&back, TMP_ROOT "/podstore");
    CHECK(back.sub_count == 2 && !strcmp(back.subs[1].feed, "https://feeds.example/a") &&
          back.subs[1].count == 4 && back.subs[1].seen == 4000 && back.subs[1].episodes[0].published == 4500 &&
          !strcmp(back.subs[1].episodes[1].url, "https://cdn.example/3.mp3") && back.subs[1].episodes[1].duration == 600);
    podStoreFree(&back);

    CHECK(podStoreUnsubscribe(&s, 0) && s.sub_count == 1 && !strcmp(s.subs[0].title, "Alpha (renamed)"));
    CHECK(!podStoreUnsubscribe(&s, 5));
    podStoreFree(&s);

    // Damaged or odd files give what can be read.
    CHECK(!podStoreReadSubs(&s, "not json") && s.sub_count == 0);
    CHECK(podStoreReadSubs(&s, "[{\"feed\":\"ftp://x\",\"title\":\"X\"},{\"feed\":\"https://f.example\"},"
                               "{\"feed\":\"https://f.example\",\"title\":\"Ok\",\"episodes\":[{\"guid\":\"g\","
                               "\"url\":\"javascript:x\"},{\"guid\":\"h\",\"url\":\"https://e.example/h.mp3\","
                               "\"published\":-5}]},{\"feed\":\"https://f.example\",\"title\":\"Twice\"}]"));
    CHECK(s.sub_count == 1 && !strcmp(s.subs[0].title, "Ok") && s.subs[0].count == 1 &&
          s.subs[0].episodes[0].published == 0);
    podStoreFree(&s);
}

static void testMarks(void)
{
    PodStore s;
    memset(&s, 0, sizeof(s));
    snprintf(s.folder, sizeof(s.folder), "%s", TMP_ROOT "/podstore");
    const char *feed = "https://feeds.example/a";
    CHECK(podStoreKey(feed, "1") != podStoreKey(feed, "2") && podStoreKey("ab", "c") != podStoreKey("a", "bc"));
    int position;
    bool played;
    CHECK(!podStoreMark(&s, feed, "1", &position, &played) && position == 0 && !played);
    podStoreSetPosition(&s, feed, "1", 754, 3600, 100);
    CHECK(podStoreMark(&s, feed, "1", &position, &played) && position == 754 && !played);
    podStoreSetPosition(&s, feed, "1", 3590, 3600, 200);  // Within the last 30 s: heard, from the start next.
    CHECK(podStoreMark(&s, feed, "1", &position, &played) && position == 0 && played);
    podStoreSetPlayed(&s, feed, "1", false, 300);
    CHECK(podStoreMark(&s, feed, "1", &position, &played) && position == 0 && !played);
    podStoreSetPosition(&s, feed, "2", 120, 0, 400);  // Unknown length: never "heard" by position.
    CHECK(podStoreMark(&s, feed, "2", &position, &played) && position == 120 && !played);

    CHECK(podStoreSaveMarks(&s));
    PodStore back;
    podStoreLoad(&back, TMP_ROOT "/podstore");
    CHECK(back.mark_count == 2 && podStoreMark(&back, feed, "2", &position, &played) && position == 120);
    podStoreFree(&back);
    CHECK(podStoreReadMarks(&s, "[{\"key\":\"zz\",\"position\":5},{\"key\":\"0123456789abcdef\",\"position\":7}]") &&
          s.mark_count == 1 && s.marks[0].key == 0x0123456789abcdefull && s.marks[0].position == 7);

    // At the limit, the oldest mark makes room.
    free(s.marks);
    s.marks = NULL;
    s.mark_count = 0;
    char guid[16];
    for (int i = 0; i < PODSTORE_MARKS_MAX; i++) {
        snprintf(guid, sizeof(guid), "%d", i);
        podStoreSetPosition(&s, feed, guid, 10, 0, 1000 + i);
    }
    podStoreSetPosition(&s, feed, "new", 10, 0, 999999);
    CHECK(s.mark_count == PODSTORE_MARKS_MAX && !podStoreMark(&s, feed, "0", &position, &played) &&
          podStoreMark(&s, feed, "new", &position, &played) && podStoreMark(&s, feed, "1", &position, &played));
    podStoreFree(&s);
}

void testPodStore(void)
{
    testSubscriptions();
    testMarks();
}
