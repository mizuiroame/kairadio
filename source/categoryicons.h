// What a genre or language tile shows: an icon for a genre, by the words in
// its tag, and a few letters in a language's own script. Pure logic; the
// icons themselves are in icons.h.
#pragma once

#include <stddef.h>

// The icon of a genre tag ("classic rock" -> "guitar-electric"), or NULL
// for a tag none of the rules knows (the tile then shows a note).
const char *genreIcon(const char *tag);

// The rules' icons, for the checks: the i-th, or NULL past the last.
const char *genreIconAt(size_t i);

// Letters in a language's own script ("es" -> "Ñ", "ja" -> "あ"), by its
// ISO 639-1 code or, without one, its name ("spanish"). NULL if unknown.
const char *languageSample(const char *code, const char *name);
