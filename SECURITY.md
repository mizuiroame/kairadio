# Security policy

## Supported versions

Only the [latest release](https://github.com/mizuiroame/kairadio/releases/latest)
gets security fixes. Please check that a problem still happens with it before
reporting.

## Reporting a vulnerability

Please don't open a public issue for a security problem. Report it privately
instead: on this repository's **Security** tab, choose **Report a
vulnerability**. Say which version you used, what you did and what happened,
and, if you can, the station (its name or Radio Browser id) or the playlist,
stream or answer that shows it.

KaiRadio is a hobby project kept by one person. Reports are read and answered
as time allows; once a fix is released, the report can be made public, and
you're credited in the release notes unless you'd rather not be.

## Scope

In scope: KaiRadio's own code and how it uses its libraries. For instance, a
station's stream, playlist (`.pls`, `.m3u`, HLS), logo or song titles, or an
answer from Radio Browser or Open-Meteo, that can crash KaiRadio or make it run
code, read or write files it shouldn't, or reach places on your network it
shouldn't.

Out of scope: kaiHEN, the console's system software, the stations and
services themselves, and bugs that belong to a bundled library alone (please
report those upstream too; KaiRadio updates them when there's a fix).

## How KaiRadio handles your data

Under kaiHEN, homebrew runs with broad system access, so a bug in an app that
reads untrusted data could affect the whole console. KaiRadio keeps what it
touches small:

- **What it connects to.**
  - [Radio Browser](https://www.radio-browser.info/)'s public API, over
    `https://`, for the station list. Its server must be one of
    `*.api.radio-browser.info`. When you play a station, KaiRadio reports the
    play to it (the station's id), as the API asks clients to; that's what
    ranks stations by popularity. Nothing else about you is sent.
  - The station itself, at the stream address it published, and the host of
    its logo.
  - [Open-Meteo](https://open-meteo.com/), over `https://`, for the time and
    weather beside the globe: it gets only the middle of the country picked on
    the map, not where you are.
  - GitHub, over `https://`, at most once a day (Settings > About > Check
    once a day turns it off) and when you choose **Look for an update**: it
    asks this repository for its latest release. Installing one downloads
    `KaiRadio.nro` only from this repository's release, checks its size and
    SHA-256 against what GitHub lists for it, and only then puts it in place,
    keeping the old one until the swap is done. From brewstore, KaiRadio only
    says a new version is out and leaves updating to brewstore.
  - Requests say they're from KaiRadio (its version and this repository).
- **Only http and https.** Streams, playlists and logos are limited to
  `http://` and `https://` (no `file:`, `ftp:` or other schemes, even through a
  redirect), and an `https://` address can't be redirected to plain `http://`.
  Certificates are checked. Many stations only stream over plain `http://`;
  what you listen to then travels unencrypted, as with any web radio player.
- **What's kept on the card**, in KaiRadio's folder: your settings, favorites
  and recent stations. No account, no password, no history sent anywhere.

## Untrusted input

- **Streams** (MP3, AAC, Ogg Vorbis, Opus, FLAC; HLS with MPEG-TS, AAC or
  fMP4 pieces) are read by FFmpeg (LGPL build, no GPL parts). An HLS list
  keeps at most its newest 256 pieces, with its timing values bounded.
- **Song titles** (the stream's ICY metadata) are checked as strict UTF-8; **logos**
  are only decoded up to 1024×1024 pixels; **Radio Browser's answers** are
  parsed with size limits, and station ids are checked before they go in a
  URL.
- **Hardening.** The `.nro` is built with `-fstack-protector-strong` and
  `-fwrapv`. The host tests run under AddressSanitizer and
  UndefinedBehaviorSanitizer.
- **Pinned dependencies.** The libraries in `third_party/` are pinned, with
  SHA-256 sums: see [third_party/VERSIONS.md](third_party/VERSIONS.md) and
  check them with `sh third_party/verify.sh`. libcurl comes from devkitPro's
  packages (7.69.1, older than upstream's).

The main remaining risk is in FFmpeg's demuxers and decoders, which parse
every stream. A station you don't know can send anything.
