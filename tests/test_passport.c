// The listening passport: stamping stations and countries, the countries in
// the order first heard, and its file read back and kept on the card.
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "passport.h"

static Station station(const char *code, const char *uuid)
{
    Station s;
    memset(&s, 0, sizeof(s));
    snprintf(s.countrycode, sizeof(s.countrycode), "%s", code);
    snprintf(s.uuid, sizeof(s.uuid), "%s", uuid);
    return s;
}

static void testStamps(void)
{
    Passport p;
    memset(&p, 0, sizeof(p));
    Station jp = station("jp", "960e5d4b-0601-11e8-ae97-52543be04c81");
    CHECK(passportStamp(&p, &jp, 100));
    CHECK(!passportStamp(&p, &jp, 200));  // Heard again: nothing new.
    CHECK(passportHasCountry(&p, "JP") && passportHasCountry(&p, "jp") && !passportHasCountry(&p, "MX"));
    Station mx1 = station("MX", "aaaa-1"), mx2 = station("MX", "aaaa-2"), jp2 = station("JP", "bbbb-1");
    CHECK(passportStamp(&p, &mx1, 300) && passportStamp(&p, &mx2, 400) && passportStamp(&p, &jp2, 500));
    // A station without an id stamps its country only, once; one without a country nothing.
    Station br = station("BR", ""), none = station("", "cccc-1"), bad = station("1X", "cccc-2");
    CHECK(passportStamp(&p, &br, 600) && !passportStamp(&p, &br, 700));
    CHECK(!passportStamp(&p, &none, 800) && !passportStamp(&p, &bad, 800));
    Station mx_noid = station("MX", "");
    CHECK(!passportStamp(&p, &mx_noid, 900));  // Mexico is in it already.
    CHECK(passportStations(&p) == 4);

    PassportCountry c[8];
    CHECK(passportCountries(&p, c, 8) == 3);
    CHECK(strcmp(c[0].code, "JP") == 0 && c[0].stations == 2 && c[0].first == 100);
    CHECK(strcmp(c[1].code, "MX") == 0 && c[1].stations == 2 && c[1].first == 300);
    CHECK(strcmp(c[2].code, "BR") == 0 && c[2].stations == 0);
    CHECK(passportCountries(&p, c, 1) == 3 && strcmp(c[0].code, "JP") == 0);  // Only one fits: the rest counted.

    // Its text read back is the same passport.
    char *text = passportToText(&p);
    CHECK(text != NULL);
    Passport back;
    memset(&back, 0, sizeof(back));
    CHECK(passportParse(&back, text));
    CHECK(back.count == p.count && passportStations(&back) == 4 && passportHasCountry(&back, "BR"));
    free(text);
    passportFree(&back);
    passportFree(&p);
}

static void testDamaged(void)
{
    Passport p;
    memset(&p, 0, sizeof(p));
    // Comments, blank lines, Windows line ends, bad codes, ids or times, and repeats are left out.
    CHECK(passportParse(&p, "# a comment\r\n\r\nDE 50 dddd-1\r\nzz\nD1 5 x\nFR -4 eeee\nFR 7 bad/id\n"
                            "fr 60 ffff-1\nDE 70 dddd-1\nGB 80\nGB 90\n" "US 1"));
    CHECK(p.count == 4 && passportStations(&p) == 2);
    PassportCountry c[4];
    // Out of order in the file: still in the order first heard.
    CHECK(passportCountries(&p, c, 4) == 4 && strcmp(c[0].code, "US") == 0 && strcmp(c[1].code, "DE") == 0);
    passportFree(&p);
    CHECK(passportParse(&p, "") && p.count == 0);
    CHECK(passportParse(&p, NULL) && p.count == 0);
    passportFree(&p);
}

static void testCard(void)
{
    const char *folder = TMP_ROOT "/passport";
    system("rm -rf " TMP_ROOT "/passport");
    system("mkdir -p " TMP_ROOT);
    Passport p;
    passportLoad(&p, folder);  // No file yet: empty.
    CHECK(p.count == 0);
    Station it = station("IT", "1111-1"), it2 = station("IT", "1111-2");
    CHECK(passportPlayed(&p, &it, 10));    // A new country,
    CHECK(!passportPlayed(&p, &it2, 20));  // a new station of one heard,
    CHECK(!passportPlayed(&p, &it2, 30));  // and one heard.
    passportFree(&p);
    passportLoad(&p, folder);  // Kept on the card.
    CHECK(p.count == 2 && passportStations(&p) == 2 && passportHasCountry(&p, "IT"));
    passportFree(&p);
}

void testPassport(void)
{
    testStamps();
    testDamaged();
    testCard();
}
