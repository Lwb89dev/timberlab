# TimberLab

**Design. Join. Build.**

TimberLab is a parametric woodworking CAD. Release 0.2.0 is the current tagged Linux desktop build: it constructs real solids, shows them in a workspace, and saves the project. It is not yet a finished cabinetmaking tool. The notes for this version are in `CHANGELOG.md`.

## What works now

- Qt 6 workspace with a dark graphite theme, a parts list, a property inspector, and a 3D viewport. The window icon is `assets/icons/icon.png`. The Wayland app id is `timberlab`, the same base name as `assets/timberlab.desktop`.
- Open CASCADE solids for a board, a box, and a cylinder. The picture on screen is a mesh generated from that solid.
- Orbit, pan, zoom, and standard views (top, front, right, isometric) from the View menu. Fit frames the model.
- Click a part to select it. Edit width, thickness, height, radius, position, and rotation as numbers.
- Metric and imperial display. Lengths are stored as integer nanometres. Changing the display unit does not change the stored size. Input accepts `19.05 mm`, `1,5 cm`, `3/4"`, and `3' 6 1/4"`.
- A shipped material catalog (46 wood species plus sheet goods) and thickness presets, including nominal versus surfaced North American lumber.
- Assign a material. The viewport uses a flat stylized color so species stay distinguishable.
- Undo and redo.
- Save and reopen a `.timberlab` project (a zip with a checksum). A raw model JSON file also loads.
- SQLite preferences, separate from the project.
- A user library of custom materials and thickness presets in its own SQLite file. The project still stores only a material id.
- Italian interface when the system language is Italian. English is the source language.
- Automated tests for units, documents, solids, storage, the user library, the catalog, joinery, parameter propagation, and assemblies.
- Mortise and tenon, half-lap, dado, groove, rabbet, bridle, castle, miter, through dovetail, sliding dovetail, finger joint, half-blind dovetail, tongue and groove, dowel, haunched tenon, wedged tenon, loose tenon, cross lap, end lap, splined miter, biscuit, and pocket screw. The cuts are booleans on the solids. A miter and the dovetails are prisms. A dowel and a biscuit kerf are cylinders. A pocket screw is an angled stepped hole. Each joint keeps its own sizes.
- A parent link and a role token on each part. Assemblies fill the role and leave the parent empty.
- A cut list and a bill of materials. CSV is the cut list. JSON holds the cut list and the bill of materials. The File menu exports them with a zero allowance, so the rough size equals the finished size. The allowance argument can make those columns differ.
- A hidden-line sheet of the selected part as SVG, DXF, or PDF: front, right, and top views from OCCT's hidden-line removal on the exact solid, visible edges solid and hidden edges dashed. Every view carries feature dimensions in millimetres: the size and position of each cut along the view's axes, chained from the part's corner, and the overall size. They are read from the cuts the joints store. A cut that is not parallel to a view axis is dimensioned by its bounding box.
- STL (binary) and STEP (AP214) export of the whole project at its placements or of the selected part in its own frame. STEP needs an Open CASCADE built with data exchange, which the Linux package and the Windows build have and the Android build does not. Each STEP part is one named solid. The wire, front-dimension, and sheet files below are still the tessellation.
- Front, top, and right SVG wires of the selected part, plus one sheet that places those three views. A DXF R12 file and a PDF hold the same edges, in millimetres. Each of those files says it is a tessellation wire. A separate front SVG adds the model's overall width and height, in millimetres, taken from the part. It does not dimension joints, and it is not hidden-line removal.
- Local AES-256-GCM for a library row, and a revision compare. Importing a sealed material or preset writes that library row only when the remote revision wins. A conflict writes nothing. The project file is left byte for byte. The key is passed in and is not written to disk.
- Desktop sign-in with an nsec kept in memory for the session, or with a bunker link. The nsec is not saved and is not sent to a relay. Bunker sign-in finishes only after the remote signer returns a public key. On this Linux build, Amber reports that it is an Android signer and does not contact a phone. Signing in does not publish the project.
- With an nsec signed in, the File menu can write or read a library-event file. Each row is a kind 30078 event, NIP-44 ciphertext for that same key. Import updates a library row when the remote revision wins. It does not change the project file. Bunker and Amber cannot seal that file, because they do not hold the secret. No run of this tree sent an event to a relay.
- A Joinery button in the toolbar. Select two touching boards, press it, and pick a joint. The menu lists only the types that fit that pair, and the cut goes on the faces that touch.
- Restore from the `.bak` of the current project. The backup is loaded first. A missing or invalid backup leaves the project file as it is. Opening a damaged project does not restore it on its own.

A demo table is nine boards (four legs, four aprons, one top). Nine project parameters drive their sizes and positions: width, depth, leg, inset, leg height, top thickness, apron height, apron thickness, and apron drop. Changing the width updates the top, the aprons that span that width, and the legs on the far side. Undo restores the previous sizes in one step. The Assembly menu inserts a table, a cabinet carcass, a drawer box, a frame, shelving, or a bedside table into the current project. Each one is ordinary boards plus its own parameters and bindings, so the parts stay editable. A later change does not move the other assemblies. The recipe is not stored, and it does not cut joints.

## Debian package

`.github/workflows/package.yml` runs when a version tag is pushed. For `0.2.0` the asset is `timberlab_0.2.0_amd64.deb`, attached to that GitHub release when the job succeeds. The runner is Ubuntu 24.04. It installs Qt 6.8.3, builds Open CASCADE 7.9.2 with the modeling libraries only, compiles this tree, and runs the test suite, `--check`, and `--accept` before it packs the archive.

