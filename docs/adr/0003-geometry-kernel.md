# ADR 0003 — Open CASCADE as the solid kernel

## Status

Accepted.

## Decision

Open CASCADE Technology is the only solid model. A board is a box solid (`BRepPrimAPI_MakeBox`) with X = width, Y = thickness, Z = height. A cylinder is `BRepPrimAPI_MakeCylinder` on local Z. The shape is checked, its volume comes from `BRepGProp`, and the viewport mesh is a tessellation of that shape.

The vendored build is 7.9.2 with modeling libraries only. The API used (`BRepMesh_IncrementalMesh` linear and angular deflection, 1-based `Poly_Triangulation::Node`) is also present in the 7.6 line shipped by Debian, but this tree has not been compiled against 7.6.

## Consequences

- Calls stay on one thread.
- Boolean joinery can be added later on the same shapes without a second kernel.
- The visualization toolkit is not a dependency.
- Exact STEP export is a later data-exchange build of OCCT, not something the current libraries do.
