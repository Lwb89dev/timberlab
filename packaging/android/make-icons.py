#!/usr/bin/env python3
"""Writes the Android launcher icon resources from assets/icons/icon.png.

Run it from the repository root after the artwork changes. It needs Pillow.

The icon is an adaptive icon. The background layer is an orange gradient. The
foreground is a black squircle with the saw blade of the artwork on it, drawn
well inside the 66 dp safe zone, so the gradient is what a launcher mask shows
around it: a thin orange outline on a rounded-square mask, a thicker one on a
circular mask, and no clipped corner on either.

The blade is cut out of the artwork by brightness. The thin blueprint lines
around it are removed by a morphological opening, and the cut is eroded one
pixel and softened so no dark fringe of the artwork's shadow stays on the edge.
"""

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ICON = Path("assets/icons/icon.png")
RES = Path("packaging/android/res")

# The square of the 1254 px artwork that holds the blade, and the blade's hub.
BLADE_BOX = (247, 230, 247 + 760, 230 + 760)
HUB = (380, 365)
BLADE_WIDTH_PX = 684  # tooth tip to tooth tip in that square

# Sizes in dp on the 108 dp canvas.
SQUIRCLE_HALF = 27.5
SQUIRCLE_EXPONENT = 5.0
BLADE_DIAMETER = 41.8

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


def foreground_master() -> Image.Image:
    size = 108 * MASTER_PER_DP
    big = size * SUPERSAMPLE
    canvas = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    ImageDraw.Draw(canvas).polygon(
        squircle_points(big / 2.0, SQUIRCLE_HALF * MASTER_PER_DP * SUPERSAMPLE, SQUIRCLE_EXPONENT),
        fill=(0, 0, 0, 255),
    )
    canvas = canvas.resize((size, size), Image.LANCZOS)
    blade = blade_cutout()
    scale = BLADE_DIAMETER * MASTER_PER_DP / BLADE_WIDTH_PX
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
    <gradient
        android:type="linear"
        android:angle="315"
        android:startColor="#F6B36B"
        android:centerColor="#D27A2A"
        android:endColor="#8C4616" />
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
