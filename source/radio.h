// Playback: the stream thread (stream.c) fills a ring; a thread here decodes
// it with FFmpeg, converts it to the console's output format and plays it
// with audout. The UI calls these from the main thread; they return at once.
#pragma once

#include <stdbool.h>

#include "stations.h"

typedef enum {
    RADIO_STOPPED,
    RADIO_CONNECTING,
    RADIO_BUFFERING,
    RADIO_PLAYING,
    RADIO_RECONNECTING,
    RADIO_FAILED,  // See error.
} RadioState;

typedef struct {
    RadioState state;
    bool has_station;
    Station station;        // The one playing, or that last played.
    char title[256];        // The song now, when the station sends it.
    unsigned title_changes;
    char codec[16];         // "MP3", "AAC", "HE-AAC", "Opus", "Vorbis", "FLAC"; empty until known.
    int bitrate;            // kbit/s; 0 if unknown.
    unsigned sample_rate;   // Of the stream; 0 until known.
    bool hls;
    float buffered_seconds; // Audio waiting to be played.
    char error[160];
} RadioStatus;

// False if the audio output can't start.
bool radioInit(void);
void radioExit(void);

void radioPlay(const Station *station);
void radioStop(void);
void radioGetStatus(RadioStatus *out);

// A short label for a state: "Connecting…", "Buffering…"...
const char *radioStateLabel(RadioState state);
