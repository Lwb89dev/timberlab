# ADR 0004 — Application license

## Status

Accepted as the provisional project license. Not a legal review.

## Decision

GPL-3.0-or-later.

The Qt modules this program links are LGPL. OCCT is LGPL-2.1 plus the OCCT linking exception. GPL-3.0-or-later stays compatible with those and with a possible later GPL-only Qt module. The program does not link Qt Quick 3D, which is GPL-only, because the renderer decision already rejected it.

MIT or Apache was not chosen. Either would forbid linking a GPL-only Qt module later, and the product specification allows that possibility.

## Consequences

Details, the module list, and the third-party table are in `LICENSES.md`. A binary distribution has to satisfy the LGPL and GPL obligations. No such distribution exists yet.
