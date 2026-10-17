# Roadmap

This is the sequence, not a claim that the later steps exist.

## Done in this baseline

Milestone 0 and the first slice of milestone 1:

- Vendored Qt 6.8 and OCCT 7.9 on Linux, with the renderer decision recorded.
- Workspace, viewport, camera, selection, board / box / cylinder, numeric edits, materials, undo, save/load, unit conversion, tests.
- `timberlab --accept` creates two boards, sets exact millimetre and inch sizes through the editor controller, saves, reopens, and checks the rebuilt solid. It then cuts a mortise and tenon into those boards, changes the tenon length, undoes that change, saves, and reloads the cut.
- Mortise and tenon, half-lap, dado, groove, and rabbet as OCCT booleans. Parameters stay in the document and the cuts are rebuilt when a driving size changes. See `docs/adr/0005-joinery.md`.
- Parameter propagation. The demo table's width, depth, leg, inset, and stock sizes recompute the parts that depend on them. A cycle or an invalid size rolls the edit back. Joint records stay literal. See `docs/adr/0007-propagation.md`.
- A versioned user library in `library.sqlite` (custom materials and thickness presets). Schema version 2 migrates a version 1 file and keeps the rows. The project archive stays the model. See `docs/adr/0008-user-library.md`.

## Next

1. **Smart assemblies** as rules that create ordinary parts: table, cabinet carcass, drawer box. Parts stay editable after the assembly is created.
2. **Cut list and bill of materials**, CSV and JSON, finished size distinct from rough stock.
3. **Drawings** projected from the solids, then PDF/SVG where the projection is real.
4. **Sync.** The user library now round-trips locally. Sync itself is not started. Optional encrypted records over Nostr. Projects stay last. See `NOSTR_SYNC.md`. No CRDT.
5. **Other platforms**, only after each one has been built and used. Android is required by the product. The renderer can select an OpenGL ES 3.00 shader path, and the desktop Qt/OCCT prefixes are not injected into an Android configure. No tablet build has been run.
6. **More joints, more species data with citations, manufacturing checks, crash recovery.**

Performance numbers (frame rate, selection latency, save time) are not published until they are measured on the scenes listed in the product specification.
