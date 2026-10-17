# Project format

Extension: `.timberlab`. Format version 1. It is a zip file, not a memory dump.

## Zip entries

`manifest.json`

```json
{
  "format": "timberlab",
  "formatVersion": 1,
  "createdBy": "TimberLab 0.1.0",
  "model": "model.json",
  "modelSha256": "<hex sha-256 of the model.json bytes>"
}
```

`model.json` uses schema `timberlab.model`, `schemaVersion` 1, `lengthUnit` `nm`.

Fields: `id`, `name`, `displayUnit` (`mm`, `cm`, `m`, `in`, `ft`), `fractionPrecision` (2, 4, 8, 16, 32, or 64), and `components`.

Each component:

- `id`, `type` (`board`, `box`, `cylinder`), `name`, `materialId`
- `visible`, `locked`, `parentId`, `notes`
- `placement`: `x`, `y`, `z` in nanometres, `rx`, `ry`, `rz` in degrees
- `parameters`: `width`, `height`, `thickness`, `radius` in nanometres

A board or box must have positive width, height, and thickness. A cylinder must have positive radius and height. Zero and negative values are rejected. The maximum part edge is 100 m (`kMaxPartNm`).

## Writes

The save path is `path.tmp`, then rename. If the destination already exists it is copied to `path.bak` first. If that backup copy fails, the save fails and the previous file stays. A checksum mismatch on load is an error. The loader also accepts a raw JSON object that starts with `{` and uses the model schema, so a single component file can be inspected without unzipping.

## Preferences

SQLite, schema version 1, tables `schema_meta(version)` and `user_preference(key, value)`. A database whose version is newer than 1 is refused. Keys used now: `display_unit`, `fraction_precision`, `mesh_quality`. The file is `$TIMBERLAB_DATA_DIR/timberlab.sqlite`, or `$XDG_DATA_HOME/timberlab/`, or `~/.local/share/timberlab/`. This database is not the project. A user material library is not in it. See `docs/adr/0006-persistence.md`.

## Catalog files

Shipped as resources, not stored inside each project:

- `resources/data/materials.json` — schema `timberlab.materials` version 1
- `resources/data/presets.json` — schema `timberlab.presets` version 1

A project stores the material id only. It does not yet snapshot the catalog row, so a future catalog edit could change how an old id is displayed. Snapshotting the material into the project is still to do. The id itself is stable.
