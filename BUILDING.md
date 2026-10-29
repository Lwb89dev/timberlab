# Building

Linux x86_64 is the only platform this tree has been configured, compiled, and run on. Windows, macOS, and Android are not supported builds. Sketches of what those builds will need are at the bottom of this file. They have not been executed.

## What this machine used

- g++ 13.3, CMake 3.28, Ninja
- Qt 6.8.3 desktop, unpacked under `deps/qt/6.8.3/gcc_64` with `aqtinstall` (archives: icu, qtbase, qtdeclarative, qtsvg, qttools, qttranslations, qtwayland)
- Open CASCADE 7.9.2 under `deps/occt-install`, built with modeling libraries only. Visualization, Draw, and data exchange were turned off.
- System OpenSSL (`libcrypto`), SQLite 3.45, and a desktop OpenGL 3.3 driver
- Catch2 3.6.0, fetched by CMake at configure time

`deps/` is gitignored. A fresh clone does not contain Qt or OCCT.

CMake looks for those prefixes when they exist:

- `deps/qt/6.8.3/gcc_64`
- `deps/occt-install`

You can pass another `CMAKE_PREFIX_PATH`. `find_package` still has to see Qt6 (Core, Gui, Qml, Quick, OpenGL, QuickControls2, LinguistTools), OpenCASCADE, OpenSSL, and SQLite3.

## Configure and build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
```

Binaries:

- `build/timberlab`
- `build/timberlab-tests`

The executables record an RPATH to the OCCT and Qt library directories. The link uses `DT_RPATH` rather than `DT_RUNPATH`, because the OCCT shared libraries do not carry a run path of their own and `DT_RUNPATH` is not consulted for their dependencies.

## Tests

```bash
./build/timberlab-tests
TIMBERLAB_DATA_DIR="$PWD/build/self-check" TIMBERLAB_SOURCE_DIR="$PWD" ./build/timberlab --check
TIMBERLAB_DATA_DIR="$PWD/build/accept" ./build/timberlab --accept
```

Both were run successfully on this baseline. See `TESTING.md`.

## Timings

```bash
./build/timberlab --bench
./build/timberlab --bench-project build/bench/frames-100.timberlab 100
TIMBERLAB_FRAME_BENCH=5 QT_QPA_PLATFORM=xcb ./build/timberlab build/bench/frames-100.timberlab
```

`--bench` prints save, load, tessellation, edit, and export times for generated projects, with no window. `--bench-project` writes a grid of boards with joints. `TIMBERLAB_FRAME_BENCH` opens a window on that project, turns the swap interval off, orbits the camera one step per swapped frame for that many seconds, and prints the frame rate. It needs a display.

## Run the window

```bash
TIMBERLAB_DATA_DIR="$PWD/build/gui-data" ./build/timberlab --demo
```

Qt must be able to open a display. This baseline was launched with `QT_QPA_PLATFORM=xcb` on `DISPLAY=:1`. `TIMBERLAB_SCREENSHOT=/path/to.png` grabs the window after 1.5 seconds and quits. That path should stay inside the repository.

## Debian package

`.github/workflows/package.yml` builds `timberlab_<version>_amd64.deb` on a version tag. `packaging/assemble-deb.sh` copies the linked binary, the Qt and Open CASCADE shared libraries it needs, the QML modules the workspace imports, and the xcb plugin. It does not copy Wayland platform plugins. A local `./build/timberlab` is the development binary. It is not installed under `/usr`.

## Panel icon

The process loads `assets/icons/icon.png` and calls `QGuiApplication::setDesktopFileName("timberlab")`. That base name matches `assets/timberlab.desktop`. A Wayland panel resolves `Icon=timberlab` from the icon theme. Copy the desktop file to `~/.local/share/applications/` and scaled PNGs to `~/.local/share/icons/hicolor/<size>/apps/timberlab.png` (48, 64, 128, 256, and 512). Then run `update-desktop-database` on the applications directory. `gtk-update-icon-cache` writes `icon-theme.cache` only when that hicolor directory already has an `index.theme`. Do not add a partial index there: the first `index.theme` on the data path replaces the system one. Without a local index, the system hicolor index still names these sizes, and GTK finds the PNGs under `~/.local/share`. The tracked desktop file keeps `Exec=timberlab`. A machine-local copy can point `Exec` at `build/timberlab`. A checkout does not install files under `/usr`. The `.deb` from the version-tag workflow does.

## OCCT options used for the vendored build

Foundation, ModelingData, ModelingAlgorithms, and DataExchange. The DataExchange module is there for the STEP writer: without it the program still builds, and a STEP export says it is unavailable (the Android build is made without it). Visualization, Draw, and the OCCT DETools were off. OCCT's own application framework toolkits are built only because the STEP writer needs two of them (TKXCAF and TKLCAF). FreeType, OpenGL, RapidJSON, TBB, VTK, FreeImage, FFmpeg, and OpenVR were off. The libraries the application links are TKernel, TKMath, TKG2d, TKG3d, TKGeomBase, TKBRep, TKGeomAlgo, TKTopAlgo, TKPrim, TKBO, TKShHealing, TKBool, TKHLR, TKFillet, TKOffset, TKFeat, TKMesh, and TKXMesh, plus TKDESTEP, TKDE, TKXSBase, TKXCAF, and TKLCAF when the install has TKDESTEP. CMake names them one by one. It does not take every toolkit an install happens to hold.

An older `deps/occt-install` built without DataExchange still works, with no STEP. To add the module to the existing build tree: `cmake -S deps/OCCT-7_9_2 -B deps/occt-build -DBUILD_MODULE_DataExchange=ON`, then `cmake --build deps/occt-build --target install`. That rebuilds every toolkit.

## Android

`packaging/build-android.sh` builds an arm64 package. It is signed with the standard debug key as `build/android/timberlab-<version>-arm64-debug.apk`, or with a release key as `timberlab-<version>-arm64.apk` when `ANDROID_KEYSTORE` (the file), `ANDROID_KEY_ALIAS` (default `timberlab`), and `ANDROID_KEYSTORE_PASS` are in the environment. The password reaches `apksigner` through the environment only. The keystore is not in git. Run it from the repository root. It needs `sdkmanager`, a JDK 17, `perl`, `curl`, and `unzip` on the path, and `deps/venv/bin/aqt` for Qt. It installs everything else under `deps/` and `build/`, both gitignored. Gradle's cache and the Android tools' preferences are kept in `deps/gradle-home` and `deps/android-user-home`, not in the home directory.

Stages, each skipped when its output exists: `sdk` (SDK platform 34, build-tools 34.0.0, NDK 26.1.10909125, after accepting the SDK licenses), `qt` (Qt 6.8.3 for `android_arm64_v8a`, with the host tools taken from `gcc_64`), `occt` (Open CASCADE 7.9.2 for the NDK, shared), `openssl` (OpenSSL 3.0.22, shared, built with a `_3` library suffix because Qt's TLS backend loads `libssl_3.so` and `libcrypto_3.so`), `sqlite` (the 3.45.1 amalgamation as a static library), `apk` (CMake and `androiddeployqt`), and `sign` (zipalign and a debug key kept in `deps/android-debug.keystore`). The sources of OpenSSL and SQLite are fetched over HTTPS. The OpenSSL archive is checked against its published SHA-256. The SQLite archive is not checked against a published digest.

The package holds the Qt libraries, the QML plugins, the JPEG plugin, the Open CASCADE libraries, and OpenSSL. API 28 is the minimum and 34 the target. The package name is `local.timberlab.app`. `packaging/android/` holds the manifest (Qt's template with the icon and the name TimberLab added) and the launcher icon. The icon is an adaptive icon: an orange gradient background, and as the foreground a black squircle carrying the saw blade, which is cut out of `assets/icons/icon.png` by brightness. The squircle sits inside the safe zone, so the gradient shows around it as an outline and no launcher mask clips a corner. `packaging/android/make-icons.py` rewrites the icon files from the artwork and needs Pillow. The CMake `ANDROID` block adds the Open CASCADE and OpenSSL libraries to the package. `ViewportItem.cpp` includes the GLES 3 header on Android, and `main.cpp` points the data folder at the app's private storage, because an Android process has no `HOME`.

This APK has been built and its structure checked (`aapt dump badging`, `apksigner verify`, the libraries inside). It has not been installed on a device or an emulator, so nothing in it has been run. The interface is the desktop one. The file dialogs on Android return `content://` addresses, and the project save and open take file paths, so they are expected not to work until that is handled.

