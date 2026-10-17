# Testing

Two runners cover the same foundation:

- `build/timberlab-tests` — Catch2, 21 test cases
- `build/timberlab --check` — the same checks linked into the application, without Qt

On the baseline that introduced this file, both passed: Catch2 reported 466 assertions, and `--check` printed `self-check ok`. Re-run them after you change units, documents, solids, or storage. A green result in a later edit is not implied by this paragraph.

`TIMBERLAB_TEST_DIR` for Catch2 is the build directory `testdata/`. `--check` writes under `TIMBERLAB_DATA_DIR` or, if that is unset, `build/self-check`. Set the variable so a test does not touch `~/.local/share`.

## What is asserted

- Exact nanometres for the imperial preset list from 1/8 inch to 4 inch, and a round trip through the formatter
- 64 × 1/64 inch equals 1 inch
- `3' 6 1/4"` and `1,5 cm`
- An inexact inch decimal parses back to the same nanometres
- Display unit changes do not rewrite a stored thickness of 3/4 inch (19 050 000 nm, shown as `19.05 mm`)
- Undo of a unit change leaves the thickness; a second undo restores the previous thickness
- Lock blocks a dimension edit and still allows hide. A duplicate is unlocked
- JSON round trip, rejected malformed JSON
- The demo table has 9 parts whose world boxes touch and do not overlap
- A 90 degree Z rotation sends local +X to world +Y
- Box 100 × 18 × 200 mm volume and bounds, cylinder volume against π r² h, fine tessellation has more triangles than draft
- Zip save/load, garbage file rejected, bad SHA-256 rejected, raw JSON load, SQLite preference round trip
- At least 40 hardwood/softwood rows, a binomial name when the species is not marked ambiguous, no density, no Janka number
- A 2×4 nominal thickness differs from the surfaced thickness
- The dependency graph reports three dependents of `width` and detects the cycle `width → apron → tenon → width`

Geometry tests call OCCT. They are not thread-safe and they are not a visual test. The window was also launched with `--demo` on this machine and a screenshot was taken. That is a manual check, not a Catch2 case.

Sanitizers and a continuous-integration workflow are not set up. GitHub cannot build this tree until Qt and OCCT are available to the runner. That job has not been written, because it has not been run.
