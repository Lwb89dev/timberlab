#!/bin/bash
# Build a binary .deb that carries the TimberLab executable, the Qt and
# Open CASCADE shared libraries it was linked against, the QML modules the
# workspace imports, and the xcb platform plugin. System OpenSSL and SQLite
# stay outside the archive. Wayland platform plugins are not packaged.
set -euo pipefail

usage() {
  echo "usage: assemble-deb.sh --binary PATH --qt PATH --occt PATH --occt-source PATH --version X.Y.Z --output PATH" >&2
  exit 2
}

BINARY=""
QT_PREFIX=""
OCCT_PREFIX=""
OCCT_SOURCE=""
VERSION=""
OUTPUT=""
PATCHELF="${PATCHELF:-patchelf}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --binary) BINARY="$2"; shift 2 ;;
    --qt) QT_PREFIX="$2"; shift 2 ;;
    --occt) OCCT_PREFIX="$2"; shift 2 ;;
    --occt-source) OCCT_SOURCE="$2"; shift 2 ;;
    --version) VERSION="$2"; shift 2 ;;
    --output) OUTPUT="$2"; shift 2 ;;
    *) usage ;;
  esac
done

[[ -n "$BINARY" && -n "$QT_PREFIX" && -n "$OCCT_PREFIX" && -n "$OCCT_SOURCE" && -n "$VERSION" && -n "$OUTPUT" ]] || usage
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || usage
[[ -x "$BINARY" && -d "$QT_PREFIX/lib" && -d "$OCCT_PREFIX/lib" && -d "$QT_PREFIX/qml" ]] || usage
[[ -f "$OCCT_SOURCE/LICENSE_LGPL_21.txt" && -f "$OCCT_SOURCE/OCCT_LGPL_EXCEPTION.txt" ]] || usage
command -v "$PATCHELF" >/dev/null
command -v dpkg-deb >/dev/null
command -v dpkg-shlibdeps >/dev/null
command -v convert >/dev/null
command -v rsync >/dev/null
command -v readelf >/dev/null

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ARCH="$(dpkg --print-architecture)"
STAGE="$(dirname "$OUTPUT")/deb-stage"
LIB="$STAGE/usr/lib/timberlab/lib"
BIN_DIR="$STAGE/usr/lib/timberlab/bin"
QML="$STAGE/usr/lib/timberlab/qml"
PLUGINS="$STAGE/usr/lib/timberlab/plugins"
DOC="$STAGE/usr/share/doc/timberlab"
export LC_ALL=C.UTF-8

rm -rf "$STAGE"
mkdir -p "$LIB" "$BIN_DIR" "$QML" "$PLUGINS" "$DOC" "$STAGE/DEBIAN" "$STAGE/usr/bin"

copy_private() {
  local src="$1"
  local resolved soname base
  resolved="$(readlink -f "$src")"
  soname="$(basename "$src")"
  base="$(basename "$resolved")"
  if [[ ! -f "$LIB/$base" ]]; then
    cp -a "$resolved" "$LIB/$base"
  fi
  if [[ "$soname" != "$base" && ! -e "$LIB/$soname" ]]; then
    ln -s "$base" "$LIB/$soname"
  fi
  printf '%s\n' "$resolved"
}

already_have() {
  local resolved="$1"
  [[ -f "$LIB/$(basename "$resolved")" ]]
}

