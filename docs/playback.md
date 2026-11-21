# Playback

[Back to the README](../README.md)

## Now Playing

Now Playing shows:

- the station's logo,
- the song playing, when the station sends it,
- the format: codec, bitrate and sample rate.

## Streams it plays

- **MP3**, **AAC** and **HE-AAC**, **Ogg Vorbis**, **Opus** and **FLAC**
  streams over HTTP and HTTPS.
- It follows `.pls` and `.m3u` playlists to the stream.
- **HLS** stations, with MPEG-TS, AAC or fMP4 segments (encrypted ones aren't
  supported).
- It reconnects on its own when the connection drops.

## When a station stutters

While a station plays, KaiRadio writes how the audio output keeps up to
`audio.log` in its folder (see
[Where KaiRadio keeps its files](settings.md#where-kairadio-keeps-its-files)).
It helps when a station stutters.
