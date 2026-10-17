# ADR 0007 — Propagation is linear bindings

## Status

Accepted. Covered by `tests/test_propagation.cpp`, the `--check` width edit, and the demo inspector.

## Context

The dependency graph could name what a change affects, and it could reject a cycle. Nothing evaluated a size. The demo table stored nine independent boards, so changing the top width left the aprons where they were.

## Decision

A project parameter is a named length. A binding sets one part field to a constant plus integer coefficients times parameters or other part fields. The fields are width, height, thickness, radius, and the placement axes. Evaluation follows a topological order of the dependency graph. One edit is one history step.

A cycle is `parameter.cycle` and is not applied. A result outside the existing part or placement limits is `parameter.range` and is not applied. A typed edit of a bound field is `parameter.driven`. Releasing a field keeps the last computed length.

A lock blocks that typed edit. It also blocks adding or releasing a binding. It does not block a parameter from moving a part that is already bound. A duplicate copies the current size and does not copy the binding.

Joint sizes stay numbers on the joint. Changing a part size rebuilds the cut from those numbers and the new extents when the joint still fits. An edit that would make the joint not fit is rejected and does not change the document. Propagation does not turn a joint into a formula. Schema version stays 1. Empty parameter and binding arrays are omitted. On load, bindings run before the joint fit check.

The demo table uses this mechanism: nine drivers, and bindings for the legs, aprons, and top. It is not an assembly recipe.

## Consequences

- There is no expression language and no cabinet generator in this step.
- A failed propagation does not drop parts or joints, and it does not keep a partial update. An edit whose result makes a stored joint fail its fit check is rolled back with that joint error. The joint numbers stay as stored.
- Display rounding still does not write back. The arithmetic is integer nanometres. An overflow is `parameter.range`.
