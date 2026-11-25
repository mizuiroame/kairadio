"""Makes the colors of KaiRadio's land, for the World view's globe and flat map,
from Natural Earth's "Cross Blended Hypsometric Tints with Shaded Relief and
Water" (1:50m, public domain, naturalearthdata.com):

  data/world_terrain.jpg  the land's colors by height and climate (green
                          lowlands, tan drylands, brown and grey mountains,
                          the lakes), with the relief's shading, toned down to
                          the night map's darkness so the borders and the
                          stations' lights stay readable over it

It covers the same equirectangular part of the world as data/world_map.png
(180W to 180E, MAP_NORTH to MAP_SOUTH). The sea is not in it: the app keeps
its own dark sea, and uses world_map.png's coverage for where the land ends,
so the colors here are spread a little past the coasts (no sea color bleeds
into the land when it's scaled).

Usage: python tools/make_terrain.py [HYP_50M_SR_W.zip]
(by default tools/.cache/, downloaded from Natural Earth's CDN; about 100 MB)
Needs numpy and Pillow.
"""
import io
import sys
import urllib.request
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SOURCE_URL = "https://naciscdn.org/naturalearth/50m/raster/HYP_50M_SR_W.zip"
CACHE = ROOT / "tools" / ".cache" / "HYP_50M_SR_W.zip"
OUT = ROOT / "data" / "world_terrain.jpg"
COVERAGE = ROOT / "data" / "world_map.png"

MAP_NORTH, MAP_SOUTH = 83.0, -56.0
WIDTH = 2048                 # About the globe's land grid twice over, and the flat map's at 1080p.
HEIGHT = round(WIDTH * (MAP_NORTH - MAP_SOUTH) / 360.0)
BRIGHTNESS = 0.58            # The source is a day map: this much of its light, for a night map's land,
SATURATION = 1.1             # its colors a little stronger (they read darker),
LIFT = 0.05                  # and its darkest shadows lifted toward the app's land grey (0x46, 0x52, 0x66).
LAND_GREY = np.array([0x46, 0x52, 0x66], dtype=np.float64) / 255
QUALITY = 88


def source(path: Path) -> Image.Image:
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(SOURCE_URL) as r:
            path.write_bytes(r.read())
    with zipfile.ZipFile(path) as z:
        name = next(n for n in z.namelist() if n.lower().endswith(".tif"))
        Image.MAX_IMAGE_PIXELS = None  # 10800 x 5400, a known file.
        return Image.open(io.BytesIO(z.read(name))).convert("RGB")


def crop(world: Image.Image) -> Image.Image:
    """The map's latitudes, from the whole world's 90N to 90S."""
    top = round((90.0 - MAP_NORTH) / 180.0 * world.height)
    bottom = round((90.0 - MAP_SOUTH) / 180.0 * world.height)
    return world.crop((0, top, world.width, bottom)).resize((WIDTH, HEIGHT), Image.LANCZOS)


def tone(rgb: np.ndarray) -> np.ndarray:
    grey = rgb.mean(axis=2, keepdims=True)
    rgb = grey + (rgb - grey) * SATURATION
    rgb = rgb * BRIGHTNESS
    return rgb + (LAND_GREY - rgb) * LIFT


def box_blur(a: np.ndarray, radius: int) -> np.ndarray:
    """A box blur of radius pixels, twice (near a Gaussian), its edges clamped."""
    for _ in range(2):
        for axis in (0, 1):
            pad = [(0, 0), (0, 0)]
            pad[axis] = (radius + 1, radius)
            c = np.cumsum(np.pad(a, pad, mode="edge"), axis=axis)
            hi = np.take(c, range(2 * radius + 1, c.shape[axis]), axis=axis)
            lo = np.take(c, range(0, c.shape[axis] - 2 * radius - 1), axis=axis)
            a = (hi - lo) / (2 * radius + 1)
    return a


def spread_past_coasts(rgb: np.ndarray, land: np.ndarray) -> np.ndarray:
    """The sea's pixels get the nearby land's colors, wider and wider blurs filling what's left."""
    out = rgb * land[..., None]
    have = land.copy()
    for radius in (2, 4, 8, 16, 32, 64):  # Smooth all the way: a JPEG keeps that smallest.
        blur = lambda a: box_blur(a, radius)
        weight = blur(have)
        colors = np.stack([blur(out[..., c]) for c in range(3)], axis=2)
        fill = (weight > 1e-3) & (have < 0.999)
        mixed = colors / np.maximum(weight, 1e-6)[..., None]
        out[fill] = out[fill] + mixed[fill] * (1 - have[fill])[..., None]
        have[fill] = 1.0
    out[have < 0.5] = LAND_GREY
    return out


def main() -> None:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else CACHE
    rgb = np.asarray(crop(source(path)), dtype=np.float64) / 255
    land = np.asarray(Image.open(COVERAGE).convert("L").resize((WIDTH, HEIGHT), Image.BOX), dtype=np.float64) / 255
    land = (land > 0.5).astype(np.float64)
    rgb = spread_past_coasts(tone(rgb), land)
    Image.fromarray((np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8)).save(OUT, "JPEG", quality=QUALITY,
                                                                            subsampling=0, optimize=True)
    print(f"{OUT} ({WIDTH} x {HEIGHT}, {OUT.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
