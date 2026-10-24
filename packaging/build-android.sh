#!/usr/bin/env bash
# Build the TimberLab arm64 APK. Run from the repository root.
#
# Everything lands under deps/ and build/android, both gitignored:
#   deps/android-sdk              Android SDK, platform 34, build-tools 34, NDK 26.1
#   deps/qt/6.8.3/android_arm64_v8a   Qt for Android (host tools come from gcc_64)
#   deps/occt-android-install     Open CASCADE, built with the NDK
#   deps/openssl-android          OpenSSL 3, shared. Qt's TLS backend loads libssl_3.so at run time.
#   deps/sqlite-android           SQLite amalgamation, static
#
# Stages: sdk | qt | occt | openssl | sqlite | apk | sign. With no argument all run.
# A stage whose output already exists is skipped.
set -euo pipefail

# The runner's own SDK is only a source for sdkmanager. This script installs its
# own SDK under deps/ and then points ANDROID_HOME there.
RUNNER_ANDROID_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS="${ROOT}/deps"
SDK="${DEPS}/android-sdk"
NDK_VERSION="26.1.10909125"
NDK="${SDK}/ndk/${NDK_VERSION}"
API=28
QT_VERSION="6.8.3"
QT_HOST="${DEPS}/qt/${QT_VERSION}/gcc_64"
QT_ANDROID="${DEPS}/qt/${QT_VERSION}/android_arm64_v8a"
OCCT_SRC="${DEPS}/OCCT-7_9_2"
OCCT_SHA256="3cd080d3fc33ba0c6c157e110afe3e015859524c4694dbb09812ec9d61595639"
OCCT_PREFIX="${DEPS}/occt-android-install"
OPENSSL_VERSION="3.0.22"
OPENSSL_SHA256="67ebca7e50d17383028045486653492195b83db95f8558709701bb47b5c1ef81"
OPENSSL_PREFIX="${DEPS}/openssl-android"
SQLITE_ZIP_URL="https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip"
SQLITE_DIR_NAME="sqlite-amalgamation-3450100"
SQLITE_PREFIX="${DEPS}/sqlite-android"
BUILD="${ROOT}/build/android"
TOOLCHAIN="${NDK}/toolchains/llvm/prebuilt/linux-x86_64"
JOBS="$(nproc)"

# sdkmanager and aqt come from the path, or from the places this checkout and
# a GitHub runner keep them.
find_tool() {
  local name="$1"
  shift
  if command -v "${name}" > /dev/null; then command -v "${name}"; return; fi
  local candidate
  for candidate in "$@"; do
    if [[ -x "${candidate}" ]]; then echo "${candidate}"; return; fi
  done
  echo "${name} not found" >&2
  return 1
}

stage_sdk() {
  [[ -d "${NDK}" ]] && return
  local sdkmanager
  sdkmanager="$(find_tool sdkmanager "${RUNNER_ANDROID_HOME:-/nonexistent}/cmdline-tools/latest/bin/sdkmanager" \
    /usr/local/lib/android/sdk/cmdline-tools/latest/bin/sdkmanager)"
  mkdir -p "${SDK}"
  yes | "${sdkmanager}" --sdk_root="${SDK}" --licenses > /dev/null || true
  "${sdkmanager}" --sdk_root="${SDK}" "platform-tools" "platforms;android-34" "build-tools;34.0.0" "ndk;${NDK_VERSION}"
}

# Qt for Android needs the desktop Qt of the same version for moc, rcc, qsb, and
# androiddeployqt.
stage_qt() {
  local aqt
  aqt="$(find_tool aqt "${DEPS}/venv/bin/aqt")"
  if [[ ! -d "${QT_HOST}" ]]; then
    "${aqt}" install-qt linux desktop "${QT_VERSION}" linux_gcc_64 -O "${DEPS}/qt" \
      --archives icu qtbase qtdeclarative qtsvg qttools qttranslations qtshadertools
  fi
  [[ -d "${QT_ANDROID}" ]] && return
  "${aqt}" install-qt linux android "${QT_VERSION}" android_arm64_v8a -O "${DEPS}/qt" \
    --archives qtbase qtdeclarative qtsvg qttools qttranslations qtshadertools
}

