# ADR 0006 — Local persistence before synchronization

## Status

Accepted. A CAD relay is not implemented. Local sealing and revision compare are a later decision (`docs/adr/0011-local-sync.md`). Sign-in is described in `NOSTR_SYNC.md`.

## Context

A sync bug that can rewrite the only copy of a model will corrupt the CAD data, not just a cache. Nostr is optional and comes after the local store is versioned and recoverable.

## Decision

The authoritative project is the `.timberlab` archive: format version 1, `model.json` in integer nanometres, SHA-256 of those bytes, atomic rename, and a `.bak` of the previous file. That file is the model. It is not a SQLite database.

SQLite schema version 1 stores preferences only: display unit, fraction denominator, mesh quality. A database newer than the code is refused. Preferences are not the project.

The user library is its own SQLite database, schema version 3, with a migration from version 1 through version 2. It holds custom materials and thickness presets. It does not replace the project archive, and it is not the only copy of a solid. See `docs/adr/0008-user-library.md`.

Nostr is blocked until that library exists and has been round-tripped locally. The sync layer must not open a socket, generate a key, or write into the archive code before then. A remote update must not delete an unpublished local edit.

## Consequences

- `timberlab --accept` saves and reopens a real archive through the editor controller. Catch2 covers the archive format without Qt.
- A failed rename does not delete the previous archive. The loader refuses a file over 32 MiB, a model over 2000 components, or duplicate component ids. Project files and the preferences database are owner-read/write only.
- Moving the model itself into SQLite is not this decision. Replacing a checksummed file with an unproven database would be the failure mode this ADR is avoiding.
- A signer socket may open for bunker sign-in. It must not call save, load, restore, or library import. An nsec stays in memory and is not sent to a relay. A library-event file, written only while that nsec is signed in, can replace one library row. It does not open the project. Publishing a project is still not implemented. See `NOSTR_SYNC.md`.
