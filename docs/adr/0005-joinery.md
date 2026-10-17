# ADR 0005 — Joints are booleans

## Status

Accepted as a constraint. No joint is implemented.

## Context

A furniture configurator can draw a mortise as a texture or as a separate mesh and still look convincing. TimberLab is a CAD program. The cut has to be in the solid, and a later edit of the part has to be able to rebuild that cut from stored parameters.

## Decision

A joint modifies the OCCT solids of the parts it connects. The operation is a boolean (or an equivalent OCCT feature) driven by parameters kept in the document: type, the two parts, position, and the sizes needed to regenerate the cut. The viewport mesh is only a tessellation of the resulting shape. Editing that mesh is not a joint.

The first set, when it is built, is mortise and tenon, half-lap, dado, groove, and rabbet/rebate. Identifiers are reserved in `JOINERY_ENGINE.md` and are not written by the editor yet.

Joinery does not start until creating a board, typing an exact size, saving, and reopening that project is a tested path. A joint on top of a simulated editor would be a simulation.

## Consequences

- Regeneration has to run when a driving parameter or a driving part changes.
- A failed boolean is an error, not a silent decorative fallback.
- Default sizes are starting points. The program does not certify them as structurally safe.