The package installs `/usr/bin/timberlab`, the desktop file, and the icon. The program and the Qt and Open CASCADE shared libraries live under `/usr/lib/timberlab`. OpenSSL and SQLite stay the system libraries. The dependency names are those of Ubuntu 24.04, including `libssl3t64` and `libglib2.0-0t64`. The xcb platform plugin is included. The Wayland platform plugins are not, so a Wayland session uses XWayland. Windows and macOS packages are not built by this workflow.

## Android and Windows

`packaging/build-android.sh` builds a debug-signed arm64 APK (`build/android/timberlab-<version>-arm64-debug.apk`). It downloads the Android SDK and NDK, Qt for Android, and sources for Open CASCADE, OpenSSL, and SQLite into `deps/`, and builds them with the NDK. That APK has been built here and by the Android workflow on a clean runner. The release APK is signed with a release key when `ANDROID_KEYSTORE` and `ANDROID_KEYSTORE_PASS` are set. It has not been installed on a device or an emulator. The interface is the desktop one. Touch input and the Android file dialogs have not been tried, and the file dialogs return `content://` addresses that the project save and open do not handle.

`.github/workflows/windows.yml` builds a Windows x64 zip with MSVC on a version tag or a manual run, runs the tests and an isolated run of the packaged program, and attaches the zip to the release. A manual run on 2026-10-24 compiled the tree with MSVC, passed the tests, `--check`, and `--accept`, and ran the package on its own. Nobody has opened the window on a Windows desktop.

```bash
sudo apt install ./timberlab_0.2.0_amd64.deb
```

The TimberLab source for that binary is the git tag. The Qt 6.8.3 and Open CASCADE 7.9.2 source archives are named in `packaging/copyright`, which the package installs under `/usr/share/doc/timberlab/`.

## Measured performance

One run on the development machine, 2026-10-24, with `timberlab --bench` (no window) and `TIMBERLAB_FRAME_BENCH` (a window). Boards are 300 × 18 × 400 mm on a grid, and every fourth board carries a mortise and tenon. The medians are of 5 save and load runs and 3 re-mesh runs. These numbers are for that machine and say nothing about a tablet.

| Boards | Joints | Save | Load | Re-mesh every part | File |
|---:|---:|---:|---:|---:|---:|
| 10 | 3 | 5.8 ms | 0.2 ms | 45 ms | 1.2 KiB |
| 100 | 25 | 6.8 ms | 1.3 ms | 380 ms | 5.2 KiB |
| 500 | 125 | 14 ms | 6.9 ms | 1.9 s | 24 KiB |
| 1000 | 250 | 33 ms | 15 ms | 3.5 s | 47 KiB |

Save includes the checksum, the temporary file, and `fsync`. The re-mesh column is the cost of tessellating every part, which the editor did after every edit until this release. It now keeps each part's tessellation and tessellates again only the parts whose size or joints changed, or whose joined part changed. Through the real editor, an edit to a jointed board took 351 ms with 100 boards and 1.75 s with 500 before that change, and 14 ms and 17 ms after it. Opening a project still tessellates everything once: 0.35 s for 100 boards and 1.8 s for 500. Adding boards one at a time took 2.9 s for 500 boards and 11 s for 1000, because each addition snapshots the history. Exports of the 100-board project: STL 0.35 s, STEP 0.83 s (2.6 MB), and the hidden-line sheet of one jointed board 28 ms.

Frame rate, with the swap interval set to 0 so it is not held to the refresh rate, a 1280 × 732 window, and the camera orbiting one step per frame: 180 frames/s (5.6 ms) with 9 parts, 167 frames/s (6.0 ms) with 100 parts, and 119 frames/s (8.4 ms) with 500 parts. The machine has an NVIDIA GeForce RTX 5070 Mobile and an AMD Raphael integrated GPU. The log does not say which one drew. The window ran under X11 (xcb). Nothing here measures a tablet, Windows, or macOS.

## What is not implemented

Tolerances, section views, and a title block on the drawings. STEP with colours or assemblies: parts are plain named solids. A published project, and any relay that stores one. CNC. A macOS package. An Android or a Windows package that has been installed and used: the Android APK and the Windows zip build on their runners, and neither has been started on a device or a desktop. A live Amber session has not been run, and a live bunker session was not run for this change. Automatic crash recovery: a failed open does not replace the project with the backup. Those targets are described in `ROADMAP.md`. Do not treat this build as supporting them.

## Run it

From a configured build tree (see `BUILDING.md`):

```bash
./build/timberlab
./build/timberlab progetto.timberlab
./build/timberlab --demo
./build/timberlab --check
./build/timberlab --accept
./build/timberlab-tests
```

`--check` and `--accept` are headless. They do not open a window. Set `TIMBERLAB_DATA_DIR` to a directory inside this repository if you do not want preferences written under `~/.local/share/timberlab`. Set `TIMBERLAB_SOURCE_DIR` to the repository root so `--check` also reads the shipped catalog.

`--accept` drives the editor controller: two boards, exact millimetre and inch text, a locked edit that must fail, undo, save, reopen, then one more edit saved back to the same file. It checks the stored nanometres and the tessellation rebuilt from the OCCT solid. It does not click the QML fields.

## Axes

Z is up. On a board, X is width, Y is thickness, and Z is height. A tabletop therefore has a small height (its vertical thickness) and a large Y size (its depth). The inspector labels say this explicitly.

## License

The application text is GPL-3.0-or-later. See `LICENSE` and `LICENSES.md`. That choice is recorded with the dependency constraints. It has not had a separate legal review.
