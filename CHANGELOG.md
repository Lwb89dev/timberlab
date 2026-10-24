# Changelog

## 0.4.0 — 2026-10-24

### Materials

- All 57 shipped materials now include a researched, photorealistic 512 px texture for the longitudinal face and a separate 512 px texture for end grain or manufactured-panel edge construction.
- The 3D viewport maps solid-wood end grain only onto faces transverse to the detected fibre axis. Sheet goods use their face texture on the broad surfaces and their layered or composite edge texture on the sides.
- The material selector now includes paired face/end previews. Custom materials without images keep their existing colour fallback.

### Quality

- The catalog schema validates both texture references and the test suite verifies that every referenced asset is shipped.
- Texture research, generation constraints, and limitations are documented with the assets.

## 0.3.1 — 2026-10-24

### Appearance

- The workspace now follows the saw-blade icon's graphite-and-brass visual language, with a branded command bar, rounded panels, clearer hierarchy, compact viewport controls, animated interactions, and a persistent light/dark theme. The OpenGL canvas and grid switch palette with the interface.
- Parts, inspector sections, joinery cards, project state, account state, and empty states have been redesigned for faster scanning. Native controls now use the Qt Material style, and every new interface string is available in Italian.

### Security and privacy

- Opening a project whose manifest has a field of the wrong JSON type no longer terminates the application; it is a format error. The same holds for a library envelope whose payload id or kind is not a string.
- Saves write through a randomly named, owner-only temporary file (`O_EXCL | O_NOFOLLOW`) that is synced before the rename. A symlink planted at `project.timberlab.bak` or at the old fixed `.tmp` name can no longer redirect a save onto another file. Restore uses the same path. The zip is parsed from the bytes that passed the size check.
- A bunker relay address can no longer carry CR/LF, spaces, control bytes, or a fragment into the WebSocket upgrade request. The upgrade answer must start with `HTTP/1.1 101`.
- Bunker sign-in now has a connect timeout per relay, an overall five-minute deadline, and per-relay limits on AUTH challenges and messages. Previously a relay that accepted TCP and never answered held the sign-in open indefinitely.
- NIP-42: after AUTH the subscription and the request are sent again, and `auth-required:` refusals no longer abort the sign-in. A refusal from one relay moves on to the next relay. Relay `OK` and `CLOSED` messages are matched to our event id and subscription.
- `auth_url` is accepted only as a printable `https://` address. Relay and bunker text is stripped of control and bidirectional-format characters before it reaches the status line.
- Labels that show part and parameter names from a project file render plain text. The QML engine can no longer make network requests: only `qrc:`, `file:`, and `data:` URLs load.
- Secret hygiene: bech32 decoding, HKDF, Schnorr nonce derivation, and NIP-44 decryption clear every intermediate buffer on every return path, and the scalar is flagged constant-time. A bunker link is cleared after parsing.
- The relay socket requires TLS 1.2 or later and caps its read buffer. The data folder is created owner-only. Importing a library events file is capped at 64 MiB.

### Fixes

- Italian: 160 interface strings from the QML files had been marked vanished since 0.2.0 and were missing from the compiled catalog. lupdate now scans the QML files; three strings that had never been translated are translated.
- Amber may answer with an npub instead of hex. A bunker may answer connect with the echoed secret instead of `ack` (NIP-46).
- `createdBy` in the manifest said 0.2.0; it now carries the build version.

### Performance

- The WebSocket reader parses a burst of frames at an offset and trims its buffer once, instead of erasing from the front after every frame.
- A relay message reaches the parser as bytes, without a round trip through a UTF-16 `QString`.

## 0.3.0 — 2026-10-22

### Selection, deletion, and viewport refresh

- Parts appear and disappear in the viewport immediately when added or removed; no click on the viewport is required.
- Select multiple parts from the parts panel with Ctrl+click (toggle) and Shift+click (range).
- Press Delete to remove all selected parts, whether selected in the panel or in the viewport.
- The inspector hides per-part fields and shows the selection count when multiple parts are selected.

### Magnetic alignment and ground snap

- Dragged parts keep their snap until pulled past a release threshold, giving a magnetic feel.
- The bottom face of a part can snap to the Z = 0 ground plane during translation.

## 0.2.0 — 2026-10-19

### Interactive alignment and contact joints

- Drag parts in the viewport. Shift-click begins a constrained drag; plain drag first aligns the click to the part surface plane, then moves in that plane. Edges and centers of other visible parts snap within 8 mm per axis; the status bar reports which axes snapped, and a floating overlay shows the live X/Y/Z deltas.
- Right-click a visible contact overlap to arm the two parts, then pick a joint type from the context menu. The joint uses the actual touching faces when the contact is a single flat overlap; otherwise it falls back to the default face mapping. The usual joint cut is applied to the solids.
- Rename a part from the parts panel; the change is undoable and stored in the project.

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
