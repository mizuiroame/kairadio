"""Draws KaiRadio's world map from Natural Earth's countries (public domain,
naturalearthdata.com), for the World view:

  data/world_map.png      the land, as coverage (255 inside, lower on borders),
                          at twice the framebuffer size it's drawn at, so it
                          stays sharp on a 4K screen (the app shrinks it for
                          smaller ones)
  source/map_countries.c  each country's place for its light, and points
                          spread over it for the smaller lights

The map is equirectangular from 180W to 180E and from MAP_NORTH to
MAP_SOUTH (no Antarctica). Points are 0-65535 across the map's width and
height.

Usage: python tools/make_map.py [ne_50m_admin_0_countries.geojson]
(by default tools/.cache/, downloaded from Natural Earth's GitHub release)
"""
import json
import random
import sys
import urllib.request
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
SOURCE_URL = ("https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/"
              "geojson/ne_50m_admin_0_countries.geojson")
CACHE = ROOT / "tools" / ".cache" / "ne_50m_admin_0_countries.geojson"

MAP_NORTH, MAP_SOUTH = 83.0, -56.0
FB_W, FB_H = 1786, 690   # Framebuffer pixels: the layout's 1191 x 460, times 1.5.
IMAGE = 2                # The image is this many times the framebuffer size.
SUPER = 2                # Drawn this much larger again, then shrunk, for smooth coasts.
BORDER = 150             # Coverage on borders between countries: they show as faint lines.
POINTS = 24              # Most points kept per country.
SEED = 1983


def load(path):
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(SOURCE_URL) as r:
            path.write_bytes(r.read())
    return json.loads(path.read_text(encoding="utf-8"))["features"]


def project(lon, lat, w, h):
    return (lon + 180.0) / 360.0 * w, (MAP_NORTH - lat) / (MAP_NORTH - MAP_SOUTH) * h


def rings(geometry):
    """The outer rings of a Polygon or MultiPolygon (holes are lakes: left as land)."""
    if geometry["type"] == "Polygon":
        return [geometry["coordinates"][0]]
    return [poly[0] for poly in geometry["coordinates"]]


def draw_land(features):
    w, h = FB_W * IMAGE * SUPER, FB_H * IMAGE * SUPER
    land = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(land)
    for f in features:
        for ring in rings(f["geometry"]):
            d.polygon([project(lon, lat, w, h) for lon, lat in ring], fill=255)
    for f in features:
        for ring in rings(f["geometry"]):
            d.line([project(lon, lat, w, h) for lon, lat in ring], fill=BORDER, width=IMAGE * SUPER)
    return land.resize((FB_W * IMAGE, FB_H * IMAGE), Image.LANCZOS)


def country_points(features, rng):
    """For each code: (label x, y) and up to POINTS points inside it, 0-65535."""
    by_code = {}
    for f in features:
        p = f["properties"]
        code = p["ISO_A2_EH"] if p["ISO_A2_EH"] not in ("-99", "") else p["ISO_A2"]
        if len(code) == 2 and code.isalpha() and code != "AQ":
            by_code.setdefault(code, []).append(f)
    w, h = FB_W, FB_H
    out = {}
    for code, parts in sorted(by_code.items()):
        mask = Image.new("1", (w, h), 0)
        d = ImageDraw.Draw(mask)
        for f in parts:
            for ring in rings(f["geometry"]):
                d.polygon([project(lon, lat, w, h) for lon, lat in ring], fill=1)
        inside = [(x, y) for y in range(h) for x in range(w) if mask.getpixel((x, y))] if mask.getbbox() else []
        biggest = max(parts, key=lambda f: sum(len(r) for r in rings(f["geometry"])))
        lx, ly = project(biggest["properties"]["LABEL_X"], biggest["properties"]["LABEL_Y"], w, h)
        if not (0 <= ly < h):
            continue  # In Antarctica's latitudes, off the map.
        rng.shuffle(inside)
        pts = inside[:POINTS]
        out[code] = ((lx, ly), pts)
    return out


def scale(v, size):
    return max(0, min(65535, round(v / size * 65535)))


def write_c(countries, path):
    lines = [
        "// Made by tools/make_map.py from Natural Earth (public domain): where each",
        "// country's lights go on the World view's map. Don't edit by hand.",
        '#include "worldmap.h"',
        "",
        "const MapPoint MAP_POINTS[] = {",
    ]
    table = []
    first = 0
    for code, ((lx, ly), pts) in countries.items():
        for x, y in pts:
            lines.append(f"    {{{scale(x + 0.5, FB_W)}, {scale(y + 0.5, FB_H)}}},")
        table.append(f'    {{"{code}", {{{scale(lx, FB_W)}, {scale(ly, FB_H)}}}, {first}, {len(pts)}}},')
        first += len(pts)
    lines += ["};", "", "const MapCountry MAP_COUNTRIES[] = {"] + table + [
        "};", "",
        "const size_t MAP_COUNTRY_COUNT = sizeof(MAP_COUNTRIES) / sizeof(MAP_COUNTRIES[0]);", ""]
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main():
    features = load(Path(sys.argv[1]) if len(sys.argv) > 1 else CACHE)
    draw_land(features).save(ROOT / "data" / "world_map.png", optimize=True)
    countries = country_points(features, random.Random(SEED))
    write_c(countries, ROOT / "source" / "map_countries.c")
    print(f"{len(countries)} countries, map {FB_W}x{FB_H}")


if __name__ == "__main__":
    main()
