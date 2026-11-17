# Vendored third-party code and fonts

These files are copied unmodified from the upstream commits below. Run
`sh third_party/verify.sh` to check them against the SHA-256 sums.

To update a dependency, download the files from a newer commit, then update
the commit, version and SHA-256 in this file and in `verify.sh`. Review the
upstream changes before you update: they parse untrusted data from the network.

| File | Upstream | Commit | Version | License |
|------|----------|--------|---------|---------|
| `stb_image.h` | [nothings/stb](https://github.com/nothings/stb) | `9600522de53973336fa033a289858a4354cbb04c` | 2.32 | Public domain or MIT |
| `stb_truetype.h` | [nothings/stb](https://github.com/nothings/stb) | `9600522de53973336fa033a289858a4354cbb04c` | 1.28 | Public domain or MIT |
| `../data/font_regular.ttf` | [notofonts/notofonts.github.io](https://github.com/notofonts/notofonts.github.io) (`fonts/NotoSans/hinted/ttf/NotoSans-Regular.ttf`) | `cfa91e36c7240b622fa7fcd3a90eeff15e28ae1c` | Noto Sans | SIL OFL 1.1 |
| `../data/font_bold.ttf` | [notofonts/notofonts.github.io](https://github.com/notofonts/notofonts.github.io) (`fonts/NotoSans/hinted/ttf/NotoSans-Bold.ttf`) | `cfa91e36c7240b622fa7fcd3a90eeff15e28ae1c` | Noto Sans | SIL OFL 1.1 |
| `NotoSans-OFL.txt` | [notofonts/latin-greek-cyrillic](https://github.com/notofonts/latin-greek-cyrillic) (`OFL.txt`) | `ffaec0a756877220dbf718ce08b8479a0be82479` | - | - |
| `cJSON.c`, `cJSON.h` | [DaveGamble/cJSON](https://github.com/DaveGamble/cJSON) | `acc76239bee01d8e9c858ae2cab296704e52d916` | 1.7.18 | MIT (`cJSON-LICENSE.txt`) |

The fonts are renamed (`NotoSans-Regular.ttf` → `font_regular.ttf`) so that
the build can embed them as `font_regular_ttf`. They are otherwise unchanged.

## Made from third-party data

| Files | Made from | By | License |
|-------|-----------|----|---------|
| `../data/world_map.png`, `../source/map_countries.c` | [Natural Earth](https://www.naturalearthdata.com/) 1:50m Admin 0 Countries, [nvkelso/natural-earth-vector](https://github.com/nvkelso/natural-earth-vector) v5.1.2 (`geojson/ne_50m_admin_0_countries.geojson`) | `tools/make_map.py` | Public domain |

## SHA-256

```
deb4f18c9ca21b751a1323019031c94c0bf5ca1d2929ef5913cc56f00aab6e91  stb_image.h
428cfe93520f2aed5e413a220b0db5bba5b17e2ebe5a64e4a8efa9eeeb67ec5f  stb_truetype.h
cee9892f9f0cc8fe882c9e9537ee6a89621d86ee7ceaf70b02e2b2b1c25c061a  NotoSans-OFL.txt
478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823  ../data/font_regular.ttf
1df075a380fc7cb898acf64c1f7b3b4dd780de3caa860178bf929de35817a913  ../data/font_bold.ttf
75c51de8fa40ac9d7a99319c6330719bd692eb81c0a869265f3d4c682533f9b9  cJSON.c
0578cc29132912edbc88f83207a8fc76e5db3db0605497e909a9384ef3cc474b  cJSON.h
```

## Libraries linked into the .nro

These come from outside this folder and are linked statically:

| Library | Version | Where from | License |
|---------|---------|------------|---------|
| FFmpeg (libavformat, libavcodec, libswresample, libavutil) | 7.1 | Built by `ffmpeg/Dockerfile` from ffmpeg.org's release tarball, only the parts radio needs | LGPL 2.1 (`FFmpeg-LGPL-2.1.txt`) |
| libcurl | 7.69.1 | devkitPro's `switch-curl` package | curl license (`curl-LICENSE.txt`) |
| zlib | 1.3.1 | devkitPro's `switch-zlib` package | zlib license |
| libounce (libnx fork) | - | ounce-devkit | ISC |

Under the LGPL, you may change FFmpeg and relink KaiRadio with your version:
the whole source and the scripts that build it are in this repository.
