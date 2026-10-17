# ADR 0005 — Joints are booleans

## Status

Accepted. The first set is implemented and covered by `tests/test_joinery.cpp` and the `--accept` mortise-and-tenon phase.

## Context

A furniture configurator can draw a mortise as a texture or as a separate mesh and still look convincing. TimberLab is a CAD program. The cut has to be in the solid, and a later edit of the part has to be able to rebuild that cut from stored parameters.

## Decision

A joint modifies the OCCT solids of the parts it connects. The operation is a boolean (or an equivalent OCCT feature) driven by parameters kept in the document: type, the two parts, position, and the sizes needed to regenerate the cut. The viewport mesh is only a tessellation of the resulting shape. Editing that mesh is not a joint.

The implemented set is mortise and tenon, half-lap, dado, groove, rabbet/rebate, bridle, castle, miter, through dovetail, and sliding dovetail. Identifiers and the stored fields are in `JOINERY_ENGINE.md`. The editor writes them. A dado, groove, or rabbet cuts the host only and still stores the mate. The others cut both solids. A miter, a through dovetail, and a sliding dovetail are prism cutters. They are not approximated by axis-aligned boxes.

Joinery does not start until creating a board, typing an exact size, saving, and reopening that project is a tested path. A joint on top of a simulated editor would be a simulation.

## Consequences

- Regeneration has to run when a driving parameter or a driving part changes.
- A failed boolean is an error, not a silent decorative fallback.
- Default sizes are starting points. The program does not certify them as structurally safe.
