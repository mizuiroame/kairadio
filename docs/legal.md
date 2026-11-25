# Legal notice and licenses

[Back to the README](../README.md)

## Legal notice

- **Independent project.** KaiRadio is not affiliated with, endorsed by or
  sponsored by Nintendo Co., Ltd. or its subsidiaries, by Radio Browser, or by
  any radio station.
- **Trademarks.** "Nintendo", "Nintendo Switch" and "Switch 2" are trademarks
  of their owners. They are used here only to say which hardware this program
  works with. Format names are used only to name the streams KaiRadio plays.
- **No Nintendo material.** This repository and its releases contain no
  Nintendo code, SDK, firmware, cryptographic keys, games, or graphics or audio
  assets. KaiRadio is built only with open-source tools and libraries.
- **Stations.** KaiRadio ships no station list, logos or broadcasts. It plays
  the public streams that stations publish themselves, as any web radio player
  does, and it doesn't record them. Each station's content belongs to its
  owners. The station list comes from Radio Browser's public API, and KaiRadio
  reports plays to it, as the API asks clients to.
- **Weather.** The time and weather beside the globe come from
  [Open-Meteo](https://open-meteo.com/) (CC BY 4.0). KaiRadio sends it only
  the middle of the country picked on the map, nothing about you.
- **FFmpeg.** KaiRadio uses FFmpeg, built under the LGPL 2.1 (no GPL or
  non-free parts). The script that builds it is in this repository, so it can
  be rebuilt, changed and relinked.
- **User responsibility.** Running homebrew (kaiHEN) can put your console at
  risk, can affect its warranty and can go against the manufacturer's terms of
  service. You do it at your own risk.
- **No warranty.** The software is provided "as is", without warranty of any
  kind. See [LICENSE](../LICENSE).

This notice is not legal advice.

## Licenses

KaiRadio is released under the [MIT License](../LICENSE). FFmpeg is released
under the LGPL 2.1; the other bundled libraries are listed in
[third_party/VERSIONS.md](../third_party/VERSIONS.md). Japanese, Chinese and
Korean text is drawn with the console's own fonts, which aren't part of
KaiRadio. The world map is drawn
from [Natural Earth](https://www.naturalearthdata.com/) (public domain) by
`tools/make_map.py`, its land colored by Natural Earth's Cross Blended
Hypsometric Tints with Shaded Relief and Water (1:50m, public domain) by
`tools/make_terrain.py`; the flags come from
[flag-icons](https://github.com/lipis/flag-icons) (MIT) and the genre icons
from [Lucide](https://lucide.dev/) (ISC). Weather data by
[Open-Meteo.com](https://open-meteo.com/) (CC BY 4.0).

| Part | What it does | License |
|------|--------------|---------|
| [FFmpeg](https://ffmpeg.org/) | Demuxing and decoding the streams | [LGPL 2.1](../third_party/FFmpeg-LGPL-2.1.txt) |
| [curl](https://curl.se/) | The network | [curl](../third_party/curl-LICENSE.txt) |
| [cJSON](https://github.com/DaveGamble/cJSON) | Reading JSON | [MIT](../third_party/cJSON-LICENSE.txt) |
| [stb](https://github.com/nothings/stb) | Images and fonts | Public domain or MIT |
| [zlib](https://zlib.net/) | Compression | [zlib](../third_party/zlib-LICENSE.txt) |
| [OunceVK](https://github.com/ouncebrew/ouncevk) (Mesa) | Drawing on the GPU (EGL and OpenGL ES) | [MIT](../third_party/Mesa-MIT.txt) ([Mesa's notes](../third_party/Mesa-license.rst)) |
| libounce (libnx fork) | Switch 2 system library | ISC ([libounce](../third_party/libounce-ISC.txt), [libnx](../third_party/libnx-ISC.txt)) |
| Noto Sans | KaiRadio's font | [OFL](../third_party/NotoSans-OFL.txt) |
| [flag-icons](https://github.com/lipis/flag-icons) | Flags | [MIT](../third_party/flag-icons-LICENSE.txt) |
| [Lucide](https://lucide.dev/) | Genre icons | [ISC](../third_party/lucide-LICENSE.txt) (some icons MIT, from Feather) |
| [Natural Earth](https://www.naturalearthdata.com/) | The world map, and its land's colors (hypsometric tints) | Public domain |
| [Open-Meteo](https://open-meteo.com/) | Time and weather | CC BY 4.0 (data) |

Under the LGPL, you may change FFmpeg and relink KaiRadio with your version:
the whole source and the scripts that build it are in this repository.
