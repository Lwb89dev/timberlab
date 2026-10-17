# TimberLab

**Design. Join. Build.**

TimberLab is a parametric woodworking CAD. Release 0.1.0 is the first tagged Linux desktop build: it constructs real solids, shows them in a workspace, and saves the project. It is not yet a finished cabinetmaking tool. The notes for this version are in `CHANGELOG.md`.

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
- Mortise and tenon, half-lap, dado, groove, rabbet, bridle, castle, miter, through dovetail, sliding dovetail, finger joint, half-blind dovetail, tongue and groove, and dowel. The cuts are booleans on the solids. A miter and the dovetails are prisms. A dowel is a cylindrical hole. Each joint keeps its own sizes.
- A parent link and a role token on each part. Assemblies fill the role and leave the parent empty.
- A cut list and a bill of materials. CSV is the cut list. JSON holds the cut list and the bill of materials. The File menu exports them with a zero allowance, so the rough size equals the finished size. The allowance argument can make those columns differ.
- Front, top, and right SVG wires of the selected part, plus one sheet that places those three views. A DXF R12 file and a PDF hold the same edges, in millimetres. Each of those files says it is a tessellation wire. A separate front SVG adds the model's overall width and height, in millimetres, taken from the part. It does not dimension joints, and it is not hidden-line removal.
- Local AES-256-GCM for a library row, and a revision compare. Importing a sealed material or preset writes that library row only when the remote revision wins. A conflict writes nothing. The project file is left byte for byte. The key is passed in and is not written to disk.
- Desktop sign-in with an nsec kept in memory for the session, or with a bunker link. The nsec is not saved and is not sent to a relay. Bunker sign-in finishes only after the remote signer returns a public key. On this Linux build, Amber reports that it is an Android signer and does not contact a phone. Signing in does not publish the project.
- With an nsec signed in, the File menu can write or read a library-event file. Each row is a kind 30078 event, NIP-44 ciphertext for that same key. Import updates a library row when the remote revision wins. It does not change the project file. Bunker and Amber cannot seal that file, because they do not hold the secret. No run of this tree sent an event to a relay.
- Restore from the `.bak` of the current project. The backup is loaded first. A missing or invalid backup leaves the project file as it is. Opening a damaged project does not restore it on its own.

A demo table is nine boards (four legs, four aprons, one top). Nine project parameters drive their sizes and positions: width, depth, leg, inset, leg height, top thickness, apron height, apron thickness, and apron drop. Changing the width updates the top, the aprons that span that width, and the legs on the far side. Undo restores the previous sizes in one step. The Assembly menu inserts a table, a cabinet carcass, a drawer box, a frame, shelving, or a bedside table into the current project. Each one is ordinary boards plus its own parameters and bindings, so the parts stay editable. A later change does not move the other assemblies. The recipe is not stored, and it does not cut joints.

## Debian package

`.github/workflows/package.yml` runs when a version tag is pushed. For `0.1.0` the asset is `timberlab_0.1.0_amd64.deb`, attached to that GitHub release when the job succeeds. The runner is Ubuntu 24.04. It installs Qt 6.8.3, builds Open CASCADE 7.9.2 with the modeling libraries only, compiles this tree, and runs the test suite, `--check`, and `--accept` before it packs the archive.

The package installs `/usr/bin/timberlab`, the desktop file, and the icon. The program and the Qt and Open CASCADE shared libraries live under `/usr/lib/timberlab`. OpenSSL and SQLite stay the system libraries. The dependency names are those of Ubuntu 24.04, including `libssl3t64` and `libglib2.0-0t64`. The xcb platform plugin is included. The Wayland platform plugins are not, so a Wayland session uses XWayland. Android, Windows, and macOS packages are not built.

```bash
sudo apt install ./timberlab_0.1.0_amd64.deb
```

The TimberLab source for that binary is the git tag. The Qt 6.8.3 and Open CASCADE 7.9.2 source archives are named in `packaging/copyright`, which the package installs under `/usr/share/doc/timberlab/`.

## What is not implemented

Feature dimensions and hidden-line views, STEP, and STL. A published project, and any relay that stores one. Haunched tenons, biscuits, and pocket screws. CNC. Android, Windows, and macOS packages. A live Amber session has not been run, and a live bunker session was not run for this change. Automatic crash recovery: a failed open does not replace the project with the backup. Those targets are described in `ROADMAP.md`. Do not treat this build as supporting them.

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
