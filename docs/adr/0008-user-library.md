# ADR 0008 — User library is its own database

## Status

Accepted. Covered by `tests/test_library.cpp`, the `--check` round trip, and the `--accept` editor path.

## Context

Custom materials and stock thicknesses belong to the user, not to one project. ADR 0006 keeps the solid in the `.timberlab` archive. Putting the library in the preferences database would mix two lifetimes, and putting it in the archive would make one project the only copy of a material the next project also needs.

## Decision

`library.sqlite` sits next to `timberlab.sqlite` in the user data directory. It stores custom materials (id, name, `#RRGGBB` color, notes) and thickness presets (id, label, nanometres, group). It does not store parts, joints, or project JSON.

Schema version 1 is the material and preset tables without notes or a group. The current code writes version 2. Opening a version 1 file adds those columns, sets the version to 2, and keeps the rows. A newer version is refused and left unchanged. A preferences file opened by mistake is refused and its rows stay. The database file and its WAL files are owner-read/write only. Failures use stable codes and do not include the path or the SQLite message.

The shipped catalog stays the JSON resource. User rows are appended in the material and preset lists. A part still stores only the material id. Removing a library row does not edit any project. Density and hardness are not columns.

## Consequences

- A failed library open leaves the project usable.
- Nostr can round-trip these records later. This step does not open a socket.
- Smart assemblies are still a later milestone. A saved thickness is a number, not a cabinet rule.
