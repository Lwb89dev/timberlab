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

## Run the window

```bash
TIMBERLAB_DATA_DIR="$PWD/build/gui-data" ./build/timberlab --demo
```

Qt must be able to open a display. This baseline was launched with `QT_QPA_PLATFORM=xcb` on `DISPLAY=:1`. `TIMBERLAB_SCREENSHOT=/path/to.png` grabs the window after 1.5 seconds and quits. That path should stay inside the repository.

## OCCT options used for the vendored build

Modeling only: Foundation, ModelingData, ModelingAlgorithms. Application frameworks, data exchange, Draw, and visualization were off. FreeType, OpenGL, RapidJSON, TBB, VTK, FreeImage, FFmpeg, and OpenVR were off. The libraries the application links are TKernel, TKMath, TKG2d, TKG3d, TKGeomBase, TKBRep, TKGeomAlgo, TKTopAlgo, TKPrim, TKBO, TKShHealing, TKBool, TKHLR, TKFillet, TKOffset, TKFeat, TKMesh, and TKXMesh.

## Unverified platform notes

These notes are not build scripts and have not been tried.

- **Windows.** MSVC needs a 128-bit integer path. The current length code uses `__int128` and stops the compile on a compiler that does not provide it. Qt and OCCT would come from their Windows packages. The viewport uses OpenGL 3.3 on desktop.
- **macOS.** Qt would be the macOS package, and the window must request OpenGL before show because the framebuffer item does not run on Metal. OCCT would be built the same way, without the visualization toolkit. Apple Silicon and Intel are both untested.
- **Android arm64.** There is no NDK setup in this tree, and no tablet has been built. The shader bodies already have an OpenGL ES 3.00 variant, selected when the context is ES, and `Q_OS_ANDROID` requests an ES 3.0 surface instead of a desktop core profile. CMake does not append the Linux `deps/qt/6.8.3/gcc_64` or `deps/occt-install` prefixes when `CMAKE_SYSTEM_NAME` is Android, and it does not pass `--disable-new-dtags` except on Linux. An Android configure still needs its own Qt and OCCT prefixes. Qt Quick 3D stays out: it is not in this install, and Qt lists it as GPL rather than LGPL. Do not mark Android supported until an arm64 package has been installed and used on a tablet.
