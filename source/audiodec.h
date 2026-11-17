// Decodes the stream in a ring with FFmpeg into 16-bit stereo at the output
// rate. One decoder at a time. Used by the audio thread (radio.c), and on
// the host by the stream test tool.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ring.h"

typedef struct {
    char codec[16];        // "MP3", "AAC", "HE-AAC", "Opus", "Vorbis", "FLAC".
    unsigned sample_rate;  // Of the stream.
    unsigned channels;
} DecoderInfo;

// Opens the stream in ring (whose start is buffered): content_type is a hint
// for the format and may be empty. Reads give up when *abort becomes true.
// After an underrun, reading waits until rebuffer bytes are in the ring.
bool decoderOpen(Ring *ring, const char *content_type, unsigned out_rate, const volatile bool *abort,
                 size_t rebuffer);
void decoderClose(void);

// Decodes up to frames stereo frames into out. Returns the frames written;
// fewer than asked only at the end of the stream, on an error, or when
// aborted.
size_t decoderRead(int16_t *out, size_t frames);

void decoderGetInfo(DecoderInfo *out);
// True while reading waits for the ring to refill.
bool decoderBuffering(void);
