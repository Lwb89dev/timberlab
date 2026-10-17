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
- Mortise and tenon, half-lap, dado, groove, and rabbet. The cuts are booleans on the solids. Each joint keeps its own sizes.

A demo table is nine boards (four legs, four aprons, one top). Nine project parameters drive their sizes and positions: width, depth, leg, inset, leg height, top thickness, apron height, apron thickness, and apron drop. Changing the width updates the top, the aprons that span that width, and the legs on the far side. Undo restores the previous sizes in one step. The Assembly menu inserts a table, a cabinet carcass, or a drawer box into the current project. Each one is ordinary boards plus its own parameters and bindings, so the parts stay editable. A later change does not move the other assemblies. The recipe is not stored, and it does not cut joints.

## What is not implemented

Cut lists, drawings, STEP/STL/DXF/SVG/PDF export, Nostr sync, CNC, and Android, Windows, and macOS packages. Those targets are described in `ROADMAP.md`. Do not treat this build as supporting them.

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
