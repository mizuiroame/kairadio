"""Makes KaiRadio's country flags from flag-icons (MIT,
github.com/lipis/flag-icons), for Search > Country and the World view:

  data/flags.bin         every flag as a small PNG, one after another
  source/flags_index.c   each country code's PNG in it: where and how long

Each flag is FLAG_IMAGE_W x FLAG_IMAGE_H (4:3), twice the framebuffer size
it's drawn at so it stays sharp on a 4K screen, with round corners. The SVGs
are rasterized by rsvg-convert in a Docker container (python:3.12-slim with
librsvg2-bin), so nothing has to be installed here but Pillow and Docker.

Usage: python tools/make_flags.py [flag-icons-7.6.0 folder]
(by default tools/.cache/flag-icons/flag-icons-7.6.0, unpacked from the
v7.6.0 release tarball)
"""
import io
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "tools" / ".cache" / "flag-icons" / "flag-icons-7.6.0"
FLAG_IMAGE_W, FLAG_IMAGE_H = 120, 90  # The layout's 40 x 30, times 1.5, times 2.
RADIUS = 9                            # Round corners, image pixels.
SUPER = 4                             # The corner mask is drawn this much larger, then shrunk.


def rasterize(svg_dir, out_dir):
    """Every flags/4x3/*.svg to a PNG of FLAG_IMAGE_W x FLAG_IMAGE_H, in Docker."""
    script = (f"apt-get update -qq >/dev/null && apt-get install -y -qq librsvg2-bin >/dev/null && "
              f"for f in /in/*.svg; do rsvg-convert -w {FLAG_IMAGE_W} -h {FLAG_IMAGE_H} "
              f"-o /out/$(basename $f .svg).png $f; done")
    subprocess.run(["docker", "run", "--rm", "-v", f"{svg_dir}:/in:ro", "-v", f"{out_dir}:/out",
                    "python:3.12-slim", "sh", "-c", script], check=True)


def corner_mask():
    w, h = FLAG_IMAGE_W * SUPER, FLAG_IMAGE_H * SUPER
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, w - 1, h - 1), radius=RADIUS * SUPER, fill=255)
    return mask.resize((FLAG_IMAGE_W, FLAG_IMAGE_H), Image.LANCZOS)


def main():
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else SOURCE
    mask = corner_mask()
    blob = bytearray()
    index = []
    with tempfile.TemporaryDirectory() as tmp:
        rasterize(source / "flags" / "4x3", tmp)
        for png in sorted(Path(tmp).glob("*.png")):
            code = png.stem.upper()
            if len(code) != 2 or not code.isalpha():
                continue  # Regions and groups ("es-ct", "eu", "un"): the directory lists countries.
            flag = Image.open(png).convert("RGBA").resize((FLAG_IMAGE_W, FLAG_IMAGE_H))
            alpha = Image.new("L", flag.size, 0)
            alpha.paste(flag.getchannel("A"), mask=mask)
            flag.putalpha(alpha)
            out = io.BytesIO()
            flag.save(out, "PNG", optimize=True)
            index.append((code, len(blob), out.tell()))
            blob += out.getvalue()
    (ROOT / "data" / "flags.bin").write_bytes(bytes(blob))
    lines = [
        "// Made by tools/make_flags.py from flag-icons (MIT): where each country's",
        "// flag is in data/flags.bin. Don't edit by hand.",
        '#include "flags.h"',
        "",
        "const FlagEntry FLAG_INDEX[] = {",
    ] + [f'    {{"{code}", {offset}, {size}}},' for code, offset, size in index] + [
        "};",
        "",
        "const size_t FLAG_COUNT = sizeof(FLAG_INDEX) / sizeof(FLAG_INDEX[0]);",
        "",
    ]
    (ROOT / "source" / "flags_index.c").write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"{len(index)} flags, {len(blob)} bytes")


if __name__ == "__main__":
    main()