stage_occt() {
  [[ -f "${OCCT_PREFIX}/lib/cmake/opencascade/OpenCASCADEConfig.cmake" ]] && return
  if [[ ! -f "${OCCT_SRC}/LICENSE_LGPL_21.txt" ]]; then
    local work="${DEPS}/downloads/occt"
    mkdir -p "${work}"
    curl -fsSL -o "${work}/occt.tar.gz" https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V7_9_2.tar.gz
    echo "${OCCT_SHA256}  ${work}/occt.tar.gz" | sha256sum -c
    tar -xzf "${work}/occt.tar.gz" -C "${DEPS}"
  fi
  cmake -S "${OCCT_SRC}" -B "${DEPS}/occt-android-build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${NDK}/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM="android-${API}" -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_LIBRARY_TYPE=Shared \
    -DINSTALL_DIR="${OCCT_PREFIX}" -DINSTALL_DIR_LAYOUT=Unix \
    -DBUILD_MODULE_FoundationClasses=ON -DBUILD_MODULE_ModelingData=ON -DBUILD_MODULE_ModelingAlgorithms=ON \
    -DBUILD_MODULE_Visualization=OFF -DBUILD_MODULE_ApplicationFramework=OFF -DBUILD_MODULE_DataExchange=OFF \
    -DBUILD_MODULE_Draw=OFF -DBUILD_MODULE_DETools=OFF -DBUILD_DOC_Overview=OFF -DBUILD_Inspector=OFF \
    -DBUILD_SAMPLES_QT=OFF -DUSE_FREETYPE=OFF -DUSE_FREEIMAGE=OFF -DUSE_FFMPEG=OFF -DUSE_OPENGL=OFF \
    -DUSE_GLES2=OFF -DUSE_RAPIDJSON=OFF -DUSE_DRACO=OFF -DUSE_TBB=OFF -DUSE_TK=OFF -DUSE_EIGEN=OFF
  cmake --build "${DEPS}/occt-android-build" --target install
}

stage_openssl() {
  [[ -f "${OPENSSL_PREFIX}/lib/libcrypto_3.so" ]] && return
  local work="${DEPS}/downloads/openssl"
  mkdir -p "${work}"
  if [[ ! -f "${work}/openssl-${OPENSSL_VERSION}.tar.gz" ]]; then
    curl -fsSL -o "${work}/openssl-${OPENSSL_VERSION}.tar.gz" \
      "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz"
  fi
  echo "${OPENSSL_SHA256}  ${work}/openssl-${OPENSSL_VERSION}.tar.gz" | sha256sum -c
  rm -rf "${DEPS}/android-src/openssl"
  mkdir -p "${DEPS}/android-src/openssl"
  tar -xzf "${work}/openssl-${OPENSSL_VERSION}.tar.gz" -C "${DEPS}/android-src/openssl" --strip-components=1
  (
    cd "${DEPS}/android-src/openssl"
    export ANDROID_NDK_ROOT="${NDK}"
    export PATH="${TOOLCHAIN}/bin:${PATH}"
    # Qt's TLS backend loads libssl_3.so and libcrypto_3.so. OpenSSL names the
    # Android libraries without that suffix unless the target says otherwise.
    cat > timberlab-android.conf <<'CONF'
my %targets = (
  "android-v3-arm64" => {
    inherit_from => [ "android-arm64" ],
    shlib_variant => "_3",
  },
);
CONF
    ./Configure --config=timberlab-android.conf android-v3-arm64 -D__ANDROID_API__="${API}" shared no-tests \
      --prefix="${OPENSSL_PREFIX}" --openssldir="${OPENSSL_PREFIX}/ssl"
    make -j"${JOBS}" build_libs
    make install_dev
  )
}

