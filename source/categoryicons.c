#include "categoryicons.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>

// An icon (tools/make_icons.py reads these names) and the words that pick it,
// "|" between them. The first rule with a word in the tag wins, so the
// narrower ones come first ("k-pop" before "pop", "dancehall" before "dance").
static const struct {
    const char *icon, *words;
} GENRES[] = {
    {"skull", "metal|punk|hardcore|grunge"},
    {"sparkles", "anime|j-pop|jpop|k-pop|kpop|vocaloid|idol"},
    {"mic-vocal", "hip hop|hip-hop|hiphop|rap|r&b|rnb|urban"},
    {"piano", "classical|baroque|opera|orchestra|symphon|chamber|piano"},
    {"disc-3", "jazz|blues|soul|funk|swing|motown|disco"},
    {"guitar-electric", "rock|indie|alternative"},
    {"sun", "reggae|ska|dancehall|afrobeat|calypso|caribbean"},
    {"audio-waveform", "electro|dance|house|techno|trance|edm|dubstep|drum and bass|dnb|club|synth"},
    {"cassette-tape", "oldies|retro|classic hits|50s|60s|70s|80s|90s|00s|decades"},
    {"moon", "chill|lofi|lo-fi|ambient|relax|sleep|meditation|downtempo|new age"},
    {"wine", "lounge|easy listening|smooth|adult contemporary"},
    {"tree-palm", "latin|salsa|cumbia|reggaeton|bachata|merengue|tropical|bossa|samba|tango|mariachi|ranchera|"
                  "regional mexican|banda|bolero|flamenco|forro|sertanejo|vallenato"},
    {"newspaper", "news|information|politic|public radio"},
    {"messages-square", "talk|podcast|spoken|interview"},
    {"trophy", "sport|football|soccer|baseball|basketball|cricket|hockey"},
    {"book-open", "islam|quran|koran|muslim"},
    {"church", "christian|gospel|worship|catholic|religio|bible|praise|church"},
    {"baby", "kids|children|child|nursery"},
    {"drama", "comedy|theater|theatre|humor|humour"},
    {"clapperboard", "soundtrack|film|movie|cinema"},
    {"heart", "love|romantic|ballad"},
    {"wheat", "country|folk|americana|bluegrass|western"},
    {"globe", "world|international|ethnic|traditional|multicultural"},
    {"star", "pop|hits|top 40|top40|chart"},
    {"radio-tower", "community|local|variety|college|university|student|fm"},
    {"headphones", "music"},
};
#define GENRE_RULES (sizeof(GENRES) / sizeof(GENRES[0]))

// True if one of words ("a|b|c") is in text.
static bool hasWord(const char *text, const char *words)
{
    while (*words) {
        const char *end = strchr(words, '|');
        size_t len = end ? (size_t)(end - words) : strlen(words);
        char word[32];
        if (len < sizeof(word)) {
            memcpy(word, words, len);
            word[len] = '\0';
            if (strstr(text, word))
                return true;
        }
        words += len + (end ? 1 : 0);
    }
    return false;
}

const char *genreIcon(const char *tag)
{
    if (!tag)
        return NULL;
    char lower[128];
    size_t n = 0;
    for (; tag[n] && n + 1 < sizeof(lower); n++)
        lower[n] = (char)tolower((unsigned char)tag[n]);
    lower[n] = '\0';
    for (size_t i = 0; i < GENRE_RULES; i++)
        if (hasWord(lower, GENRES[i].words))
            return GENRES[i].icon;
    return NULL;
}

const char *genreIconAt(size_t i)
{
    return i < GENRE_RULES ? GENRES[i].icon : NULL;
}

