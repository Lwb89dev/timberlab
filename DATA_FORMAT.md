# Project format

Extension: `.timberlab`. Format version 1. It is a zip file, not a memory dump.

## Zip entries

`manifest.json`

```json
{
  "format": "timberlab",
  "formatVersion": 1,
  "createdBy": "TimberLab 0.3.1",
  "model": "model.json",
  "modelSha256": "<hex sha-256 of the model.json bytes>"
}
```

`model.json` uses schema `timberlab.model`, `schemaVersion` 1, `lengthUnit` `nm`.

Fields: `id`, `name`, `displayUnit` (`mm`, `cm`, `m`, `in`, `ft`), `fractionPrecision` (2, 4, 8, 16, 32, or 64), and `components`.

Each component:

- `id`, `type` (`board`, `box`, `cylinder`), `name`, `materialId`
- `visible`, `locked`, `parentId`, optional `role`, `notes`
- `placement`: `x`, `y`, `z` in nanometres, `rx`, `ry`, `rz` in degrees
- `parameters`: `width`, `height`, `thickness`, `radius` in nanometres

A board or box must have positive width, height, and thickness. A cylinder must have positive radius and height. Zero and negative values are rejected. The maximum part edge is 100 m (`kMaxPartNm`).

`role` is omitted when empty. When present it is a token of at most 32 bytes matching `[a-z0-9._-]`. A bad token is `storage.json` on load and `component.role` from the editor. `parentId` empty means no parent. A missing parent is `component.missing`. A parent chain that returns to the part, or that is longer than the part list, is `component.cycle`. Removing a part clears that id from every child, including a locked child. An assembly does not set `parentId`.

`joints` is optional. A model with no joints omits the array, so an older file still loads. Schema version stays 1. Each joint:

- `id`, `type` (`joint.mortise_tenon`, `joint.half_lap`, `joint.dado`, `joint.groove`, `joint.rabbet`, `joint.bridle`, `joint.castle`, `joint.miter`, `joint.dovetail`, `joint.sliding_dovetail`, `joint.finger`, `joint.half_blind_dovetail`, `joint.tongue_groove`, `joint.dowel`, `joint.haunched_tenon`, `joint.wedged_tenon`, `joint.loose_tenon`, `joint.cross_lap`, `joint.end_lap`, `joint.splined_miter`, `joint.biscuit`, `joint.pocket_screw`)
- `partA`, `partB`, `faceA`, `faceB` (`x0`, `x1`, `y0`, `y1`, `z0`, `z1`)
- `edge` for a rabbet or a pocket screw (`u0`, `u1`, `v0`, `v1`)
- `length`, `width`, `thickness`, `depth`, `offset`, `offset2` in nanometres
- optional `length2` and `width2` in nanometres, omitted when zero. A haunched tenon requires `length2`. A wedged tenon requires both. Every other type ignores them, so an older file loads
- optional `count`, an integer. Castle, dovetail, finger, half-blind dovetail, wedged tenon, loose tenon, biscuit, and pocket screw require it. It is omitted when zero. It is not a length
- `removeMinA`, `removeMinB` for a half-lap, a miter, a splined miter, a haunched tenon, or an end lap; a missing flag means false

On load, bindings are applied before joints are checked, so a joint is judged against the recomputed parts. A joint that does not fit is rejected (`joint.parameter`). A missing part is `component.missing`. Duplicate joint ids are `storage.duplicate`. More than 2000 joints is `storage.too_many`. The cuts are not stored; they are rebuilt from these fields and the current part sizes. Joint sizes are not formulas.

Root `parameters` and `bindings` are optional. A model with none omits both arrays, so an older file still loads. Schema version stays 1. These are not the per-component `parameters` object above.

Each parameter is `id`, optional `name` (the id when omitted), and `value` in nanometres. Zero is allowed. A negative value or a value above 100 m is `parameter.range`. Duplicate parameter ids are `storage.duplicate`. More than 2000 parameters is `storage.too_many`.

Each binding is `part`, `field` (`width`, `height`, `thickness`, `radius`, `x`, `y`, or `z`), optional `constant` in nanometres (omitted means 0), and `terms`. A term is either `{ "parameter", "coefficient" }` or `{ "part", "field", "coefficient" }`. The coefficient is a required non-zero integer. The field becomes `constant + sum(coefficient × source)`.

