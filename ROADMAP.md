# Roadmap

This is the sequence, not a claim that the later steps exist.

## Done in this baseline

Milestone 0 and the first slice of milestone 1:

- Vendored Qt 6.8 and OCCT 7.9 on Linux, with the renderer decision recorded.
- Workspace, viewport, camera, selection, board / box / cylinder, numeric edits, materials, undo, save/load, unit conversion, tests.
- `timberlab --accept` creates two boards, sets exact millimetre and inch sizes through the editor controller, saves, reopens, and checks the rebuilt solid. It then cuts a mortise and tenon into those boards, changes the tenon length, undoes that change, saves, and reloads the cut.
- Mortise and tenon, half-lap, dado, groove, and rabbet as OCCT booleans. Parameters stay in the document and the cuts are rebuilt when a driving size changes. See `docs/adr/0005-joinery.md`.
- Parameter propagation. The demo table's width, depth, leg, inset, and stock sizes recompute the parts that depend on them. A cycle or an invalid size rolls the edit back. Joint records stay literal. See `docs/adr/0007-propagation.md`.
- A versioned user library in `library.sqlite` (custom materials and thickness presets). Schema version 3 migrates a version 1 file through version 2, and a version 2 file gains a revision column. The project archive stays the model. See `docs/adr/0008-user-library.md`.
- Smart assemblies. A table, a cabinet carcass, a drawer box, a frame, shelving, or a bedside table is created as ordinary boards, parameters, and linear bindings. The parts stay editable and receive a role. A second assembly keeps the origin it was given. See `docs/adr/0009-assemblies.md`.
- Bridle, castle, miter, through dovetail, and sliding dovetail, added to the first joinery set. Miter and both dovetails are prism booleans.
- A parent link (`setParent`) and a role token on each part.
- Cut list and bill of materials. CSV and JSON. An allowance separates rough stock from the finished size. The menu export uses allowance zero. See `docs/adr/0010-cut-list.md`.
- Front, top, and right SVG wires, one sheet, a DXF R12 of those edges, and a PDF of the same edges. They are tessellation wires. A separate front file adds the model's overall width and height in millimetres. Hidden-line removal is not used. See `docs/adr/0010-cut-list.md`.
- Local AES-256-GCM, a revision compare, and a sealed library-row envelope. A remote win updates the library row. The project file is not part of that path. That envelope code opens no socket and stores no key. See `docs/adr/0011-local-sync.md`.
- Finger joint, half-blind dovetail, tongue and groove, and dowel. The dowel cutter is a cylinder. The half-blind dovetail is the through-dovetail prism and refuses a depth that reaches the far face.
- A library event, kind 30078, sealed with NIP-44 to the author's own nsec. The file is a JSON array of those events. Import writes a library row under the same revision rule and does not change the project file. Nothing in that path opens a relay. See `NOSTR_SYNC.md`.
- An explicit restore from `.bak`, after the backup loads. Load does not call it.
- Length conversion uses a portable 128-bit multiply and divide. On this GCC build that result is checked against `__int128`.

## Next

1. **Drawings.** The shop wires are still the tessellation. The front dimension file states the model's overall width and height. It does not dimension features, and it is not a hidden-line view. TKHLR is linked and unused.
2. **Sync.** A library row can be sealed as kind 30078 and imported from a file. Desktop sign-in can hold an nsec in memory or start a bunker session over a WebSocket. That socket does not read or write the project. Amber is an Android request; this Linux build returns an error and does not talk to a phone. No test and no run of this tree published an event to a relay. A project is not an event. See `NOSTR_SYNC.md`. No CRDT. Milestone 6 is not finished.
3. **Other platforms**, only after each one has been built and used. Android is required by the product. The renderer can select an OpenGL ES 3.00 shader path, and the desktop Qt/OCCT prefixes are not injected into an Android configure. The length math no longer requires `__int128`. No tablet, Windows, or macOS build has been run on this machine.
4. **The remaining joints** in the product list (haunched, wedged, and loose tenons, cross lap, end lap, splined miter, biscuit, pocket screw), species data with citations, and manufacturing checks.
5. **Hardening.** A successful save copies the previous file to `.bak`. Restore is a separate call and refuses a backup that does not load. `-DTIMBERLAB_SANITIZE=ON` builds the model tests with AddressSanitizer and UndefinedBehaviorSanitizer. That suite was run on this machine with leak detection off, and it passed. `.github/workflows/package.yml` runs on a version tag, builds Qt and OCCT on the runner (they are not in the git checkout), and publishes `timberlab_<version>_amd64.deb` when that job succeeds. Measured frame rate and save time are not published.

Performance numbers are not published until they are measured on the scenes listed in the product specification.
