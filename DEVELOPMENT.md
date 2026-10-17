# Development

Work stays inside this repository. `deps/` and `build/` are local. Do not commit them.

## Layout

- `include/timberlab/` public headers for the model
- `src/` the matching sources, plus `src/ui` for the Qt controller and viewport
- `apps/timberlab/` `main.cpp` and the headless self-check
- `qml/` the workspace
- `resources/` the Qt resource file and `resources/data/` catalog JSON
- `translations/timberlab_it.ts` Italian UI catalog. English strings live in the source.
- `tests/` Catch2
- `scripts/seed_data.py` rewrites the shipped catalog and presets
- `docs/adr/` decisions

## Rules that already matter

- Three levels of indentation is the point where a function should be split. The code follows that unless a loop genuinely needs one more.
- Core functions return stable error codes (`dimension.non_positive`, `storage.checksum`, …). The controller translates those codes. Do not put translated sentences in the model.
- Do not write display rounding back into nanometres.
- Do not invent density or hardness. Leave the field null.
- A new joint or assembly is a later milestone. Do not fake one with a decorative mesh.
- OCCT stays on the calling thread.

## Regenerating the catalog

```bash
python3 scripts/seed_data.py
```

Edit the species list in that script, run it, and commit the JSON. The loader ignores nothing it understands: `grain` may be empty, and `densityKgM3` / `jankaN` stay null.

## Adding a UI string

Wrap it in `qsTr` or `tr`. Then rebuild. `qt_add_translations` refreshes `translations/timberlab_it.ts`. Fill the Italian `<translation>` and leave it finished. English needs no catalog entry.

## History

`Document` stores whole JSON snapshots. That is deliberate at this size. An incremental command log can replace it later without changing the file schema.
