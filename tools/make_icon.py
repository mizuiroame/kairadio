"""Draws KaiRadio's icon (icon.jpg, 256x256): a flat radio sending out waves, all
of it inside the square.

Usage: python tools/make_icon.py [OUT]
"""
import sys

from icon_render import Canvas, arc, capsule, circle, round_rect, save_icon, subtract, union

BLUE = (79, 195, 247)
TIP = (176.0, 76.0)
WAVE_RADII = (20.0, 38.0, 56.0)
WAVE_HALF_WIDTH = 5.5


def draw() -> Canvas:
    canvas = Canvas()
    body = round_rect(32, 112, 224, 222, 22)
    antenna = union(capsule((86, 116), TIP, 5.0), circle(TIP[0], TIP[1], 9.0))
    radio = union(body, antenna)
    canvas.long_shadow(radio)
    canvas.fill(radio, BLUE)
    for radius in WAVE_RADII:
        canvas.fill(arc(TIP[0], TIP[1], radius, 0.0, 45.0, WAVE_HALF_WIDTH), BLUE)

    speaker = subtract(circle(88, 167, 36), circle(88, 167, 25))
    canvas.cut(speaker)
    canvas.cut(circle(88, 167, 12))
    canvas.cut(round_rect(144, 138, 202, 166, 6))
    canvas.fill(round_rect(171, 142, 176, 162, 2), BLUE)
    canvas.cut(circle(156, 193, 9.5))
    canvas.cut(circle(190, 193, 9.5))
    return canvas


def main() -> None:
    save_icon(draw().result(), sys.argv[1] if len(sys.argv) > 1 else "icon.jpg")


if __name__ == "__main__":
    main()
