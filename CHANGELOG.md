# Changelog

## 0.1.0 — 2026-10-17

First tagged release. Linux x86_64 is the only platform this version builds and runs. It is a working parametric woodworking baseline, not a finished cabinetmaking tool.

### Workspace

- Qt 6 desktop window with a dark graphite theme, a parts list, a property inspector, and an OpenGL 3.3 viewport. The window icon is `assets/icons/icon.png`.
- Orbit, pan, zoom, fit, and the standard views (top, front, right, isometric).
- Numeric edits for width, thickness, height, radius, position, and rotation. Display is metric or imperial. Stored lengths are integer nanometres, so changing the display unit does not change the model.
- Input accepts `19.05 mm`, `1,5 cm`, `3/4"`, and `3' 6 1/4"`.
- Undo and redo.
- Italian interface when the system language is Italian. English is the source language.
- A demo table (four legs, four aprons, one top) driven by nine project parameters. The Assembly menu also inserts a cabinet carcass, a drawer box, a frame, shelving, or a bedside table. Each recipe writes ordinary boards, parameters, and bindings. It does not cut joints.

### Model

- Open CASCADE solids for a board, a box, and a cylinder. The viewport shows a mesh of that solid.
- Joints are booleans. The parameters stay on the joint and the cut is rebuilt when a driving size changes. A miter and the dovetails are prisms. A dowel is a cylindrical hole.
- Shipped joints: mortise and tenon, half-lap, dado, groove, rabbet, bridle, castle, miter, through dovetail, sliding dovetail, finger joint, half-blind dovetail, tongue and groove, and dowel.
- A parent link and a role token on each part. Assemblies fill the role and leave the parent empty.
- A shipped catalog of 46 wood species plus sheet goods, and thickness presets that include nominal versus surfaced North American lumber. A user library in its own SQLite file holds custom materials and presets. The project stores a material id, not the library row.

### Drawings and lists

- Cut list as CSV. Cut list and bill of materials as JSON. The File menu exports them with allowance zero, so rough size equals finished size.
- Front, top, and right SVG wires, one sheet, a DXF R12 file, and a PDF. Those files are tessellations of the mesh. Each one says so.
- A separate front SVG states the part's overall width and height in millimetres, taken from the stored nanometres. It does not dimension joints or features. It is not hidden-line removal.

### Projects, library, and sign-in

- A `.timberlab` project is a zip with a checksum. A raw model JSON file also loads. A successful save copies the previous file to `.bak`. Restore is a separate action and runs only after the backup loads. Opening a damaged project does not replace it with the backup.
- A library row can be sealed with AES-256-GCM. Import writes that row only when the remote revision wins. The same revision with a different hash writes nothing. The project file is left unchanged.
- Desktop sign-in keeps an nsec in memory for the session, or opens a bunker socket. The nsec is not saved and is not sent to a relay. The bunker socket does not save, load, restore, or import a project. On Linux, Amber reports that it is an Android signer and does not contact a phone.
- With an nsec signed in, the File menu writes and reads a library-event file. Each row is a kind 30078 event, NIP-44 ciphertext for that same key. Import follows the same revision rule and does not change the project file. Bunker and Amber cannot seal that file. No run of this tree published an event to a relay. A project is not an event.

### Package

- Tag `0.1.0` is the CMake and application version. `.github/workflows/package.yml` runs on a version tag. On Ubuntu 24.04 it installs Qt 6.8.3, builds Open CASCADE 7.9.2 with the modeling libraries only, runs the test suite, `--check`, and `--accept`, and attaches `timberlab_<version>_amd64.deb` to the GitHub release when the job succeeds.
- The package bundles those Qt and Open CASCADE shared libraries and the xcb platform plugin. It does not include Wayland platform plugins. OpenSSL and SQLite stay the system libraries. Dependency names match Ubuntu 24.04 (`libssl3t64`, `libglib2.0-0t64`). The copyright file names the Qt 6.8.3 and Open CASCADE 7.9.2 source archives.

### Not in this release

- Feature dimensions, hidden-line removal, STEP, and STL. TKHLR is linked and unused.
- A published project, a live relay, a live bunker session, and Amber on a device.
- Haunched tenons, wedged tenons, loose tenons, cross laps, end laps, splined miters, biscuits, and pocket screws.
- Android, Windows, and macOS builds.
- A measured frame rate or save time. The AddressSanitizer suite was run before sign-in, with leak detection off, and has not been repeated for this tag.
