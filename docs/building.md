# Building

[Back to the README](../README.md)

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

The world map is drawn from Natural Earth by `tools/make_map.py`. The
vendored code, fonts and data are listed in
[third_party/VERSIONS.md](../third_party/VERSIONS.md).
