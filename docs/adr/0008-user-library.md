# ADR 0008 — User library is its own database

## Status

Accepted. Covered by `tests/test_library.cpp`, the `--check` round trip, and the `--accept` editor path.

## Context

Custom materials and stock thicknesses belong to the user, not to one project. ADR 0006 keeps the solid in the `.timberlab` archive. Putting the library in the preferences database would mix two lifetimes, and putting it in the archive would make one project the only copy of a material the next project also needs.

## Decision

`library.sqlite` sits next to `timberlab.sqlite` in the user data directory. It stores custom materials (id, name, `#RRGGBB` color, notes, revision) and thickness presets (id, label, nanometres, group, revision). It does not store parts, joints, or project JSON.

Schema version 1 is the material and preset tables without notes or a group. Version 2 adds those columns. Version 3 adds `revision`, default 1. A new file is written at version 3. Opening version 1 adds the version 2 columns and then the revision. Opening version 2 adds the revision and keeps the rows. A newer version is refused and left unchanged. A preferences file opened by mistake is refused and its rows stay. The database file and its WAL files are owner-read/write only. Failures use stable codes and do not include the path or the SQLite message. A sealed envelope may replace one library row. It does not open a project.

The shipped catalog stays the JSON resource. User rows are appended in the material and preset lists. A part still stores only the material id. Removing a library row does not edit any project. Density and hardness are not columns.

## Consequences

- A failed library open leaves the project usable.
- Nostr can round-trip these records later. This step does not open a socket.
- A saved thickness is a number, not a cabinet rule. Assembly recipes write ordinary parts into the project and do not read this database.
