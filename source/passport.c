#include "passport.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define FILE_MAX (1u << 20)
#define FILE_NAME "passport.txt"
#define LINE_MAX_LEN (3 + 21 + STATION_UUID_MAX + 4)

// The code in capitals, if it's two letters. False otherwise.
static bool countryCode(const char *code, char out[3])
{
    if (!code || !isalpha((unsigned char)code[0]) || !isalpha((unsigned char)code[1]) || code[2])
        return false;
    out[0] = (char)toupper((unsigned char)code[0]);
    out[1] = (char)toupper((unsigned char)code[1]);
    out[2] = '\0';
    return true;
}

// A station id as Radio Browser gives them: letters, digits and dashes.
static bool validUuid(const char *uuid)
{
    size_t n = strlen(uuid);
    if (n >= STATION_UUID_MAX)
        return false;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)uuid[i]) && uuid[i] != '-')
            return false;
    return true;
}

static long findStamp(const Passport *p, const char *code, const char *uuid)
{
    for (size_t i = 0; i < p->count; i++)
        if (uuid[0] ? strcmp(p->stamps[i].uuid, uuid) == 0
                    : !p->stamps[i].uuid[0] && strcmp(p->stamps[i].code, code) == 0)
            return (long)i;
    return -1;
}

static bool append(Passport *p, const char code[3], const char *uuid, int64_t first)
{
    if (p->count >= PASSPORT_MAX)
        return false;
    if ((p->count & (p->count - 1)) == 0) {  // 0, 1, 2, 4...: room for twice as many.
        size_t room = p->count ? p->count * 2 : 16;
        PassportStamp *more = realloc(p->stamps, room * sizeof(*more));
        if (!more)
            return false;
        p->stamps = more;
    }
    PassportStamp *s = &p->stamps[p->count++];
    memcpy(s->code, code, 3);
    snprintf(s->uuid, sizeof(s->uuid), "%s", uuid);
    s->first = first;
    return true;
}

bool passportParse(Passport *p, const char *text)
{
    while (text && *text) {
        const char *end = strchr(text, '\n');
        size_t len = end ? (size_t)(end - text) : strlen(text);
        char line[LINE_MAX_LEN + 1], code_in[3] = "", uuid[STATION_UUID_MAX] = "", code[3];
        long long first = 0;
        if (len <= LINE_MAX_LEN) {
            memcpy(line, text, len);
            line[len] = '\0';
            line[strcspn(line, "\r")] = '\0';
            int got = line[0] == '#' ? 0 : sscanf(line, "%2s %lld %39s", code_in, &first, uuid);
            if (got >= 2 && first >= 0 && countryCode(code_in, code) && validUuid(uuid) &&
                findStamp(p, code, uuid) < 0 && !append(p, code, uuid, first) && p->count < PASSPORT_MAX)
                return false;  // Out of memory.
        }
        text = end ? end + 1 : NULL;
    }
    return true;
}

char *passportToText(const Passport *p)
{
    static const char HEAD[] = "# KaiRadio's passport: country, first heard (UTC seconds), station id\n";
    size_t size = sizeof(HEAD) + p->count * (LINE_MAX_LEN + 1), used = 0;
    char *text = malloc(size);
    if (!text)
        return NULL;
    used += (size_t)snprintf(text, size, "%s", HEAD);
    for (size_t i = 0; i < p->count; i++) {
        const PassportStamp *s = &p->stamps[i];
        used += (size_t)snprintf(text + used, size - used, "%s %" PRId64 "%s%s\n", s->code, s->first,
                                 s->uuid[0] ? " " : "", s->uuid);
    }
    return text;
}

bool passportHasCountry(const Passport *p, const char *code)
{
    char want[3];
    if (!countryCode(code, want))
        return false;
    for (size_t i = 0; i < p->count; i++)
        if (strcmp(p->stamps[i].code, want) == 0)
            return true;
    return false;
}

bool passportStamp(Passport *p, const Station *station, int64_t utc)
{
    char code[3];
    if (!countryCode(station->countrycode, code))
        return false;
    // Without a usable id, only its country is stamped.
    const char *uuid = validUuid(station->uuid) ? station->uuid : "";
    if (findStamp(p, code, uuid) >= 0 || (!uuid[0] && passportHasCountry(p, code)))
        return false;
    return append(p, code, uuid, utc);
}

size_t passportCountries(const Passport *p, PassportCountry *out, size_t max)
{
    size_t n = 0;
    PassportCountry *seen = malloc((p->count ? p->count : 1) * sizeof(*seen));
    if (!seen)
        return 0;
    for (size_t i = 0; i < p->count; i++) {
        const PassportStamp *s = &p->stamps[i];
        size_t k = 0;
        while (k < n && strcmp(seen[k].code, s->code) != 0)
            k++;
        if (k == n) {
            memcpy(seen[n].code, s->code, 3);
            seen[n].stations = 0;
            seen[n].first = s->first;
            n++;
        }
        if (s->uuid[0])
            seen[k].stations++;
        if (s->first < seen[k].first)
            seen[k].first = s->first;
    }
    // In the order first heard (the stamps are, but an older file may not be).
    for (size_t i = 1; i < n; i++)
        for (size_t j = i; j > 0 && seen[j].first < seen[j - 1].first; j--) {
            PassportCountry t = seen[j];
            seen[j] = seen[j - 1];
            seen[j - 1] = t;
        }
    for (size_t i = 0; i < n && i < max; i++)
        out[i] = seen[i];
    free(seen);
    return n;
}

size_t passportStations(const Passport *p)
{
    size_t n = 0;
    for (size_t i = 0; i < p->count; i++)
        n += p->stamps[i].uuid[0] != '\0';
    return n;
}

void passportLoad(Passport *p, const char *folder)
{
    memset(p, 0, sizeof(*p));
    snprintf(p->folder, sizeof(p->folder), "%s", folder);
    char path[300];
    snprintf(path, sizeof(path), "%s/" FILE_NAME, folder);
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    char *text = malloc(FILE_MAX + 1);
    size_t got = text ? fread(text, 1, FILE_MAX, f) : 0;
    fclose(f);
    if (text) {
        text[got] = '\0';
        passportParse(p, text);
    }
    free(text);
}

// Writes to a temporary file first, so a failed write keeps the old one.
static void save(const Passport *p)
{
    char *text = p->folder[0] ? passportToText(p) : NULL;
    if (!text)
        return;
    mkdir(p->folder, 0777);
    char path[300], temp[308];
    snprintf(path, sizeof(path), "%s/" FILE_NAME, p->folder);
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    FILE *f = fopen(temp, "wb");
    bool ok = f && fputs(text, f) >= 0;
    if (f)
        ok = fclose(f) == 0 && ok;
    if (ok) {
        remove(path);
        ok = rename(temp, path) == 0;
    }
    if (!ok)
        remove(temp);
    free(text);
}

bool passportPlayed(Passport *p, const Station *station, int64_t utc)
{
    bool new_country = !passportHasCountry(p, station->countrycode);
    if (!passportStamp(p, station, utc))
        return false;
    save(p);
    return new_country;
}

void passportFree(Passport *p)
{
    free(p->stamps);
    p->stamps = NULL;
    p->count = 0;
}
