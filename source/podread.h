// An episode's audio file over the network, read like a file that can seek:
// a thread downloads ahead into memory, and a seek outside what it holds
// starts a new request from there (an HTTP range). The decoder reads through
// it, so FFmpeg can find an MP3's or an M4A's length and jump in it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PODREAD_BUFFER (4u << 20)     // Held ahead of (and a little behind) the reader.
#define PODREAD_KEEP_BACK (512u << 10)  // Kept behind it, for FFmpeg's small jumps back.
#define PODREAD_REACH (1u << 20)      // A seek this far past what came waits instead of asking again.
#define PODREAD_RETRIES 3             // A dropped connection is picked up again this often.

typedef struct PodReader PodReader;

typedef struct {
    int64_t total;          // The file's size; -1 until known (or if never said).
    int64_t position;       // Where the reader is.
    int64_t ahead;          // Bytes downloaded past it.
    bool ended;             // All of it came.
    bool failed;            // See error.
    char content_type[64];
    char error[160];
} PodReaderInfo;

// Starts reading url. NULL if memory runs out.
PodReader *podReaderOpen(const char *url);
void podReaderClose(PodReader *r);

// Up to size bytes at the reader's position. Waits for them; returns how
// many came, 0 at the end of the file, -1 when it failed, -2 if abort became
// true while waiting.
int podReaderRead(PodReader *r, void *buf, int size, const volatile bool *abort);
// Moves the reader to offset (kept within the file when its size is known).
// Returns where it is.
int64_t podReaderSeek(PodReader *r, int64_t offset);
// The file's size, or -1 if not known.
int64_t podReaderSize(PodReader *r);
void podReaderInfo(PodReader *r, PodReaderInfo *out);
