// Podcasts: the XML reader, feeds, Apple's search answers, durations and dates.
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include "check.h"
#include "fake_net.h"
#include "podfetch.h"
#include "podcast.h"
#include "xmlread.h"

static void testXml(void)
{
    const char *doc = "<?xml version=\"1.0\"?><!-- a note --><rss a='1' b=\"x &amp; y\">"
                      "<t>A &lt;b&gt; &#233;&#x4E2D; &bogus; &</t><e url=\"u\"/><![CDATA[<raw> &amp;]]></rss>";
    XmlReader r;
    xmlBegin(&r, doc, strlen(doc));
    char out[64];
    CHECK(xmlNext(&r) == XML_OPEN && !strcmp(r.name, "rss"));
    CHECK(xmlAttr(&r, "b", out, sizeof(out)) && !strcmp(out, "x & y"));
    CHECK(xmlAttr(&r, "a", out, sizeof(out)) && !strcmp(out, "1"));
    CHECK(!xmlAttr(&r, "c", out, sizeof(out)) && out[0] == '\0');
    CHECK(xmlNext(&r) == XML_OPEN && !strcmp(r.name, "t"));
    CHECK(xmlNext(&r) == XML_TEXT);
    size_t n = xmlTextAppend(&r, out, sizeof(out), 0);
    CHECK(!strcmp(out, "A <b> \xC3\xA9\xE4\xB8\xAD &bogus; &") && n == strlen(out));
    CHECK(xmlNext(&r) == XML_CLOSE && !strcmp(r.name, "t"));
    CHECK(xmlNext(&r) == XML_OPEN && !strcmp(r.name, "e") && xmlAttr(&r, "url", out, sizeof(out)) &&
          !strcmp(out, "u"));
    CHECK(xmlNext(&r) == XML_CLOSE && !strcmp(r.name, "e"));  // <e/> closes itself.
    CHECK(xmlNext(&r) == XML_TEXT);
    xmlTextAppend(&r, out, sizeof(out), 0);
    CHECK(!strcmp(out, "<raw> &amp;"));  // CDATA: as it is.
    CHECK(xmlNext(&r) == XML_CLOSE && xmlNext(&r) == XML_END && xmlNext(&r) == XML_END);

    // Text is cut between characters, never inside one.
    const char *wide = "<a>\xC3\xA9\xC3\xA9\xC3\xA9</a>";
    xmlBegin(&r, wide, strlen(wide));
    xmlNext(&r);
    xmlNext(&r);
    char small[6];
    xmlTextAppend(&r, small, sizeof(small), 0);
    CHECK(!strcmp(small, "\xC3\xA9\xC3\xA9"));

    // A broken document ends without reading past its end.
    const char *broken = "<a b=\"unterminated><c";
    xmlBegin(&r, broken, strlen(broken));
    int tokens = 0;
    while (xmlNext(&r) != XML_END && tokens < 10)
        tokens++;
    CHECK(tokens < 10);
}

