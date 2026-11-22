// GitHub's answer about the latest release, read for an update (updaterelease.h).
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "updaterelease.h"

// The shape of GitHub's answer (trimmed), with fields in its order.
#define RELEASE(tag, extra, asset_name, url, digest, size)                                                    \
    "{\"url\":\"https://api.github.com/repos/mizuiroame/kairadio/releases/1\",\"tag_name\":\"" tag "\","       \
    "\"name\":\"KaiRadio\"" extra ",\"draft\":false,\"prerelease\":false,\"assets\":["                        \
    "{\"name\":\"notes.txt\",\"size\":10,\"digest\":null,\"browser_download_url\":\"https://example.com/x\"}," \
    "{\"name\":\"" asset_name "\",\"content_type\":\"application/octet-stream\",\"size\":" size              \
    ",\"digest\":\"" digest "\",\"browser_download_url\":\"" url "\"}],\"body\":\"What's new...\"}"

#define URL "https://github.com/mizuiroame/kairadio/releases/download/v1.3.0/KaiRadio.nro"
#define DIGEST "sha256:682ba5d410544f2d9bc1558a5239cb8c94af874825528fd3862a06b60e3ada3f"

static void testGood(void)
{
    UpdateRelease r;
    CHECK(updateReadRelease(RELEASE("v1.3.0", "", "KaiRadio.nro", URL, DIGEST, "21000000"), "kairadio",
                            "KaiRadio.nro", &r));
    CHECK(strcmp(r.tag, "v1.3.0") == 0 && strcmp(r.url, URL) == 0 && r.size == 21000000);
    CHECK(strcmp(r.sha256, "682ba5d410544f2d9bc1558a5239cb8c94af874825528fd3862a06b60e3ada3f") == 0);
}

static void testRefused(void)
{
    UpdateRelease r;
    // Another host, another file's address, no digest, a pre-release, a draft, sizes out of bounds.
    CHECK(!updateReadRelease(RELEASE("v1.3.0", "", "KaiRadio.nro",
                                     "https://evil.example/KaiRadio.nro", DIGEST, "1000"),
                             "kairadio", "KaiRadio.nro", &r));
    CHECK(!updateReadRelease(RELEASE("v1.3.0", "", "KaiRadio.nro",
                                     "https://github.com/mizuiroame/kairadio/releases/download/v1.2.9/KaiRadio.nro",
                                     DIGEST, "1000"),
                             "kairadio", "KaiRadio.nro", &r));
    CHECK(!updateReadRelease(RELEASE("v1.3.0", "", "KaiRadio.nro", URL, "", "1000"), "kairadio", "KaiRadio.nro",
                             &r));
    CHECK(!updateReadRelease(RELEASE("v1.3.0", ",\"prerelease\":true", "KaiRadio.nro", URL, DIGEST, "1000"),
                             "kairadio", "KaiRadio.nro", &r));
    CHECK(!updateReadRelease(RELEASE("v1.3.0", ",\"draft\":true", "KaiRadio.nro", URL, DIGEST, "1000"), "kairadio",
                             "KaiRadio.nro", &r));
    CHECK(!updateReadRelease(RELEASE("v1.3.0", "", "KaiRadio.nro", URL, DIGEST, "0"), "kairadio", "KaiRadio.nro", &r));
    CHECK(!updateReadRelease(RELEASE("v1.3.0", "", "KaiRadio.nro", URL, DIGEST, "1e12"), "kairadio", "KaiRadio.nro",
                             &r));
    // No such file in it, a tag that isn't x.y.z, not JSON at all.
    CHECK(!updateReadRelease(RELEASE("v1.3.0", "", "Other.nro", URL, DIGEST, "1000"), "kairadio", "KaiRadio.nro", &r));
    CHECK(!updateReadRelease(RELEASE("nightly", "", "KaiRadio.nro", URL, DIGEST, "1000"), "kairadio", "KaiRadio.nro",
                             &r));
    CHECK(!updateReadRelease("<html>rate limited</html>", "kairadio", "KaiRadio.nro", &r));
    CHECK(!updateReadRelease(NULL, "kairadio", "KaiRadio.nro", &r));
    CHECK(r.tag[0] == '\0' && r.url[0] == '\0');  // Nothing kept from a refused answer.
}

void testUpdateRelease(void)
{
    testGood();
    testRefused();
}
