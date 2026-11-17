"""Makes the genre icons from Lucide (ISC, with some icons MIT from Feather;
github.com/lucide-icons/lucide), for Search > Genre:

  data/icons.bin         each icon as two 8-bit masks, ICON_SMALL then ICON_LARGE
                         pixels square (1080p, and 1440p or 4K screens)
  source/icons_index.c   each icon's name and where its masks start

The icons are the ones named in source/categoryicons.c's rules. The SVGs are
rasterized by rsvg-convert in a Docker container (python:3.12-slim with
librsvg2-bin), white on clear: the mask is the alpha.

Usage: python tools/make_icons.py [lucide-1.66.0 folder]
(by default tools/.cache/lucide/lucide-1.66.0, from the 1.66.0 tarball)
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "tools" / ".cache" / "lucide" / "lucide-1.66.0"
ICON_SMALL, ICON_LARGE = 42, 84  # The layout's 28, times 1.5, and twice that.
STROKE = "2.25"                  # A little bolder than Lucide's 2, for a TV across the room.


def icon_names():
    source = (ROOT / "source" / "categoryicons.c").read_text(encoding="utf-8")
    rules = source[source.index("GENRES[] = {"):]
    rules = rules[:rules.index("\n};")]
    return sorted(set(re.findall(r'^\s*\{"([a-z0-9-]+)", "', rules, re.MULTILINE)))


def main():
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else SOURCE
    names = icon_names()
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        for name in names:
            svg = (source / "icons" / f"{name}.svg").read_text(encoding="utf-8")
            svg = svg.replace("currentColor", "#ffffff").replace('stroke-width="2"', f'stroke-width="{STROKE}"')
            (work / f"{name}.svg").write_text(svg, encoding="utf-8")
        script = " && ".join(
            ["apt-get update -qq >/dev/null", "apt-get install -y -qq librsvg2-bin >/dev/null"] +
            [f"for f in /w/*.svg; do rsvg-convert -w {n} -h {n} -o /w/$(basename $f .svg).{n}.png $f; done"
             for n in (ICON_SMALL, ICON_LARGE)])
        subprocess.run(["docker", "run", "--rm", "-v", f"{work}:/w", "python:3.12-slim", "sh", "-c", script],
                       check=True)
        blob = bytearray()
        index = []
        for name in names:
            index.append((name, len(blob)))
            for n in (ICON_SMALL, ICON_LARGE):
                mask = Image.open(work / f"{name}.{n}.png").convert("RGBA").getchannel("A")
                blob += mask.tobytes()
    (ROOT / "data" / "icons.bin").write_bytes(bytes(blob))
    lines = [
        "// Made by tools/make_icons.py from Lucide (ISC): each genre icon's masks",
        "// in data/icons.bin. Don't edit by hand.",
        '#include "icons.h"',
        "",
        "const IconEntry ICON_INDEX[] = {",
    ] + [f'    {{"{name}", {offset}}},' for name, offset in index] + [
        "};",
        "",
        "const size_t ICON_COUNT = sizeof(ICON_INDEX) / sizeof(ICON_INDEX[0]);",
        "",
    ]
    (ROOT / "source" / "icons_index.c").write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"{len(names)} icons, {len(blob)} bytes")


if __name__ == "__main__":
    main()