static const char FEED[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<rss version=\"2.0\" xmlns:itunes=\"http://www.itunes.com/dtds/podcast-1.0.dtd\">\n"
    "<channel>\n"
    "  <title>  Made-up   Show </title>\n"
    "  <itunes:author>Kai &amp; Friends</itunes:author>\n"
    "  <image><url>https://example.com/small.jpg</url><title>ignored</title></image>\n"
    "  <itunes:image href=\"https://example.com/big.jpg\"/>\n"
    "  <item>\n"
    "    <title>Older one</title><guid isPermaLink=\"false\">ep-1</guid>\n"
    "    <pubDate>Mon, 01 Jan 2024 10:00:00 +0100</pubDate>\n"
    "    <itunes:duration>45:30</itunes:duration>\n"
    "    <enclosure url=\"https://cdn.example.com/1.mp3\" type=\"audio/mpeg\" length=\"1234\"/>\n"
    "  </item>\n"
    "  <item>\n"
    "    <title><![CDATA[Newer <one>]]></title>\n"
    "    <pubDate>Tue, 02 Jan 2024 09:00:00 GMT</pubDate>\n"
    "    <itunes:duration>3723</itunes:duration>\n"
    "    <enclosure url=\"https://cdn.example.com/2.m4a\" type=\"audio/x-m4a\" length=\"99\"/>\n"
    "  </item>\n"
    "  <item><title>A video</title><enclosure url=\"https://cdn.example.com/v.mp4\" type=\"video/mp4\"/></item>\n"
    "  <item><title>No audio</title></item>\n"
    "  <item><title>Not the web</title><enclosure url=\"ftp://x/3.mp3\" type=\"audio/mpeg\"/></item>\n"
    "  <item><title>Undated</title><enclosure url=\"http://cdn.example.com/u.mp3\"/></item>\n"
    "</channel>\n"
    "</rss>\n";

static void testFeed(void)
{
    Feed f;
    CHECK(podcastParseFeed(FEED, strlen(FEED), &f));
    CHECK(!strcmp(f.title, "Made-up Show") && !strcmp(f.author, "Kai & Friends"));
    CHECK(!strcmp(f.image, "https://example.com/big.jpg"));  // itunes:image over <image>.
    CHECK(f.count == 3);
    if (f.count == 3) {
        CHECK(!strcmp(f.episodes[0].title, "Newer <one>") && f.episodes[0].duration == 3723 &&
              !strcmp(f.episodes[0].url, "https://cdn.example.com/2.m4a"));
        CHECK(!strcmp(f.episodes[0].guid, f.episodes[0].url));  // No guid: its address.
        CHECK(!strcmp(f.episodes[1].guid, "ep-1") && f.episodes[1].duration == 45 * 60 + 30 &&
              f.episodes[1].bytes == 1234 && !strcmp(f.episodes[1].type, "audio/mpeg"));
        CHECK(f.episodes[1].published == 1704099600);  // 2024-01-01 09:00 UTC.
        CHECK(!strcmp(f.episodes[2].title, "Undated") && f.episodes[2].published == 0);
    }
    podcastFeedFree(&f);
    CHECK(f.episodes == NULL && f.count == 0);

    // Not a feed.
    CHECK(!podcastParseFeed("<html><body>Not found</body></html>", 35, &f));
    CHECK(!podcastParseFeed("", 0, &f) && !podcastParseFeed(NULL, 0, &f));
    const char *empty = "<rss><channel><title>Empty</title></channel></rss>";
    CHECK(podcastParseFeed(empty, strlen(empty), &f) && f.count == 0 && !strcmp(f.title, "Empty"));
    podcastFeedFree(&f);

    // A long feed keeps the newest PODCAST_EPISODES_MAX.
    size_t items = PODCAST_EPISODES_MAX + 20, size = items * 200 + 200;
    char *big = malloc(size);
    if (big) {
        size_t n = (size_t)snprintf(big, size, "<rss><channel><title>Long</title>");
        for (size_t i = 0; i < items; i++)
            n += (size_t)snprintf(big + n, size - n,
                                  "<item><title>E%zu</title><pubDate>%02zu Jan 2020 00:00:00 GMT</pubDate>"
                                  "<enclosure url=\"https://e.example/%zu.mp3\"/></item>",
                                  i, 1 + i % 28, i);
        n += (size_t)snprintf(big + n, size - n, "</channel></rss>");
        CHECK(podcastParseFeed(big, n, &f) && f.count == PODCAST_EPISODES_MAX);
        for (size_t i = 1; i < f.count; i++)
            if (f.episodes[i].published > f.episodes[i - 1].published) {
                CHECK(!"newest first");
                break;
            }
        podcastFeedFree(&f);
        free(big);
    }
}

static void testSearch(void)
{
    char url[512];
    CHECK(podcastSearchUrl("caf\xC3\xA9 & radio", url, sizeof(url)));
    CHECK(!strcmp(url, "https://itunes.apple.com/search?media=podcast&entity=podcast&limit=50&term="
                       "caf%C3%A9%20%26%20radio"));
    CHECK(!podcastSearchUrl("   ", url, sizeof(url)) && !podcastSearchUrl("x", url, 20));

    const char *answer =
        "{\"resultCount\":3,\"results\":["
        "{\"collectionName\":\"Made-up Talk\",\"artistName\":\"Kai\",\"feedUrl\":\"https://feeds.example/talk\","
        "\"artworkUrl100\":\"https://img.example/100.jpg\",\"artworkUrl600\":\"https://img.example/600.jpg\","
        "\"primaryGenreName\":\"Comedy\",\"trackCount\":120},"
        "{\"collectionName\":\"No feed\",\"artistName\":\"Kai\"},"
        "{\"collectionName\":\"Odd feed\",\"feedUrl\":\"file:///etc/passwd\"}]}";
    PodcastResults r;
    CHECK(podcastParseSearch(answer, &r) && r.count == 1);
    if (r.count == 1)
        CHECK(!strcmp(r.items[0].title, "Made-up Talk") && !strcmp(r.items[0].feed, "https://feeds.example/talk") &&
              !strcmp(r.items[0].image, "https://img.example/600.jpg") && r.items[0].episodes == 120 &&
              !strcmp(r.items[0].genre, "Comedy"));
    podcastResultsFree(&r);
    CHECK(podcastParseSearch("{\"resultCount\":0,\"results\":[]}", &r) && r.count == 0);
    podcastResultsFree(&r);
    CHECK(!podcastParseSearch("<html>rate limited</html>", &r) && !podcastParseSearch(NULL, &r));
}

static void testCharts(void)
{
    char url[512];
    CHECK(podcastChartUrl("mx", 1488, url, sizeof(url)) &&
          !strcmp(url, "https://itunes.apple.com/mx/rss/toppodcasts/limit=50/genre=1488/json"));
    CHECK(podcastChartUrl("us", 0, url, sizeof(url)) &&
          !strcmp(url, "https://itunes.apple.com/us/rss/toppodcasts/limit=50/json"));
    CHECK(!podcastChartUrl("MX", 0, url, sizeof(url)) && !podcastChartUrl("m/", 0, url, sizeof(url)) &&
          !podcastChartUrl("mex", 0, url, sizeof(url)) && !podcastChartUrl(NULL, 0, url, sizeof(url)));

    const char *chart = "{\"feed\":{\"entry\":["
                        "{\"im:name\":{\"label\":\"A\"},\"id\":{\"attributes\":{\"im:id\":\"1539248506\"}}},"
                        "{\"id\":{\"attributes\":{\"im:id\":\"x12\"}}},"
                        "{\"id\":{\"attributes\":{\"im:id\":\"1517954480\"}}},"
                        "{\"id\":{\"attributes\":{\"im:id\":\"1539248506\"}}},"
                        "{\"id\":{\"attributes\":{\"im:id\":\"1580419124\"}}}]}}";
    long long ids[8];
    CHECK(podcastParseChartIds(chart, ids, 8) == 3 && ids[0] == 1539248506LL && ids[1] == 1517954480LL &&
          ids[2] == 1580419124LL);
    CHECK(podcastParseChartIds(chart, ids, 2) == 2);
    CHECK(podcastParseChartIds("{\"feed\":{\"entry\":{\"id\":{\"attributes\":{\"im:id\":\"7\"}}}}}", ids, 8) == 1 &&
          ids[0] == 7);  // A chart of one.
    CHECK(podcastParseChartIds("{\"feed\":{}}", ids, 8) == 0 && podcastParseChartIds("<html>", ids, 8) == 0);

    long long want[3] = {30, 10, 20};
    CHECK(podcastLookupUrl(want, 3, url, sizeof(url)) &&
          !strcmp(url, "https://itunes.apple.com/lookup?entity=podcast&id=30,10,20"));
    CHECK(!podcastLookupUrl(want, 0, url, sizeof(url)) && !podcastLookupUrl(want, 3, url, 40));

    const char *lookup = "{\"results\":["
                         "{\"collectionId\":10,\"collectionName\":\"Ten\",\"feedUrl\":\"https://f.example/10\"},"
                         "{\"collectionId\":40,\"collectionName\":\"Forty\",\"feedUrl\":\"https://f.example/40\"},"
                         "{\"collectionId\":30,\"collectionName\":\"Thirty\",\"feedUrl\":\"https://f.example/30\"}]}";
    PodcastResults r;
    CHECK(podcastParseSearch(lookup, &r) && r.count == 3 && r.items[0].id == 10);
    podcastOrderByIds(&r, want, 3);  // The chart's order; 20 wasn't found, 40 wasn't asked for.
    CHECK(r.count == 2 && r.items[0].id == 30 && r.items[1].id == 10);
    podcastResultsFree(&r);
}

static void testDurationsAndDates(void)
{
    CHECK(podcastParseDuration("1:02:03") == 3723 && podcastParseDuration("62:03") == 3723);
    CHECK(podcastParseDuration("3723") == 3723 && podcastParseDuration(" 3723.6 ") == 3723);
    CHECK(podcastParseDuration("") == 0 && podcastParseDuration("abc") == 0 && podcastParseDuration("-5") == 0);
    CHECK(podcastParseDuration("1:2:3:4") == 0 && podcastParseDuration(NULL) == 0);

    CHECK(podcastParseDate("Wed, 02 Oct 2002 13:00:00 GMT") == 1033563600);
    CHECK(podcastParseDate("Wed, 02 Oct 2002 15:00:00 +0200") == 1033563600);
    CHECK(podcastParseDate("Wed, 02 Oct 2002 08:00:00 EST") == 1033563600);
    CHECK(podcastParseDate("2 Oct 2002 13:00 GMT") == 1033563600);  // No day name, no seconds.
    CHECK(podcastParseDate("Wed, 02 Oct 02 13:00:00 GMT") == 1033563600);  // Two-digit year.
    CHECK(podcastParseDate("2002-10-02T13:00:00Z") == 1033563600);
    CHECK(podcastParseDate("2002-10-02T15:00:00+02:00") == 1033563600);
    CHECK(podcastParseDate("yesterday") == 0 && podcastParseDate("") == 0 && podcastParseDate(NULL) == 0);
    CHECK(podcastParseDate("Wed, 32 Oct 2002 13:00:00 GMT") == 0);
}

// Damaged and cut-short copies of a feed: read without a memory error (the
// sanitizers watch), whatever they give.
static void testFeedFuzz(void)
{
    size_t length = strlen(FEED);
    char *copy = malloc(length);
    if (!copy)
        return;
    static const char SPECIAL[] = "<>/&;\"'=![]-?#x";
    srand(7);
    int parsed = 0;
    for (int round = 0; round < 3000; round++) {
        memcpy(copy, FEED, length);
        int flips = 1 + rand() % 8;
        for (int i = 0; i < flips; i++)
            copy[rand() % length] = rand() % 2 ? SPECIAL[rand() % (sizeof(SPECIAL) - 1)] : (char)(rand() % 256);
        size_t cut = round % 3 == 0 ? (size_t)rand() % length : length;
        Feed f;
        if (podcastParseFeed(copy, cut, &f)) {
            parsed++;
            CHECK(f.count <= PODCAST_EPISODES_MAX);
            podcastFeedFree(&f);
        }
    }
    CHECK(parsed > 0);
    free(copy);
}

// The fetcher's thread against the fake network (fake_net.c).
static void testFetch(void)
{
    fakeNetReset();
    fakeNetAnswer("itunes.apple.com/search",
                  "{\"results\":[{\"collectionName\":\"Made-up Talk\",\"feedUrl\":\"https://feeds.example/a\"}]}", 200);
    fakeNetAnswer("feeds.example/a", FEED, 200);
    fakeNetAnswer("feeds.example/html", "<html><body>Moved</body></html>", 200);
    CHECK(podFetchInit());
    podFetchSearch("made up");
    PodSearchStatus st;
    for (int i = 0; i < 500; i++) {
        podFetchSearchStatus(&st);
        if (st.state != POD_SEARCH_LOADING)
            break;
        usleep(2000);
    }
    PodcastResults found;
    CHECK(st.state == POD_SEARCH_READY && !podFetchTakeSearch(st.generation + 1, &found) &&
          podFetchTakeSearch(st.generation, &found) && found.count == 1 &&
          !strcmp(found.items[0].feed, "https://feeds.example/a"));
    if (st.state == POD_SEARCH_READY)
        podcastResultsFree(&found);

    // A chart: its ids, then the lookup, in the chart's order.
    fakeNetAnswer("itunes.apple.com/mx/rss/toppodcasts",
                  "{\"feed\":{\"entry\":[{\"id\":{\"attributes\":{\"im:id\":\"22\"}}},"
                  "{\"id\":{\"attributes\":{\"im:id\":\"11\"}}}]}}", 200);
    fakeNetAnswer("itunes.apple.com/lookup",
                  "{\"results\":[{\"collectionId\":11,\"collectionName\":\"Eleven\",\"feedUrl\":\"https://f.example/11\"},"
                  "{\"collectionId\":22,\"collectionName\":\"Twenty-two\",\"feedUrl\":\"https://f.example/22\"}]}", 200);
    podFetchChart("mx", 1488);
    for (int i = 0; i < 500; i++) {
        podFetchSearchStatus(&st);
        if (st.state != POD_SEARCH_LOADING)
            break;
        usleep(2000);
    }
    CHECK(st.state == POD_SEARCH_READY && podFetchTakeSearch(st.generation, &found) && found.count == 2 &&
          found.items[0].id == 22 && !strcmp(found.items[1].title, "Eleven"));
    if (st.state == POD_SEARCH_READY)
        podcastResultsFree(&found);

    podFetchFeed("https://feeds.example/a");
    podFetchFeed("https://feeds.example/a");  // Asked again before it's read: read once.
    podFetchFeed("https://feeds.example/html");
    char url[LINK_URL_MAX], error[256];
    Feed f;
    int good = 0, bad = 0, taken = 0;
    for (int i = 0; i < 1000 && (podFetchFeedsPending() > 0 || taken < 2); i++) {
        if (podFetchTakeFeed(url, sizeof(url), &f, error, sizeof(error))) {
            taken++;
            if (!strcmp(url, "https://feeds.example/a") && !error[0] && f.count == 3)
                good++;
            if (!strcmp(url, "https://feeds.example/html") && error[0] && f.count == 0)
                bad++;
            podcastFeedFree(&f);
        } else {
            usleep(2000);
        }
    }
    CHECK(good == 1 && bad == 1 && taken == 2 && podFetchFeedsPending() == 0);
    podFetchExit();
    fakeNetReset();
}

void testPodcast(void)
{
    testFetch();
    testFeedFuzz();
    testXml();
    testFeed();
    testSearch();
    testCharts();
    testDurationsAndDates();
}
