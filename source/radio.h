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

// A podcast episode to play (podcast.h has where these come from).
typedef struct {
    char feed[LINK_URL_MAX];   // The podcast it's from (its id).
    char guid[160];
    char title[160];
    char podcast[160];         // The podcast's name.
    char image[LINK_URL_MAX];  // Its picture; may be empty.
    char url[LINK_URL_MAX];    // The audio.
    char type[32];             // Its content type; may be empty.
    int start;                 // Seconds in to start at.
    int duration;              // As the feed says; 0 if unknown.
} RadioEpisode;

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
    char error[160];        // In English, as logged: shown with T().
    // An episode instead of a station (has_station is then false).
    bool has_episode;
    RadioEpisode episode;
    double position;        // Seconds in.
    double duration;        // Its length (the file's, else the feed's); 0 if unknown.
    bool paused;
    bool ended;             // It played to the end.
} RadioStatus;

// False if the audio output can't start.
// folder: where the audio log goes.
bool radioInit(const char *folder);
void radioExit(void);

void radioPlay(const Station *station);
// Plays an episode from episode->start. Stops a station or episode playing.
void radioPlayEpisode(const RadioEpisode *episode);
// For an episode: jumps to seconds in, and pauses or goes on.
void radioSeek(double seconds);
void radioPause(bool paused);
void radioStop(void);
void radioGetStatus(RadioStatus *out);

// A short label for a state: "Connecting…", "Buffering…"...
const char *radioStateLabel(RadioState state);
