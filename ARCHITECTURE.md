# Architecture

TimberLab keeps the parametric model independent of Qt. QML reads and writes the model through one controller. The geometric authority is Open CASCADE. The viewport draws a tessellation of those solids. It does not invent a parallel mesh model.

## Modules in this baseline

| Area | Where | Role |
|---|---|---|
| Units | `include/timberlab/units` | Parse and format lengths. Storage is nanometres. |
| Document | `include/timberlab/core` | Parts, placement, history, JSON. |
| Constraints | `include/timberlab/constraints` | Dependency graph only. It does not resize furniture yet. |
| Geometry | `include/timberlab/geometry` | OCCT box and cylinder, volume, bounds, display mesh. |
| Materials | `include/timberlab/materials` | Catalog and preset JSON. |
| Storage | `include/timberlab/storage` | `.timberlab` zip, SHA-256, SQLite preferences. |
| Platform | `include/timberlab/platform` | User data directory. |
| UI | `src/ui`, `qml/` | Controller, OpenGL viewport, workspace. |
| App | `apps/timberlab` | Process entry and the headless self-check. |

Planned modules (joinery, assemblies, sync, manufacturing export) have no code yet. The document model leaves room for a parent id, notes, and a material id on every part.

## Boundaries

- The core, geometry, materials, and storage libraries do not include Qt.
- QML does not compute solids or write the project file itself.
- OCCT calls are made on the thread that asks for a mesh. The kernel is not treated as thread-safe.
- History is a stack of model JSON snapshots, capped at 100. Identical states are not stored. Undo restores a snapshot and does not discard the rest of the stack.
- A locked part rejects dimension, placement, material, name, and delete. Visibility still changes. A duplicate of a locked part is unlocked.

## Viewport

See `docs/adr/0001-renderer.md`. Short version: OCCT builds the solid, `BRepMesh_IncrementalMesh` tessellates it, and a `QQuickFramebufferObject` draws the triangles with OpenGL 3.3. Picking uses an integer id buffer (`GL_R32I`), not an 8-bit color. The process asks Qt for OpenGL before any window exists, because this item does not render on the default Qt 6 Vulkan scene graph.

Mesh deflection is 1.5 mm (draft), 0.4 mm (standard), or 0.1 mm (fine). The solid volume does not depend on that setting.

## Length and coordinates

See `docs/adr/0002-units.md`. One inch is 25 400 000 nm, so binary fractions down to 1/64 inch are exact. OCCT receives double millimetres at the boundary only. Placement is column-major: world = T · Rz · Ry · Rx · local, with degrees. The local origin of a board is its minimum corner. A cylinder stands on Z = 0 and is centered on local Z.

## Persistence

Preferences (display unit, fraction denominator, mesh quality) live in SQLite. The project file does not. See `DATA_FORMAT.md`.

The dependency graph is covered by tests so later propagation has a cycle check. Nothing in the editor calls it yet.