A cycle is `parameter.cycle`. A result that is not a legal size or placement is `parameter.range`. An unknown field is `parameter.unknown`. A missing parameter or part is `parameter.missing` or `component.missing`. Duplicate targets are `storage.duplicate`. When load fails, the document being read into is left unchanged. A typed edit of a bound field is `parameter.driven` and does not change the number.

An assembly is not a stored object. Creating one writes rows into these `parameters` and `bindings` arrays, and ordinary components. Schema version stays 1. There is no `assemblies` array.

## Writes

The save writes a new owner-only temporary file beside the destination, `path.<16 random hex digits>.tmp`, opened with `O_EXCL | O_NOFOLLOW` so a file or symlink planted under that name is refused. The file is flushed with `fsync`, then renamed onto the destination, and the folder is synced. If that rename fails, the temporary file is removed and the existing project is left in place. If the destination already exists it is copied to `path.bak` first, through the same kind of temporary file and a rename, so a symlink at `path.bak` is replaced rather than written through. If that backup copy fails, the save fails and the previous file stays. `createdBy` carries the CMake project version. `restoreBackup` copies that `.bak` onto the project only after the backup itself loads. A missing backup is `storage.restore`. An invalid backup returns the load error. Either failure leaves the project file and the caller's document untouched. `loadProject` does not call `restoreBackup`. The new project file and the backup are owner-read/write only. A file larger than 32 MiB is rejected before it is parsed (`storage.too_large`). A model with more than 2000 components is rejected (`storage.too_many`). Duplicate component ids are rejected (`storage.duplicate`). Names are at most 200 bytes, notes at most 8000 bytes, ids at most 64 bytes, and a rotation whose absolute value is above one million degrees is rejected. A checksum mismatch on load is an error. The zip is read from the bytes already size-checked, not reopened from disk. Manifest fields of the wrong JSON type are `storage.format` (a non-string `modelSha256` is `storage.checksum`), never an exception. The loader also accepts a raw JSON object that starts with `{` and uses the model schema, so a single component file can be inspected without unzipping. That path is still size-capped and still parsed by the model checks. It does not verify the zip checksum.

## Preferences

SQLite, schema version 1, tables `schema_meta(version)` and `user_preference(key, value)`. A database whose version is newer than 1 is refused. Keys used now: `display_unit`, `fraction_precision`, `mesh_quality`. The file is `$TIMBERLAB_DATA_DIR/timberlab.sqlite`, or `$XDG_DATA_HOME/timberlab/`, or `~/.local/share/timberlab/`. The database file, its `-wal` file, and its `-shm` file are owner-read/write only. Open and schema failures stay stable codes (`preferences.open`, `preferences.schema`, `preferences.version`) and do not include the filesystem path. This database is not the project. The user library is a second file, `library.sqlite`, in the same directory. See `docs/adr/0008-user-library.md`.

## User library

SQLite, current schema version 3. A file at version 1 is migrated through version 2 on open. A file at version 2 gains a revision column. A file newer than 3 is refused and left as it is. Tables:

- `schema_meta(version)`
- `user_material(id, name, visual_hex, notes, revision)`. Version 1 had no `notes`. Version 2 had no `revision`.
- `user_preset(id, label, thickness_nm, group_name, revision)`. Version 1 had no `group_name`. Version 2 had no `revision`.

`revision` is a positive integer. A row inserted in the application starts at 1. An imported envelope may store the remote revision.

`id` is a token of at most 64 bytes. `name` and `label` are at most 200 bytes. `notes` is at most 8000 bytes. `visual_hex` is `#` plus six hex digits. `thickness_nm` is a positive nanometre count up to the same maximum as a part. Each table stops at 2000 rows. The file, its `-wal` file, and its `-shm` file are owner-read/write only. Open and schema failures stay stable codes (`library.open`, `library.schema`, `library.version`) and do not include the filesystem path. A project still stores only the material id.

## Catalog files

Shipped as resources, not stored inside each project:

- `resources/data/materials.json` — schema `timberlab.materials` version 1
- `resources/data/presets.json` — schema `timberlab.presets` version 1

A project stores the material id only. It does not yet snapshot the catalog row, so a future catalog edit could change how an old id is displayed. Snapshotting the material into the project is still to do. The id itself is stable.
