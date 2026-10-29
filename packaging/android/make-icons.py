#!/usr/bin/env python3
"""Writes the Android launcher icon resources from assets/icons/icon.png.

Run it from the repository root after the artwork changes. It needs Pillow.

The icon is an adaptive icon in the style of the desktop one: a black squircle
with a thin copper outline that goes from pale gold at the top left to dark
copper at the bottom right, and the saw blade of the artwork on it. The
background layer is plain black. The squircle sits inside the 66 dp safe zone,
so a rounded-square or a circular launcher mask leaves it whole. The outline is
thin because the artwork's is, about a percent of the icon's width.

The blade is cut out of the artwork by brightness. The thin blueprint lines
around it are removed by a morphological opening, and the cut is eroded one
pixel and softened so no dark fringe of the artwork's shadow stays on the edge.
"""

import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ICON = Path("assets/icons/icon.png")
RES = Path("packaging/android/res")

# The square of the 1254 px artwork that holds the blade, and the blade's hub.
BLADE_BOX = (247, 230, 247 + 760, 230 + 760)
HUB = (380, 365)
BLADE_WIDTH_PX = 684  # tooth tip to tooth tip in that square

# Sizes in dp on the 108 dp canvas. The squircle's corners stay inside the
# 36 dp radius of a circular mask. The outline is a thin line.
SQUIRCLE_HALF = 28.5
SQUIRCLE_EXPONENT = 5.0
OUTLINE_WIDTH = 1.0
BLADE_SHARE = 0.74  # of the squircle's width inside the outline

# The outline's colors from the top left to the bottom right, as in the artwork.
OUTLINE_STOPS = [(0.0, (0xF6, 0xD3, 0xA0)), (0.4, (0xD9, 0x86, 0x3A)), (1.0, (0x7A, 0x3D, 0x14))]

DENSITIES = {"mdpi": 108, "hdpi": 162, "xhdpi": 216, "xxhdpi": 324, "xxxhdpi": 432}
MASTER_PER_DP = 16
SUPERSAMPLE = 2


def blade_cutout() -> Image.Image:
    crop = Image.open(ICON).convert("RGBA").crop(BLADE_BOX)
    mask = crop.convert("L").point(lambda value: 255 if value > 0.28 * 255 else 0)
    mask = mask.filter(ImageFilter.MinFilter(9)).filter(ImageFilter.MaxFilter(9))
    mask = mask.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.GaussianBlur(1.2))
    crop.putalpha(mask)
    return crop


def squircle_points(center: float, half: float, exponent: float, count: int = 1440):
    points = []
    for step in range(count):
        angle = 2.0 * math.pi * step / count
        cosine = math.cos(angle)
        sine = math.sin(angle)
        x = math.copysign(abs(cosine) ** (2.0 / exponent), cosine)
        y = math.copysign(abs(sine) ** (2.0 / exponent), sine)
        points.append((center + half * x, center + half * y))
    return points


def gradient_square(size: int) -> Image.Image:
    """The outline's colors along the diagonal, top left to bottom right."""
    image = Image.new("RGB", (size, size))
    pixels = image.load()
    for y in range(size):
        for x in range(size):
            t = (x + y) / (2.0 * (size - 1))
            for (t0, c0), (t1, c1) in zip(OUTLINE_STOPS, OUTLINE_STOPS[1:]):
                if t <= t1:
                    k = (t - t0) / (t1 - t0)
                    pixels[x, y] = tuple(round(c0[i] + (c1[i] - c0[i]) * k) for i in range(3))
                    break
    return image


def polygon_mask(size: int, half_dp: float) -> Image.Image:
    big = size * SUPERSAMPLE
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).polygon(
        squircle_points(big / 2.0, half_dp * MASTER_PER_DP * SUPERSAMPLE, SQUIRCLE_EXPONENT), fill=255
    )
    return mask.resize((size, size), Image.LANCZOS)


def foreground_master() -> Image.Image:
    size = 108 * MASTER_PER_DP
    outer = polygon_mask(size, SQUIRCLE_HALF)
    inner = polygon_mask(size, SQUIRCLE_HALF - OUTLINE_WIDTH)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.paste((0, 0, 0, 255), (0, 0), outer)
    # The outline is the outer shape minus the inner one.
    ring = ImageChops.subtract(outer, inner)
    canvas.paste(gradient_square(size), (0, 0), ring)
    blade = blade_cutout()
    diameter = 2.0 * (SQUIRCLE_HALF - OUTLINE_WIDTH) * BLADE_SHARE * MASTER_PER_DP
    scale = diameter / BLADE_WIDTH_PX
    blade = blade.resize((round(blade.width * scale), round(blade.height * scale)), Image.LANCZOS)
    hub = (HUB[0] * scale, HUB[1] * scale)
    canvas.alpha_composite(blade, (round(size / 2 - hub[0]), round(size / 2 - hub[1])))
    return canvas


def main() -> None:
    master = foreground_master()
    for density, canvas in DENSITIES.items():
        folder = RES / f"mipmap-{density}"
        folder.mkdir(parents=True, exist_ok=True)
        master.resize((canvas, canvas), Image.LANCZOS).save(folder / "ic_launcher_foreground.png", optimize=True)

    (RES / "drawable").mkdir(parents=True, exist_ok=True)
    (RES / "drawable" / "ic_launcher_background.xml").write_text(
        """<?xml version="1.0" encoding="utf-8"?>
<shape xmlns:android="http://schemas.android.com/apk/res/android" android:shape="rectangle">
    <solid android:color="#000000" />
</shape>
"""
    )
    (RES / "mipmap-anydpi-v26").mkdir(parents=True, exist_ok=True)
    adaptive = """<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@drawable/ic_launcher_background" />
    <foreground android:drawable="@mipmap/ic_launcher_foreground" />
</adaptive-icon>
"""
    for name in ("ic_launcher", "ic_launcher_round"):
        (RES / "mipmap-anydpi-v26" / f"{name}.xml").write_text(adaptive)


if __name__ == "__main__":
    main()