```bash
packaging/build-android.sh
adb install -r build/android/timberlab-0.5.0-arm64-debug.apk
```

## Windows

`.github/workflows/windows.yml` builds a Windows x64 zip on a `windows-2022` runner, on a version tag or a manual run. It installs Qt 6.8.3 for MSVC 2022 with `aqtinstall`, builds Open CASCADE 7.9.2 with the modeling libraries only (cached), installs OpenSSL and SQLite with vcpkg, builds this tree with Ninja and `cl`, and runs the tests, `--check`, and `--accept`. It then runs `windeployqt`, copies the Open CASCADE, OpenSSL, and SQLite libraries and the license texts, runs `--check` and `--accept` from the package with only the Windows folders on `PATH`, zips it, and attaches the zip to the release on a tag.

MSVC compiled this tree on the runner, and a manual run passed the tests, `--check`, `--accept`, and the isolated run of the package. Nobody has opened the window on a Windows desktop. These changes were made for it without a Windows compiler, before that run: the file calls in `src/storage/archive.cpp` have a C runtime branch (no `O_NOFOLLOW` and no folder `fsync` there), `src/platform/paths.cpp` reads `LOCALAPPDATA`, the warning flags are chosen by compiler, and two tests skip the POSIX permission and symlink checks. Windows 10 and 11 on x64 are the only target. Windows on Arm is not.

## macOS

Not built and not set up. Qt would be the macOS package, and the window must request OpenGL before show because the framebuffer item does not run on Metal. OCCT would be built the same way, without the visualization toolkit. Apple Silicon and Intel are both untested.
