# ADR 0006 — Local persistence before synchronization

## Status

Accepted. Synchronization is not implemented.

## Context

A sync bug that can rewrite the only copy of a model will corrupt the CAD data, not just a cache. Nostr is optional and comes after the local store is versioned and recoverable.

## Decision

The authoritative project is the `.timberlab` archive: format version 1, `model.json` in integer nanometres, SHA-256 of those bytes, atomic rename, and a `.bak` of the previous file. That file is the model. It is not a SQLite database.

SQLite schema version 1 stores preferences only: display unit, fraction denominator, mesh quality. A database newer than the code is refused. Preferences are not the project.

A user library (custom materials and presets) does not exist yet. When it is added it will be its own SQLite database with a schema version and a migration test. It will not replace the project archive, and it will not be the only copy of a solid.

Nostr is blocked until that library exists and has been round-tripped locally. The sync layer must not open a socket, generate a key, or write into the archive code before then. A remote update must not delete an unpublished local edit.

## Consequences

- `timberlab --accept` saves and reopens a real archive through the editor controller. Catch2 covers the archive format without Qt.
- Moving the model itself into SQLite is not this decision. Replacing a checksummed file with an unproven database would be the failure mode this ADR is avoiding.
- See `NOSTR_SYNC.md`. Nothing there is code.
