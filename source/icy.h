// ICY metadata (SHOUTcast/Icecast): when a client asks with "Icy-MetaData: 1",
// the server answers with "icy-metaint: N" and puts a metadata block after
// every N bytes of audio. A block is one length byte (times 16) and that many
// bytes of text such as "StreamTitle='Artist - Song';". Pure logic.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ICY_BLOCK_MAX (255 * 16)

typedef void (*IcyBlockFn)(const char *block, void *user);

typedef struct {
    size_t metaint;     // Audio bytes between blocks; 0 when the stream has none.
    size_t audio_left;  // Audio bytes before the next length byte.
    size_t meta_left;   // Bytes of the current block still to come.
    size_t meta_len;
    bool in_meta;
    char meta[ICY_BLOCK_MAX + 1];
} IcyParser;

void icyInit(IcyParser *p, size_t metaint);

// Splits data into audio, copied to out (room for size bytes), and metadata
// blocks, given to on_block as they complete (empty ones are skipped).
// Returns the audio bytes written.
size_t icyFeed(IcyParser *p, const uint8_t *data, size_t size, uint8_t *out, IcyBlockFn on_block, void *user);

// The StreamTitle of a block, as UTF-8 (Latin-1 text is converted). False if
// the block has no title or it is empty.
bool icyStreamTitle(const char *block, char *out, size_t size);

// Copies text as UTF-8: valid UTF-8 as it is, anything else read as Latin-1.
// Control characters become spaces. Always terminates out.
void icyToUtf8(const char *text, size_t len, char *out, size_t size);
