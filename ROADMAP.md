# Roadmap

This is the sequence, not a claim that the later steps exist.

## Done in this baseline

Milestone 0 and the first slice of milestone 1:

- Vendored Qt 6.8 and OCCT 7.9 on Linux, with the renderer decision recorded.
- Workspace, viewport, camera, selection, board / box / cylinder, numeric edits, materials, undo, save/load, unit conversion, tests.

## Next

1. **Joinery, first set.** Mortise and tenon, half-lap, dado, groove, rabbet/rebate. Real booleans on the OCCT solid, parameters stored, geometry rebuilt when the part changes. Regression tests for each joint.
2. **Propagation.** Use the dependency graph so one cabinet or table dimension updates the parts that depend on it. The demo table does not do this yet.
3. **Smart assemblies** as rules that create ordinary parts: table, cabinet carcass, drawer box. Parts stay editable after the assembly is created.
4. **Cut list and bill of materials**, CSV and JSON, finished size distinct from rough stock.
5. **Drawings** projected from the solids, then PDF/SVG where the projection is real.
6. **Sync.** Optional encrypted library records over Nostr. Projects stay last. See `NOSTR_SYNC.md`. No CRDT.
7. **Other platforms**, only after each one has been built and used. Android is required by the product, and it is still unverified.
8. **More joints, more species data with citations, manufacturing checks, crash recovery.**

Performance numbers (frame rate, selection latency, save time) are not published until they are measured on the scenes listed in the product specification.
