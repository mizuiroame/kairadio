# KaiRadio

Open-source internet radio player for Switch 2 homebrew (kaiHEN), in the
spirit of [KaiPlayer](https://github.com/mizuiroame/kaiplayer).

> **Work in progress.** Nothing to download yet.

## Plans

- Browse and search public internet radio stations from the community
  [Radio Browser](https://www.radio-browser.info/) directory: by name,
  country, language or genre.
- Play MP3, AAC and Ogg (Vorbis and Opus) streams over HTTP and HTTPS, with
  the title of what's playing when the station sends it.
- Favorites saved on your card, and your own station list (a plain `.m3u` or
  `.pls` file).
- Drawn on the GPU, sharp in handheld and docked, with KaiPlayer's look,
  touch and buttons.

## Building

KaiRadio is built in Docker, on top of the
[ounce-devkit](https://github.com/ouncebrew/ounce-devkit) image:

```sh
docker build -t kairadio-devkit ffmpeg   # once: the devkit plus FFmpeg (LGPL)
.\build.ps1                              # KaiRadio.nro
.\build.ps1 test                         # host tests
.\build.ps1 preview                      # screenshots and touch checks
```

`ffmpeg/Dockerfile` builds FFmpeg 7.1 from its release tarball (checked by
SHA-256) with only the demuxers, parsers and audio decoders radio needs, and
fails if the build would include GPL or non-free parts.

## Legal notice

- **Independent project.** KaiRadio is not affiliated with, endorsed by or
  sponsored by Nintendo Co., Ltd. or its subsidiaries, by Radio Browser, or by
  any radio station.
- **Trademarks.** "Nintendo", "Nintendo Switch" and "Switch 2" are trademarks
  of their owners. They are used here only to say which hardware this program
  works with.
- **No Nintendo material.** This repository and its releases contain no
  Nintendo code, SDK, firmware, cryptographic keys, games, or graphics or audio
  assets. KaiRadio is built only with open-source tools and libraries.
- **Stations.** KaiRadio ships no station list, logos or broadcasts. It plays
  the public streams that stations publish themselves, as any web radio player
  does, and it doesn't record them. Each station's content belongs to its
  owners.
- **User responsibility.** Running homebrew (kaiHEN) can put your console at
  risk, can affect its warranty and can go against the manufacturer's terms of
  service. You do it at your own risk.
- **No warranty.** The software is provided "as is", without warranty of any
  kind. See [LICENSE](LICENSE).

This notice is not legal advice.

## License

KaiRadio is released under the [MIT License](LICENSE).
