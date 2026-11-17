#include "icy.h"

#include <string.h>

void icyInit(IcyParser *p, size_t metaint)
{
    memset(p, 0, sizeof(*p));
    p->metaint = metaint;
    p->audio_left = metaint;
}

static void finishBlock(IcyParser *p, IcyBlockFn on_block, void *user)
{
    p->meta[p->meta_len] = '\0';
    p->in_meta = false;
    p->audio_left = p->metaint;
    if (p->meta_len > 0 && on_block)
        on_block(p->meta, user);
}

size_t icyFeed(IcyParser *p, const uint8_t *data, size_t size, uint8_t *out, IcyBlockFn on_block, void *user)
{
    if (p->metaint == 0) {
        memcpy(out, data, size);
        return size;
    }
    size_t written = 0;
    while (size > 0) {
        if (p->in_meta) {
            size_t n = size < p->meta_left ? size : p->meta_left;
            memcpy(p->meta + p->meta_len, data, n);
            p->meta_len += n;
            p->meta_left -= n;
            data += n;
            size -= n;
            if (p->meta_left == 0)
                finishBlock(p, on_block, user);
        } else if (p->audio_left == 0) {
            // The length byte.
            p->meta_left = (size_t)data[0] * 16;
            p->meta_len = 0;
            p->in_meta = true;
            data++;
            size--;
            if (p->meta_left == 0)
                finishBlock(p, on_block, user);
        } else {
            size_t n = size < p->audio_left ? size : p->audio_left;
            memcpy(out + written, data, n);
            written += n;
            p->audio_left -= n;
            data += n;
            size -= n;
        }
    }
    return written;
}

// Length of a valid UTF-8 sequence at s (at most len bytes), or 0.
static size_t utf8Sequence(const unsigned char *s, size_t len)
{
    size_t n;
    if (s[0] < 0x80)
        return 1;
    if ((s[0] & 0xE0) == 0xC0 && s[0] >= 0xC2)
        n = 2;
    else if ((s[0] & 0xF0) == 0xE0)
        n = 3;
    else if ((s[0] & 0xF8) == 0xF0 && s[0] <= 0xF4)
        n = 4;
    else
        return 0;
    if (n > len)
        return 0;
    for (size_t i = 1; i < n; i++)
        if ((s[i] & 0xC0) != 0x80)
            return 0;
    return n;
}

static bool isUtf8(const unsigned char *s, size_t len)
{
    for (size_t i = 0; i < len;) {
        size_t n = utf8Sequence(s + i, len - i);
        if (n == 0)
            return false;
        i += n;
    }
    return true;
}

void icyToUtf8(const char *text, size_t len, char *out, size_t size)
{
    if (size == 0)
        return;
    const unsigned char *s = (const unsigned char *)text;
    bool utf8 = isUtf8(s, len);
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned c = s[i];
        if (c < 0x20 || c == 0x7F) {
            if (o + 1 >= size)
                break;
            out[o++] = ' ';
        } else if (c < 0x80) {
            if (o + 1 >= size)
                break;
            out[o++] = (char)c;
        } else if (utf8) {
            size_t n = utf8Sequence(s + i, len - i);
            if (o + n >= size)
                break;
            memcpy(out + o, s + i, n);
            o += n;
            i += n - 1;
        } else {
            // Latin-1: one byte is one code point below 256.
            if (o + 2 >= size)
                break;
            out[o++] = (char)(0xC0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[o] = '\0';
}

bool icyStreamTitle(const char *block, char *out, size_t size)
{
    static const char KEY[] = "StreamTitle='";
    const char *start = strstr(block, KEY);
    if (!start)
        return false;
    start += sizeof(KEY) - 1;
    // The value ends at "';": a title may itself contain a quote.
    const char *end = strstr(start, "';");
    if (!end)
        end = strrchr(start, '\'');
    if (!end)
        end = start + strlen(start);
    icyToUtf8(start, (size_t)(end - start), out, size);
    // Trim spaces; a title of only spaces or " - " counts as none.
    size_t len = strlen(out);
    while (len > 0 && out[len - 1] == ' ')
        out[--len] = '\0';
    size_t lead = strspn(out, " ");
    memmove(out, out + lead, len - lead + 1);
    return out[0] != '\0' && strcmp(out, "-") != 0;
}
