#!/usr/bin/env bash
# Writes the Android launcher icon resources from assets/icons/icon.png. Run it
# from the repository root after the artwork changes. It needs ImageMagick.
#
# The icon is an adaptive icon: a flat dark background and a foreground layer.
# The foreground is the inside of the artwork (the saw blade on its blueprint),
# scaled so the blade fills the 66 dp safe zone of the 108 dp canvas. Round,
# squircle, and square launcher masks all keep the blade whole.
set -euo pipefail

ICON="assets/icons/icon.png"
RES="packaging/android/res"
# The inner panel of the 1254 px artwork, a square around the blade's center.
CROP="860x860+197+177"
# Share of the canvas the cropped panel covers. The blade is 79% of the
# panel, so 0.772 puts it at 61% of the canvas.
SHARE="0.772"

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
