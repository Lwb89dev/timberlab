# Joinery

The first set is implemented. The rule is in `docs/adr/0005-joinery.md`.

A joint is an OCCT boolean on the part solids. The viewport mesh is a tessellation of that solid. A decorative mesh, a texture, or a second shape drawn on top of an untouched board is not a joint. The record stores the type, both part ids, the faces, and the sizes. Those sizes are applied again when a part changes, so a centered tenon follows a thickness edit that still fits. An edit that makes a stored joint not fit is rejected and the document stays as it was. The mesh does not fall back to the uncut solid.

Stable identifiers are not localized:

- `joint.mortise_tenon` — cuts both parts. Part A is the mortise, part B is the tenon.
- `joint.half_lap` — cuts both parts. `removeMinA` and `removeMinB` choose the low-U half.
- `joint.dado` — cuts part A across the full V axis of the face. Part B is the stored mate and is not cut.
- `joint.groove` — cuts a stopped slot in part A. Part B is the stored mate and is not cut.
- `joint.rabbet` — cuts one edge of part A's face. Part B is the stored mate and is not cut. US “rabbet”, UK “rebate”; one identifier.
- `joint.bridle` — cuts both parts. Part A keeps a centered slot. Part B keeps a centered tongue and loses both cheeks. The slot is `thickness` wide and `length` deep.
- `joint.castle` — cuts both parts. `count` (2 to 8) is the number of fingers on part A. The bands are equal, centered, and `thickness` wide. Part A loses the odd bands. Part B loses the even bands.
- `joint.miter` — cuts both parts. The cutter is a right-triangle prism, not a box. One leg is `length` along the face normal. The other leg is the full U size. `removeMinA` and `removeMinB` keep the low-U corner. The angle is 45 degrees when `length` equals that U size.
- `joint.dovetail` — through dovetail. Part A is the pins, part B is the tails. `count` (1 to 8) is the number of tails. `thickness` is the tail width at the tail end, `width` is the pin width on that same plane, and `offset` is the slope on each side. `length` reaches into the tails. `depth` reaches into the pins and may equal the pin thickness, which is through.
- `joint.sliding_dovetail` — cuts both parts. Part A loses a trapezoid dado that runs the full V of the face. `width` is the opening at the face, `offset` is its U position, and `offset2` is the slope on each side. Part B loses the two shoulders of a centered tongue.

Faces are `x0`, `x1`, `y0`, `y1`, `z0`, `z1` in the part's local frame. `x0` is the minimum X face. On a face, U and V are the other two axes: X faces use U = Y and V = Z, Y faces use U = X and V = Z, Z faces use U = X and V = Y. A rabbet edge is `u0`, `u1`, `v0`, or `v1`.

Lengths in the joint record are nanometres: `length`, `width`, `thickness`, `depth`, `offset`, `offset2`. `count` is an integer, not a length. Castle and dovetail require it. It is omitted from the file when it is zero. The editor sends `count` as that integer. It does not parse it as a length. The editor's Add joint fills them with `defaultJoint`. Those starting sizes are not a structural rating, and TimberLab does not present them as one. The joint does not move either part; placement stays where the editor put it. A project parameter can move a part. That edit does not rewrite the joint record. The cut is rebuilt from the stored joint sizes and the new part extents when the joint still fits. An edit that makes the joint not fit is rejected (`joint.parameter`) and the document is unchanged. Loading a file whose joint does not fit the propagated sizes fails the same way.

A box cutter that already lies on a part face is extended 0.2 mm past that face so the boolean is not asked to cut a coincident face. The extra tool volume is outside the solid. A miter, a dovetail, and a sliding dovetail are prisms. Their sloped edges are not moved. The extrusion is extended past the part, and a 0.2 mm slab is fused on each profile edge that already lies on a part face. That slab stays outside the part. Interior faces of a pocket are not extended. Several cutters on one part are fused, then subtracted once. A boolean that does not run, or that does not reduce the volume, is an error (`joint.no_effect` or `geometry.kernel`). A cylinder cannot take a joint (`joint.not_box`).

Half-blind dovetails, finger joints, haunched tenons, dowels, biscuits, and the rest of the specification list are not implemented. Neither is a solved assembly that closes the gap between the two parts. The recipes do not cut joints.
