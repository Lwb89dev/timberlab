# Joinery

No joint is implemented.

The product will cut real solids. A mortise will be a boolean on the receiving part, driven by stored parameters, and rebuilt when that part changes. A decorative mesh is not a joint.

The first set, when it is built, is mortise and tenon, half-lap, dado, groove, and rabbet/rebate. Dovetails and the other joints in the specification come after those have regression tests.

Stable identifiers are reserved and are not localized:

- `joint.mortise_tenon`
- `joint.half_lap`
- `joint.dado`
- `joint.groove`
- `joint.rabbet` (US “rabbet”, UK “rebate”; one identifier)

Nothing in the editor writes these yet. Default dimensions, when they exist, will be labeled as starting points, not as structurally safe values. TimberLab will not present joint warnings as a structural certificate.