stage_sqlite() {
  [[ -f "${SQLITE_PREFIX}/lib/libsqlite3.a" ]] && return
  local work="${DEPS}/downloads/sqlite"
  mkdir -p "${work}" "${SQLITE_PREFIX}/include" "${SQLITE_PREFIX}/lib"
  if [[ ! -f "${work}/${SQLITE_DIR_NAME}.zip" ]]; then
    curl -fsSL -o "${work}/${SQLITE_DIR_NAME}.zip" "${SQLITE_ZIP_URL}"
  fi
  rm -rf "${DEPS}/android-src/sqlite"
  mkdir -p "${DEPS}/android-src/sqlite"
  unzip -q -o "${work}/${SQLITE_DIR_NAME}.zip" -d "${DEPS}/android-src/sqlite"
  local src="${DEPS}/android-src/sqlite/${SQLITE_DIR_NAME}"
  "${TOOLCHAIN}/bin/aarch64-linux-android${API}-clang" -O2 -fPIC -DSQLITE_THREADSAFE=1 \
    -c "${src}/sqlite3.c" -o "${src}/sqlite3.o"
  "${TOOLCHAIN}/bin/llvm-ar" rcs "${SQLITE_PREFIX}/lib/libsqlite3.a" "${src}/sqlite3.o"
  cp "${src}/sqlite3.h" "${src}/sqlite3ext.h" "${SQLITE_PREFIX}/include/"
}

stage_apk() {
  cmake -S "${ROOT}" -B "${BUILD}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${QT_ANDROID}/lib/cmake/Qt6/qt.toolchain.cmake" \
    -DQT_HOST_PATH="${QT_HOST}" \
    -DANDROID_SDK_ROOT="${SDK}" -DANDROID_NDK_ROOT="${NDK}" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM="android-${API}" -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DCMAKE_FIND_ROOT_PATH="${OCCT_PREFIX};${OPENSSL_PREFIX};${SQLITE_PREFIX}" \
    -DCMAKE_PREFIX_PATH="${OCCT_PREFIX};${OPENSSL_PREFIX};${SQLITE_PREFIX}" \
    -DOPENSSL_ROOT_DIR="${OPENSSL_PREFIX}" \
    -DSQLite3_INCLUDE_DIR="${SQLITE_PREFIX}/include" -DSQLite3_LIBRARY="${SQLITE_PREFIX}/lib/libsqlite3.a"
  cmake --build "${BUILD}" --target apk
}

# A debug-signed package, so a tablet accepts it. The key lives in deps/ and is
# the standard Android debug key. It is not a release signature.
stage_sign() {
  local version
  version="$(sed -n 's/^project(TimberLab VERSION \([0-9.]*\) .*/\1/p' "${ROOT}/CMakeLists.txt")"
  local unsigned="${BUILD}/android-build/build/outputs/apk/release/android-build-release-unsigned.apk"
  local aligned="${BUILD}/timberlab-${version}-arm64-aligned.apk"
  local signed="${BUILD}/timberlab-${version}-arm64-debug.apk"
  local keystore="${DEPS}/android-debug.keystore"
  local tools="${SDK}/build-tools/34.0.0"
  if [[ ! -f "${keystore}" ]]; then
    keytool -genkeypair -keystore "${keystore}" -storepass android -keypass android -alias androiddebugkey \
      -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
  fi
  "${tools}/zipalign" -f -p 4 "${unsigned}" "${aligned}"
  "${tools}/apksigner" sign --ks "${keystore}" --ks-pass pass:android --key-pass pass:android \
    --ks-key-alias androiddebugkey --out "${signed}" "${aligned}"
  "${tools}/apksigner" verify "${signed}"
  rm -f "${aligned}"
  echo "${signed}"
}

export ANDROID_SDK_ROOT="${SDK}"
export ANDROID_HOME="${SDK}"
# Keep Gradle's cache and the Android tools' preferences inside the checkout
# instead of the home directory.
export GRADLE_USER_HOME="${DEPS}/gradle-home"
export ANDROID_USER_HOME="${DEPS}/android-user-home"
mkdir -p "${GRADLE_USER_HOME}" "${ANDROID_USER_HOME}"

if [[ $# -eq 0 ]]; then
  set -- sdk qt occt openssl sqlite apk sign
fi
for stage in "$@"; do
  echo "== ${stage}"
  "stage_${stage}"
done
