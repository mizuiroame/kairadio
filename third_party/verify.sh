#!/bin/sh
# Checks the vendored files and fonts against the SHA-256 sums in VERSIONS.md.
set -eu
cd "$(dirname "$0")"
sha256sum -c <<'EOF'
deb4f18c9ca21b751a1323019031c94c0bf5ca1d2929ef5913cc56f00aab6e91  stb_image.h
428cfe93520f2aed5e413a220b0db5bba5b17e2ebe5a64e4a8efa9eeeb67ec5f  stb_truetype.h
cee9892f9f0cc8fe882c9e9537ee6a89621d86ee7ceaf70b02e2b2b1c25c061a  NotoSans-OFL.txt
478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823  ../data/font_regular.ttf
1df075a380fc7cb898acf64c1f7b3b4dd780de3caa860178bf929de35817a913  ../data/font_bold.ttf
75c51de8fa40ac9d7a99319c6330719bd692eb81c0a869265f3d4c682533f9b9  cJSON.c
0578cc29132912edbc88f83207a8fc76e5db3db0605497e909a9384ef3cc474b  cJSON.h
8f1195d55a2fd315a07d812328470ca9ba2abb78c8d317ff19619d5125e00cea  flag-icons-LICENSE.txt
EOF