take_needed() {
  local file="$1"
  local path resolved
  while read -r path; do
    [[ -n "$path" && "$path" != "not" ]] || {
      echo "unresolved library from $file" >&2
      exit 1
    }
    case "$path" in
      "$LIB"/*) continue ;;
      "$QT_PREFIX"/*|"$OCCT_PREFIX"/*)
        resolved="$(readlink -f "$path")"
        if already_have "$resolved"; then
          copy_private "$path" >/dev/null
          continue
        fi
        copy_private "$path" >/dev/null
        take_needed "$resolved"
        ;;
      /lib/*|/usr/lib/*|/lib64/*) ;;
      *)
        echo "refusing library outside Qt, OCCT, and the system: $path" >&2
        exit 1
        ;;
    esac
  done < <(LD_LIBRARY_PATH="$QT_PREFIX/lib:$OCCT_PREFIX/lib" ldd "$file" | awk '/=>/ { print $3 }')
}

reject_shipped_name() {
  case "$1" in
    *[Dd]mabuf*|*wayland*|*Wayland*|*qtquick3d*|*Quick3D*)
      echo "refusing to ship $1" >&2
      exit 1
      ;;
  esac
}

install_plugin() {
  local rel="$1"
  local dest="$PLUGINS/$rel"
  mkdir -p "$(dirname "$dest")"
  cp -a "$QT_PREFIX/plugins/$rel" "$dest"
}

install_qml_dir() {
  local rel="$1"
  mkdir -p "$QML/$rel"
  cp -a "$QT_PREFIX/qml/$rel/." "$QML/$rel/"
}

# Resolve private libraries from the unpatched binary. Its build RPATH still
# points at the Qt and OCCT prefixes. The packaged copy is patched afterwards.
take_needed "$BINARY"
cp -a "$BINARY" "$BIN_DIR/timberlab"
"$PATCHELF" --force-rpath --set-rpath '$ORIGIN/../lib' "$BIN_DIR/timberlab"
rpath="$(readelf -d "$BIN_DIR/timberlab" | awk -F'[][]' '/\(RPATH\)/ { print $2; exit }')"
[[ "$rpath" == '$ORIGIN/../lib' ]] || {
  echo "DT_RPATH is '$rpath'" >&2
  exit 1
}

install_plugin platforms/libqxcb.so
install_plugin xcbglintegrations/libqxcb-glx-integration.so
install_plugin xcbglintegrations/libqxcb-egl-integration.so
install_plugin platforminputcontexts/libcomposeplatforminputcontextplugin.so
install_plugin platformthemes/libqxdgdesktopportal.so
install_plugin tls/libqopensslbackend.so
# Material previews are JPEGs. The developer Qt installation can decode them
# through its plugin, but the packaged qt.conf deliberately isolates the
# application from the host plugin directory.
install_plugin imageformats/libqjpeg.so

mkdir -p "$QML/QtQuick"
for name in qmldir plugins.qmltypes libqtquick2plugin.so; do
  cp -a "$QT_PREFIX/qml/QtQuick/$name" "$QML/QtQuick/$name"
done
install_qml_dir QtQml
rm -rf "$QML/QtQml/XmlListModel"
for rel in QtQuick/Layouts QtQuick/Dialogs QtQuick/Window QtQuick/Templates; do
  install_qml_dir "$rel"
done
mkdir -p "$QML/QtQuick/Controls"
rsync -a \
  --exclude '/designer/' \
  --exclude '/Material/' \
  --exclude '/Universal/' \
  --exclude '/Imagine/' \
  --exclude '/FluentWinUI3/' \
  --exclude '/NativeStyle/' \
  "$QT_PREFIX/qml/QtQuick/Controls/" "$QML/QtQuick/Controls/"

while read -r so; do
  reject_shipped_name "$so"
  take_needed "$so"
done < <(find "$STAGE/usr/lib/timberlab" -type f -name '*.so' -o -type f -name '*.so.*')

while read -r path; do
  reject_shipped_name "$path"
done < <(find "$STAGE/usr/lib/timberlab" -print)

cat > "$BIN_DIR/qt.conf" <<'EOF'
[Paths]
Prefix = ..
Plugins = plugins
Qml2Imports = qml
EOF

cat > "$STAGE/usr/bin/timberlab" <<'EOF'
#!/bin/sh
exec /usr/lib/timberlab/bin/timberlab "$@"
EOF
chmod 0755 "$STAGE/usr/bin/timberlab" "$BIN_DIR/timberlab"

for size in 48 64 128 256 512; do
  dest="$STAGE/usr/share/icons/hicolor/${size}x${size}/apps"
  mkdir -p "$dest"
  convert "$ROOT/assets/icons/icon.png" -resize "${size}x${size}" "$dest/timberlab.png"
done
mkdir -p "$STAGE/usr/share/pixmaps"
cp -a "$STAGE/usr/share/icons/hicolor/256x256/apps/timberlab.png" "$STAGE/usr/share/pixmaps/timberlab.png"
mkdir -p "$STAGE/usr/share/applications"
install -m 0644 "$ROOT/assets/timberlab.desktop" "$STAGE/usr/share/applications/timberlab.desktop"

install -m 0644 "$ROOT/packaging/copyright" "$DOC/copyright"
install -m 0644 "$ROOT/LICENSE" "$DOC/LICENSE"
install -m 0644 "$OCCT_SOURCE/LICENSE_LGPL_21.txt" "$DOC/LICENSE_LGPL_21.txt"
install -m 0644 "$OCCT_SOURCE/OCCT_LGPL_EXCEPTION.txt" "$DOC/OCCT_LGPL_EXCEPTION.txt"
gzip -c -n "$ROOT/CHANGELOG.md" > "$DOC/changelog.gz"

check_dir="$STAGE/self-check"
accept_dir="$STAGE/accept"
mkdir -p "$check_dir" "$accept_dir"
check_out="$(env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH \
  TIMBERLAB_DATA_DIR="$check_dir" TIMBERLAB_SOURCE_DIR="$ROOT" \
  "$BIN_DIR/timberlab" --check)"
[[ "$check_out" == "self-check ok" ]] || {
  echo "packaged --check printed: $check_out" >&2
  exit 1
}
accept_out="$(env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH \
  TIMBERLAB_DATA_DIR="$accept_dir" \
  "$BIN_DIR/timberlab" --accept)"
[[ "$accept_out" == accept\ ok:* ]] || {
  echo "packaged --accept printed: $accept_out" >&2
  exit 1
}

mapfile -t objects < <(find "$STAGE/usr/lib/timberlab" -type f \( -name '*.so' -o -name '*.so.*' -o -name timberlab \))
mkdir -p "$STAGE/debian"
cat > "$STAGE/debian/control" <<EOF
Source: timberlab
Package: timberlab
Architecture: $ARCH
EOF
(
  cd "$STAGE"
  dpkg-shlibdeps --ignore-missing-info -l"$LIB" -O "${objects[@]}"
) > "$STAGE/DEBIAN/substvars"
rm -rf "$STAGE/debian"
depends="$(sed -n 's/^shlibs:Depends=//p' "$STAGE/DEBIAN/substvars")"
[[ -n "$depends" ]] || {
  echo "dpkg-shlibdeps produced no Depends" >&2
  exit 1
}
installed="$(du -sk "$STAGE/usr" | awk '{ print $1 }')"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: timberlab
Version: $VERSION
Section: science
Priority: optional
Architecture: $ARCH
Maintainer: Lwb89dev <luigi.antona89@protonmail.com>
Homepage: https://github.com/Lwb89dev/timberlab
Installed-Size: $installed
Depends: $depends
Description: Parametric woodworking CAD
 TimberLab $VERSION builds boards, joints, and assemblies on Linux.
 The package bundles the Qt 6.8.3 and Open CASCADE 7.9.2 shared
 libraries this binary was linked against, and the xcb platform plugin.
 Wayland platform plugins are not included. OpenSSL and SQLite come
 from the system. This is not a finished cabinetmaking tool.
EOF

(
  cd "$STAGE"
  find usr -type f -print0 | sort -z | xargs -0 md5sum
) > "$STAGE/DEBIAN/md5sums"
chmod 0644 "$STAGE/DEBIAN/control" "$STAGE/DEBIAN/md5sums"
rm -rf "$check_dir" "$accept_dir" "$STAGE/DEBIAN/substvars"

mkdir -p "$(dirname "$OUTPUT")"
rm -f "$OUTPUT"
dpkg-deb --root-owner-group -Zxz --build "$STAGE" "$OUTPUT"
echo "wrote $OUTPUT"
