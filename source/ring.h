// A byte ring between the network thread (writer) and the decoder (reader).
// The writer blocks while it is full; the reader waits for data until the
// writer ends the stream or the ring is cancelled.
#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t head;   // Next byte to read.
    size_t fill;
    bool ended;    // The writer has finished: no more data will come.
    bool cancelled;
    pthread_mutex_t lock;
    pthread_cond_t changed;
} Ring;

bool ringInit(Ring *r, size_t capacity);
void ringFree(Ring *r);

// Empties the ring and clears ended and cancelled, for a new stream.
void ringReset(Ring *r);

// Writes all of data, waiting for room. False if the ring was cancelled.
bool ringWrite(Ring *r, const uint8_t *data, size_t size);
// Reads up to size bytes, waiting up to timeout_ms for at least one.
// Returns the bytes read; 0 on timeout, or when the stream ended and the
// ring is empty, or when cancelled (see ringFinished).
size_t ringRead(Ring *r, uint8_t *out, size_t size, unsigned timeout_ms);

// No more data will be written.
void ringEnd(Ring *r);
// Wakes both sides; writes and reads fail until ringReset.
void ringCancel(Ring *r);

// True once nothing more can be read: ended and empty, or cancelled.
bool ringFinished(Ring *r);
size_t ringFill(Ring *r);
