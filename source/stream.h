// Receives a station's stream in a background thread and writes its audio
// bytes to a ring for the decoder. Follows .pls and .m3u files to the
// stream, plays HLS by fetching its segments in order, takes the song
// titles out of ICY streams, and reconnects when the connection drops.
#pragma once

#include <stdbool.h>

#include "ring.h"

typedef enum {
    STREAM_IDLE,
    STREAM_CONNECTING,
    STREAM_RECEIVING,
    STREAM_RECONNECTING,
    STREAM_FAILED,  // See error.
    STREAM_ENDED,   // The server finished the stream.
} StreamState;

typedef struct {
    StreamState state;
    bool hls;
    int bitrate;              // kbit/s, from icy-br or the HLS variant; 0 if unknown.
    char content_type[64];    // Of the audio ("audio/mpeg", "audio/aac", "video/mp2t"...).
    char title[256];          // The song now, from ICY metadata; may be empty.
    unsigned title_changes;   // Counts title changes.
    char error[160];
} StreamInfo;

bool streamInit(void);
void streamExit(void);

// Starts receiving url into ring (which it resets). Stops any stream before.
void streamStart(Ring *ring, const char *url);
// Stops receiving and cancels the ring. Waits for the thread.
void streamStop(void);
void streamGetInfo(StreamInfo *out);