// Letters only that language writes (or that say it at a glance), in UTF-8.
static const struct {
    const char *code, *name, *sample;
} LANGUAGES[] = {
    {"en", "english", "Aa"},
    {"es", "spanish", "\xC3\x91"},             // Ñ
    {"pt", "portuguese", "\xC3\x83"},          // Ã
    {"fr", "french", "\xC3\x87"},              // Ç
    {"de", "german", "\xC3\x9F"},              // ß
    {"it", "italian", "\xC3\x88"},             // È
    {"nl", "dutch", "IJ"},
    {"pl", "polish", "\xC5\x81"},              // Ł
    {"cs", "czech", "\xC5\x98"},               // Ř
    {"sk", "slovak", "\xC4\xBD"},              // Ľ
    {"sl", "slovenian", "\xC4\x8C"},           // Č
    {"hr", "croatian", "\xC4\x90"},            // Đ
    {"bs", "bosnian", "\xC4\x86"},             // Ć
    {"sr", "serbian", "\xD0\x82"},             // Ђ
    {"bg", "bulgarian", "\xD0\xAA"},           // Ъ
    {"ru", "russian", "\xD0\xAF"},             // Я
    {"uk", "ukrainian", "\xD0\x87"},           // Ї
    {"be", "belarusian", "\xD0\x8E"},          // Ў
    {"mk", "macedonian", "\xD0\x83"},          // Ѓ
    {"el", "greek", "\xCE\xA9"},               // Ω
    {"tr", "turkish", "\xC4\x9E"},             // Ğ
    {"ro", "romanian", "\xC8\x98"},            // Ș
    {"hu", "hungarian", "\xC5\x90"},           // Ő
    {"sv", "swedish", "\xC3\x85"},             // Å
    {"no", "norwegian", "\xC3\x98"},           // Ø
    {"nb", "norwegian bokmal", "\xC3\x98"},    // Ø
    {"da", "danish", "\xC3\x86"},              // Æ
    {"fi", "finnish", "\xC3\x84"},             // Ä
    {"is", "icelandic", "\xC3\x9E"},           // Þ
    {"et", "estonian", "\xC3\x95"},            // Õ
    {"lv", "latvian", "\xC4\x80"},             // Ā
    {"lt", "lithuanian", "\xC4\x96"},          // Ė
    {"sq", "albanian", "\xC3\x8B"},            // Ë
    {"cy", "welsh", "\xC5\xB4"},               // Ŵ
    {"ga", "irish", "\xC3\x81"},               // Á
    {"ca", "catalan", "\xC4\xBF"},             // Ŀ
    {"eo", "esperanto", "\xC4\x88"},           // Ĉ
    {"mt", "maltese", "\xC4\xA6"},             // Ħ
    {"vi", "vietnamese", "\xC6\xA0"},          // Ơ
    {"af", "afrikaans", "\xC3\x8A"},           // Ê
    {"az", "azerbaijani", "\xC6\x8F"},         // Ə
    {"kk", "kazakh", "\xD3\x98"},              // Ә
    {"mn", "mongolian", "\xD3\xA8"},           // Ө
    {"ka", "georgian", "\xE1\x83\x90"},        // ა
    {"hy", "armenian", "\xD4\xB1"},            // Ա
    {"ar", "arabic", "\xD8\xB9"},              // ع
    {"fa", "persian", "\xD9\xBE"},             // پ
    {"ur", "urdu", "\xDB\x92"},                // ے
    {"he", "hebrew", "\xD7\x90"},              // א
    {"hi", "hindi", "\xE0\xA4\x85"},           // अ
    {"bn", "bengali", "\xE0\xA6\x85"},         // অ
    {"ta", "tamil", "\xE0\xAE\xA4"},           // த
    {"th", "thai", "\xE0\xB8\x81"},            // ก
    {"ja", "japanese", "\xE3\x81\x82"},        // あ
    {"zh", "chinese", "\xE4\xB8\xAD"},         // 中
    {"ko", "korean", "\xED\x95\x9C"},          // 한
};

const char *languageSample(const char *code, const char *name)
{
    size_t count = sizeof(LANGUAGES) / sizeof(LANGUAGES[0]);
    for (size_t i = 0; code && code[0] && i < count; i++)
        if (strcasecmp(LANGUAGES[i].code, code) == 0)
            return LANGUAGES[i].sample;
    for (size_t i = 0; name && name[0] && i < count; i++)
        if (strcasecmp(LANGUAGES[i].name, name) == 0)
            return LANGUAGES[i].sample;
    return NULL;
}
