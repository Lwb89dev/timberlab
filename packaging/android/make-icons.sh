#!/usr/bin/env bash
# Writes the Android launcher icon resources from assets/icons/icon.png. Run it
# from the repository root after the artwork changes. It needs ImageMagick.
#
# The icon is an adaptive icon: a flat dark background and a foreground layer.
# The foreground is the whole desktop icon, the rounded square with its copper
# edge, centered and scaled to fit the 72 dp that every launcher mask shows. A
# squircle mask, which is what One UI uses, shows it as drawn. A circle mask
# clips the four corners.
set -euo pipefail

ICON="assets/icons/icon.png"
RES="packaging/android/res"
# A square around the rounded square of the 1254 px artwork, a little wider
# than its 1059 x 1044 px body.
CROP="1070x1070+92+72"
# Share of the 108 dp canvas the cropped square covers. The masks show 72 dp, 66.7%,
# and the icon stays a little inside that so its edge is not clipped.
SHARE="0.630"

test -f "${ICON}"
mkdir -p "${RES}/values" "${RES}/mipmap-anydpi-v26"
for entry in mdpi:108 hdpi:162 xhdpi:216 xxhdpi:324 xxxhdpi:432; do
  density="${entry%%:*}"
  canvas="${entry##*:}"
  inner="$(awk -v c="${canvas}" -v s="${SHARE}" 'BEGIN { printf "%d", c * s + 0.5 }')"
  mkdir -p "${RES}/mipmap-${density}"
  convert "${ICON}" -crop "${CROP}" +repage -resize "${inner}x${inner}" -background none -gravity center \
    -extent "${canvas}x${canvas}" "${RES}/mipmap-${density}/ic_launcher_foreground.png"
done

cat > "${RES}/values/ic_launcher_background.xml" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <color name="ic_launcher_background">#1C1C1A</color>
</resources>
XML

for name in ic_launcher ic_launcher_round; do
  cat > "${RES}/mipmap-anydpi-v26/${name}.xml" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/ic_launcher_background" />
    <foreground android:drawable="@mipmap/ic_launcher_foreground" />
</adaptive-icon>
XML
done
