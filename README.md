# TimberLab

**Design. Join. Build.**

TimberLab is a parametric woodworking CAD. This repository is the first running baseline: a Linux desktop application that builds real solids, shows them in a workspace, and saves the project. It is not yet a finished cabinetmaking tool.

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
- Mortise and tenon, half-lap, dado, groove, rabbet, bridle, castle, miter, through dovetail, and sliding dovetail. The cuts are booleans on the solids. A miter and both dovetails are prisms. Each joint keeps its own sizes.
- A parent link and a role token on each part. Assemblies fill the role and leave the parent empty.
- A cut list and a bill of materials. CSV is the cut list. JSON holds the cut list and the bill of materials. The File menu exports them with a zero allowance, so the rough size equals the finished size. The allowance argument can make those columns differ.
- Front, top, and right SVG wires of the selected part, plus one sheet that places those three views. A DXF R12 file and a PDF hold the same edges, in millimetres. Each file says it is a tessellation wire. None of them is a dimensioned drawing, and hidden-line removal is not used.
- Local AES-256-GCM for a library row, and a revision compare. Importing a sealed material or preset writes that library row only when the remote revision wins. A conflict writes nothing. The project file is left byte for byte. The key is passed in and is not written to disk. There is no relay.
- Restore from the `.bak` of the current project. The backup is loaded first. A missing or invalid backup leaves the project file as it is. Opening a damaged project does not restore it on its own.

A demo table is nine boards (four legs, four aprons, one top). Nine project parameters drive their sizes and positions: width, depth, leg, inset, leg height, top thickness, apron height, apron thickness, and apron drop. Changing the width updates the top, the aprons that span that width, and the legs on the far side. Undo restores the previous sizes in one step. The Assembly menu inserts a table, a cabinet carcass, a drawer box, a frame, shelving, or a bedside table into the current project. Each one is ordinary boards plus its own parameters and bindings, so the parts stay editable. A later change does not move the other assemblies. The recipe is not stored, and it does not cut joints.

## What is not implemented

Dimensioned drawings, hidden-line views, STEP, and STL. A Nostr relay, key generation, and any socket. CNC. Android, Windows, and macOS packages. Automatic crash recovery: a failed open does not replace the project with the backup. Those targets are described in `ROADMAP.md`. Do not treat this build as supporting them.

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
