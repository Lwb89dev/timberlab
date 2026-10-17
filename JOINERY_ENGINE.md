# Joinery

The first set is implemented. The rule is in `docs/adr/0005-joinery.md`.

A joint is an OCCT boolean on the part solids. The viewport mesh is a tessellation of that solid. A decorative mesh, a texture, or a second shape drawn on top of an untouched board is not a joint. The record stores the type, both part ids, the faces, and the sizes. Those sizes are applied again when a part changes, so a centered tenon follows a thickness edit that still fits. An edit that makes a stored joint not fit is rejected and the document stays as it was. The mesh does not fall back to the uncut solid.

Stable identifiers are not localized:

- `joint.mortise_tenon` — cuts both parts. Part A is the mortise, part B is the tenon.
- `joint.half_lap` — cuts both parts. `removeMinA` and `removeMinB` choose the low-U half.
- `joint.dado` — cuts part A across the full V axis of the face. Part B is the stored mate and is not cut.
- `joint.groove` — cuts a stopped slot in part A. Part B is the stored mate and is not cut.
- `joint.rabbet` — cuts one edge of part A's face. Part B is the stored mate and is not cut. US “rabbet”, UK “rebate”; one identifier.

Faces are `x0`, `x1`, `y0`, `y1`, `z0`, `z1` in the part's local frame. `x0` is the minimum X face. On a face, U and V are the other two axes: X faces use U = Y and V = Z, Y faces use U = X and V = Z, Z faces use U = X and V = Y. A rabbet edge is `u0`, `u1`, `v0`, or `v1`.

Lengths in the joint record are nanometres: `length`, `width`, `thickness`, `depth`, `offset`, `offset2`. The editor's Add joint fills them with `defaultJoint`. Those starting sizes are not a structural rating, and TimberLab does not present them as one. The joint does not move either part; placement stays where the editor put it. A project parameter can move a part. That edit does not rewrite the joint record. The cut is rebuilt from the stored joint sizes and the new part extents when the joint still fits. An edit that makes the joint not fit is rejected (`joint.parameter`) and the document is unchanged. Loading a file whose joint does not fit the propagated sizes fails the same way.

A cutter that already lies on a part face is extended 0.2 mm past that face so the boolean is not asked to cut a coincident face. The extra tool volume is outside the solid. Interior faces of a pocket are not extended. Several cutters on one part are fused, then subtracted once. A boolean that does not run, or that does not reduce the volume, is an error (`joint.no_effect` or `geometry.kernel`). A cylinder cannot take a joint (`joint.not_box`).

Dovetails and the other joints in the specification are not implemented. Neither is a solved assembly that closes the gap between the two parts.
