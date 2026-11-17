"""Draws KaiRadio's icon (icon.jpg, 256x256): a radio with broadcast waves on
the dark background KaiPlayer's icon uses, in KaiRadio's sky blue.

Usage: python tools/make_icon.py [OUT]
"""
import math
import sys

from PIL import Image, ImageDraw, ImageFilter

SIZE = 256
SCALE = 4  # Drawn larger, then shrunk, for smooth edges.
BLUE = (79, 195, 247)
PINK = (255, 107, 157)
BG_CENTER = (40, 42, 58)
BG_EDGE = (16, 17, 24)


def background(size):
    img = Image.new("RGB", (size, size))
    px = img.load()
    c = size / 2
    for y in range(size):
        for x in range(size):
            t = min(1.0, math.hypot(x - c, y - c * 0.9) / (size * 0.72))
            px[x, y] = tuple(int(a + (b - a) * t) for a, b in zip(BG_CENTER, BG_EDGE))
    return img


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "icon.jpg"
    s = SIZE * SCALE
    img = background(s).convert("RGBA")
    layer = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    u = s / 256  # One icon pixel.

    # Broadcast waves above the antenna tip.
    tip = (172 * u, 52 * u)
    for i, r in enumerate((22, 40, 58)):
        box = (tip[0] - r * u, tip[1] - r * u, tip[0] + r * u, tip[1] + r * u)
        alpha = 255 - i * 60
        d.arc(box, 205, 335, fill=BLUE + (alpha,), width=int(9 * u))

    # The antenna.
    d.line((92 * u, 98 * u, tip[0], tip[1]), fill=BLUE, width=int(9 * u))
    d.ellipse((tip[0] - 9 * u, tip[1] - 9 * u, tip[0] + 9 * u, tip[1] + 9 * u), fill=BLUE)

    # The body.
    body = (40 * u, 94 * u, 216 * u, 206 * u)
    d.rounded_rectangle(body, radius=26 * u, fill=BLUE)

    # Speaker: a dark disc with rings.
    cx, cy, r = 92 * u, 150 * u, 36 * u
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=BG_EDGE)
    for rr in (26, 15):
        d.ellipse((cx - rr * u, cy - rr * u, cx + rr * u, cy + rr * u), outline=BLUE, width=int(5 * u))
    d.ellipse((cx - 6 * u, cy - 6 * u, cx + 6 * u, cy + 6 * u), fill=BLUE)

    # Dial window with a pink needle, and two knobs.
    dial = (142 * u, 118 * u, 198 * u, 150 * u)
    d.rounded_rectangle(dial, radius=8 * u, fill=BG_EDGE)
    for k in range(5):
        x = (150 + k * 10) * u
        d.line((x, 136 * u, x, 144 * u), fill=BLUE + (170,), width=int(3 * u))
    d.line((171 * u, 123 * u, 171 * u, 145 * u), fill=PINK, width=int(5 * u))
    for kx in (154, 186):
        d.ellipse(((kx - 9) * u, 165 * u, (kx + 9) * u, 183 * u), fill=BG_EDGE)

    # A soft glow under the shapes.
    glow = layer.filter(ImageFilter.GaussianBlur(10 * u))
    glow.putalpha(glow.getchannel("A").point(lambda a: a * 0.35))
    img = Image.alpha_composite(img, glow)
    img = Image.alpha_composite(img, layer)
    img = img.convert("RGB").resize((SIZE, SIZE), Image.LANCZOS)
    img.save(out, quality=95)


if __name__ == "__main__":
    main()
