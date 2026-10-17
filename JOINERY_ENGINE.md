# Joinery

No joint is implemented. The rule is in `docs/adr/0005-joinery.md`.

A mortise and tenon will modify both solids with an OCCT boolean. The joint record keeps the parameters needed to regenerate those cuts when either part changes. A decorative mesh, a texture, or a second shape drawn on top of an untouched board is not a joint. A failed boolean is an error.

The first set, when it is built, is mortise and tenon, half-lap, dado, groove, and rabbet/rebate. Dovetails and the other joints in the specification come after those have regression tests.

Stable identifiers are reserved and are not localized:

- `joint.mortise_tenon`
- `joint.half_lap`
- `joint.dado`
- `joint.groove`
- `joint.rabbet` (US “rabbet”, UK “rebate”; one identifier)

Nothing in the editor writes these yet. Default dimensions, when they exist, will be labeled as starting points, not as structurally safe values. TimberLab will not present joint warnings as a structural certificate.
