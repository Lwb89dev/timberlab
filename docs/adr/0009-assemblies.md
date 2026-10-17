# ADR 0009 — An assembly is a recipe that runs once

## Status

Accepted. Covered by `tests/test_assembly.cpp` and a carcass width edit in `--check`.

## Context

A cabinet is several boards whose sizes follow a few numbers. A live parent would turn every later edit into a second solver, and it would need a new object in the project file. Propagation already recomputes a part field from a parameter. The demo table is that mechanism with fixed ids. It replaces the project, so it is not a way to add a second cabinet beside the first.

## Decision

`addAssembly` accepts `table`, `carcass`, or `drawer`. Anything else is `assembly.unknown`. The recipe appends ordinary boards, ordinary parameters, and ordinary linear bindings, then stops. The project file does not gain an assemblies array. `parentId` stays empty. No joint is cut.

Parameter ids are `table_<id>.width` and the same pattern for the other roles, so two tables do not share `table.width`. The demo table still uses the short ids `table.width` and the short stored names. An inserted table stores names such as `Table width`.

The origin is 0 on an empty project. Otherwise it is the greatest world X of the existing parts, from the eight corners of each local box, plus a 20 mm gap. That number is the constant of each new X binding, and the literal X of a part whose X is not bound (the table top, the carcass left side, the drawer left side). A later change to an earlier part does not move it. The parts may then overlap. Touching is not overlap.

The new rows stay within the existing caps of 2000 components, 2000 parameters, and 2000 bindings (`storage.too_many`). A duplicate parameter id is `storage.duplicate`. Bindings are propagated and the stored joints are checked before the document changes. A failure leaves the document as it was. A size that makes an interior non-positive is `parameter.range` through the existing parameter edit.

The demo table calls the same table recipe with the prefix `table` and origin 0. A failed propagation still keeps those drivers, and the history step after it, as before.

## Consequences

- Releasing a field and typing a number still works. The other fields of that assembly keep following their parameters.
- There is still no expression language, and no solver that closes a gap or cuts a joint.
- Deleting one part does not delete the rest of the recipe. Bindings that named the part are dropped by the existing remove path.
- Cut lists, drawings, and sync are still later.
