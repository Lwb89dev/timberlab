# Testing

Two runners cover the same foundation:

- `build/timberlab-tests` — Catch2, 47 test cases. These do not construct the Qt controller.
- `build/timberlab --check` — the same model checks linked into the application, without Qt
- `build/timberlab --accept` — the editor controller, without a window. It creates two boards, types exact sizes, saves, reopens, edits again, and checks nanometres plus the rebuilt mesh. It then adds a mortise and tenon, shortens the tenon, undoes that edit, and reloads the cut.

On the baseline that introduced this file, Catch2 reported 466 assertions and `--check` printed `self-check ok`. After the editor acceptance path and the box-mesh vertex check were added, Catch2 reported 472 assertions in 21 cases. After the debug pass (save backup, size and text limits, preference error codes), Catch2 reported 512 assertions in 25 cases. After the first joinery set, Catch2 reported 623 assertions in 30 cases, `--check` printed `self-check ok`, and `--accept` printed `accept ok` for an 800 × 19.05 × 400 mm shelf with a mortise and a 24 × 3/4 × 12 inch rail with a tenon, reloaded from `build/accept/boards.timberlab`. After parameter propagation, Catch2 reported 733 assertions in 40 cases. `--check` printed `self-check ok`, including a demo width of 1500 mm (top 1500 mm, front apron 1300 mm, leg 2 at X 1400 mm) and the undo back to 1200 mm, 1000 mm, and 1100 mm. `--accept` again printed `accept ok` for the same unbound shelf and rail: those two boards have no parameters. After the debug pass that rejects an edit when a stored joint would no longer fit, Catch2 reported 766 assertions in 42 cases. `--check` printed `self-check ok` and `--accept` printed `accept ok` again. After the user library, Catch2 reported 2855 assertions in 47 cases. Most of the new assertions are the 2000-row cap, one per inserted material. `--check` printed `self-check ok`, including a library material and an 18 mm preset reopened from `library.sqlite`. `--accept` printed `accept ok` again, and also saved a user material onto the shelf, reloaded that id, and left the mortise in place. Adding the preset did not change the shelf thickness. `--accept` forces millimetres after loading preferences, so a previous inch setting in that data directory does not change the metric checks. Re-run them after you change units, documents, solids, storage, or the controller. A green result in a later edit is not implied by this paragraph.

`TIMBERLAB_TEST_DIR` for Catch2 is the build directory `testdata/`. `--check` writes under `TIMBERLAB_DATA_DIR` or, if that is unset, `build/self-check`. Set the variable so a test does not touch `~/.local/share`.

## What is asserted

- Exact nanometres for the imperial preset list from 1/8 inch to 4 inch, and a round trip through the formatter
- 64 × 1/64 inch equals 1 inch
- `3' 6 1/4"` and `1,5 cm`
- An inexact inch decimal parses back to the same nanometres
- Display unit changes do not rewrite a stored thickness of 3/4 inch (19 050 000 nm, shown as `19.05 mm`)
- Undo of a unit change leaves the thickness; a second undo restores the previous thickness
- Lock blocks a dimension edit and still allows hide. A duplicate is unlocked
- JSON round trip, rejected malformed JSON, rejected overlong names and notes, rejected non-finite or huge angles, rejected duplicate ids, rejected more than 2000 components
- The demo table has 9 parts whose world boxes touch and do not overlap. Setting `table.width` to 1500 mm sets the top to 1500 mm, the front apron to 1300 mm, and leg 2's X to 1400 mm, still without overlap. Undo restores 1200 mm, 1000 mm, and 1100 mm. A typed edit of the bound top width is `parameter.driven`. A width that collapses an apron rolls the document back
- Shrinking a mortised shelf from 600 mm to 10 mm is rejected (`joint.parameter`) and the document is unchanged, whether the width is typed or driven. A width of 400 mm still fits and reloads. A locked part refuses a new binding and a release. A parameter that already drives that part still moves it
- A new library file is schema version 2. A planted version 1 file migrates, keeps the material and the 18 mm preset, and then accepts another row. A version 3 file is refused and its row stays. Opening a preferences file as a library fails with `library.schema` and the preference stays. A bad name, color, thickness, or duplicate id is rejected. The 2001st material is `library.too_many`
- A 90 degree Z rotation sends local +X to world +Y
- Box 100 × 18 × 200 mm volume and bounds, cylinder volume against π r² h, fine tessellation has more triangles than draft
- Zip save/load, a second save leaves the new file and a backup of the previous one, garbage file rejected, bad SHA-256 rejected, a file over 32 MiB rejected, raw JSON load, SQLite preference round trip, a preferences path that cannot be opened reports `preferences.open`
- At least 40 hardwood/softwood rows, a binomial name when the species is not marked ambiguous, no density, no Janka number
- A 2×4 nominal thickness differs from the surfaced thickness
- The dependency graph reports three dependents of `width`, orders `width` before `apron` before `tenon`, and detects the cycle `width → apron → tenon → width`
- Mortise and tenon cut both solids; a later thickness change recenters the tenon. Half-lap cuts both parts. Dado, stopped groove, and rabbet cut the host and leave the mate whole. A cylinder that no joint cuts stays a cylinder. Undo, a locked part, a joint that does not fit, and deleting a referenced part are covered. One meshed joint matches the probed volume

Geometry tests call OCCT. They are not thread-safe and they are not a visual test. The window was launched with `--demo` on this machine and a screenshot was taken. The inspector listed the nine table parameters in Italian, then the library heading and the material field. The QML log was empty. Drive, Release, Add material, Add preset, and Remove were not clicked. That is a manual check, not a Catch2 case.

Sanitizers and a continuous-integration workflow are not set up. GitHub cannot build this tree until Qt and OCCT are available to the runner. That job has not been written, because it has not been run.
