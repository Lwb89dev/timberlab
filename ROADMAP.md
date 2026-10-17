# Roadmap

This is the sequence, not a claim that the later steps exist.

## Done in this baseline

Milestone 0 and the first slice of milestone 1:

- Vendored Qt 6.8 and OCCT 7.9 on Linux, with the renderer decision recorded.
- Workspace, viewport, camera, selection, board / box / cylinder, numeric edits, materials, undo, save/load, unit conversion, tests.
- `timberlab --accept` creates two boards, sets exact millimetre and inch sizes through the editor controller, saves, reopens, and checks the rebuilt solid.

## Next

1. **Joinery, first set.** Mortise and tenon, half-lap, dado, groove, rabbet/rebate. Real booleans on the OCCT solids, parameters stored, both parts rebuilt when a driving size changes. See `docs/adr/0005-joinery.md`. No decorative mesh.
2. **Propagation.** Use the dependency graph so one cabinet or table dimension updates the parts that depend on it. The demo table does not do this yet.
3. **Versioned user library** in its own SQLite database (custom materials and presets), with a schema version and a migration test. The project archive stays the model. See `docs/adr/0006-persistence.md`.
4. **Smart assemblies** as rules that create ordinary parts: table, cabinet carcass, drawer box. Parts stay editable after the assembly is created.
5. **Cut list and bill of materials**, CSV and JSON, finished size distinct from rough stock.
6. **Drawings** projected from the solids, then PDF/SVG where the projection is real.
7. **Sync.** Only after the library in step 3 round-trips locally. Optional encrypted records over Nostr. Projects stay last. See `NOSTR_SYNC.md`. No CRDT.
8. **Other platforms**, only after each one has been built and used. Android is required by the product. The renderer can select an OpenGL ES 3.00 shader path, and the desktop Qt/OCCT prefixes are not injected into an Android configure. No tablet build has been run.
9. **More joints, more species data with citations, manufacturing checks, crash recovery.**

Performance numbers (frame rate, selection latency, save time) are not published until they are measured on the scenes listed in the product specification.
