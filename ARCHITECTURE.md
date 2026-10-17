# Architecture

TimberLab keeps the parametric model independent of Qt. QML reads and writes the model through one controller. The geometric authority is Open CASCADE. The viewport draws a tessellation of those solids. It does not invent a parallel mesh model.

## Modules in this baseline

| Area | Where | Role |
|---|---|---|
| Units | `include/timberlab/units` | Parse and format lengths. Storage is nanometres. |
| Document | `include/timberlab/core` | Parts, placement, history, JSON. |
| Assemblies | `src/core/assembly.cpp` | Recipes that append boards, parameters, and bindings. Not a stored parent. |
| Manufacturing | `include/timberlab/core/manufacturing.hpp` | Cut list, bill of materials, and a front-view tessellation wire. |
| Sync records | `include/timberlab/storage/sync.hpp` | Local AES-256-GCM and revision compare. No network. |
| Constraints | `include/timberlab/constraints` | Dependency graph and linear bindings. A project parameter recomputes the part fields that name it. |
| Geometry | `include/timberlab/geometry` | OCCT box and cylinder, volume, bounds, display mesh. |
| Materials | `include/timberlab/materials` | Catalog and preset JSON. |
| Storage | `include/timberlab/storage` | `.timberlab` zip, SHA-256, SQLite preferences, SQLite user library, local AES-256-GCM. |
| Platform | `include/timberlab/platform` | User data directory. |
| UI | `src/ui`, `qml/` | Controller, OpenGL viewport, workspace. |
| App | `apps/timberlab` | Process entry and the headless self-check. |

`parentId` is a parent link. `setParent` rejects a missing part and a cycle. Assemblies do not fill it. `role` is an optional token on a part. PDF and DXF wires exist. The front dimension file states overall width and height from the model. A hidden-line drawing does not. A relay that stores a project does not.

## Boundaries

- The core, geometry, materials, and storage libraries do not include Qt.
- QML does not compute solids or write the project file itself.
- OCCT calls are made on the thread that asks for a mesh. The kernel is not treated as thread-safe.
- History is a stack of model JSON snapshots, capped at 100. Identical states are not stored. Undo restores a snapshot and does not discard the rest of the stack.
- A locked part rejects a direct edit of dimension, placement, material, name, role, parent, delete, and of driving or releasing a field. Visibility still changes. A project parameter can still move a locked part that is already bound. Removing another part still clears a locked child's parent link. A duplicate of a locked part is unlocked and keeps the current size without the binding.

## Viewport

See `docs/adr/0001-renderer.md`. Short version: OCCT builds the solid, `BRepMesh_IncrementalMesh` tessellates it, and a `QQuickFramebufferObject` draws the triangles. Desktop Linux uses OpenGL 3.3 core. The same shader bodies compile as OpenGL ES 3.00 when the context is ES, which is what an Android tablet will need. That ES path has not been run on a device. Picking uses an integer id buffer (`GL_R32I`), not an 8-bit color. The process asks Qt for OpenGL before any window exists, because this item does not render on the default Qt 6 Vulkan scene graph.

Mesh deflection is 1.5 mm (draft), 0.4 mm (standard), or 0.1 mm (fine). The solid volume does not depend on that setting.

## Length and coordinates

See `docs/adr/0002-units.md`. One inch is 25 400 000 nm, so binary fractions down to 1/64 inch are exact. OCCT receives double millimetres at the boundary only. Placement is column-major: world = T · Rz · Ry · Rx · local, with degrees. The local origin of a board is its minimum corner. A cylinder stands on Z = 0 and is centered on local Z.

## Persistence

The project model lives in the versioned `.timberlab` archive, with a checksum. SQLite preferences are schema version 1. The user library is a second database, schema version 3, for custom materials and thickness presets, each with a revision. A CAD relay is still not implemented. Desktop sign-in can hold an nsec in memory or open a bunker socket. That socket does not replace the project file. Local AES-256-GCM can replace one library row when a sealed envelope wins the revision check. A kind 30078 library event, opened with the nsec, can replace that same row. Neither path replaces the project file. The library must not become the only copy of a solid. See `DATA_FORMAT.md`, `docs/adr/0006-persistence.md`, and `docs/adr/0008-user-library.md`.

Propagation evaluates linear bindings in topological order. A cycle, or a result outside the part and placement limits, rejects the whole edit. Joint sizes stay numbers on the joint. See `docs/adr/0007-propagation.md`.
