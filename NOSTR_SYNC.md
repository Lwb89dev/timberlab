# Nostr synchronization

A relay client is not implemented. The application does not open a socket, does not generate a key, and does not publish an event. See `docs/adr/0006-persistence.md` and `docs/adr/0011-local-sync.md`.

What exists now:

- The versioned `.timberlab` archive (format 1, checksum, atomic rename) and a separate user library (`library.sqlite`, schema version 3) of custom materials and thickness presets. Version 1 migrates through version 2. Version 2 gains a `revision` column, default 1. Preferences stay in their own SQLite schema 1 file.
- `sealAes256Gcm` and `openAes256Gcm` in `include/timberlab/storage/sync.hpp`. The caller passes a 32-byte key. The key is not written into the project, the library, or a file by this code. The packed bytes are a 12-byte random nonce, the ciphertext, and a 16-byte GCM tag. A bad tag or a short buffer is `sync.crypto` and returns no plaintext.
- `chooseRevision` compares two records that already have an id, a kind, a revision, and a content hash. The same hash keeps the local record. The same revision with a different hash is `sync.conflict` and changes nothing. The higher revision wins. A negative revision is `sync.range`. A different id or kind is `sync.mismatch`.
- `Library::sealMaterial`, `Library::sealPreset`, and `Library::importEnvelope` put one library row in that envelope. The plaintext carries the id, the kind, the revision, the content hash, and the row. Import opens the blob and calls `chooseRevision`. A remote win replaces that library row. A local win or a conflict writes nothing. These calls do not open the project file.

Nostr is still not implemented. A sync bug must not be able to replace the only copy of a solid. The envelope functions do not read or write the project file. Milestone 6 is not finished: there is still no relay.

## Intent, when it is built

- The CAD tools keep working with no account and no network.
- Identity is a Nostr key. Secret keys are not sent to a relay, not written into a project file, and not written into logs. Typing a raw secret is not the preferred path. Platform signers come first where they exist.
- Only categories the user enables are published: custom materials, presets, and later project revisions.
- Payloads are encrypted before they are published. NIP-44 is the payload cipher to evaluate. NIP-44 does not define multi-device storage, conflict handling, or blob layout, so TimberLab will need its own documented schema.
- This document does not assign an event kind. No TimberLab NIP exists. When events are introduced, the kind, tags, and replacement rules will be written here as an application schema, not described as a standard NIP.
- Cleartext tags must not carry project names or other identifying content. Relays will still see timing, size, and the author's public key. That leakage will be stated in the client.
- Large projects are not stuffed into a relay event. Small encrypted records, a manifest, and content-addressed encrypted blobs stay separate. Local use never requires a relay.
- Conflicting geometry is not resolved by last-write-wins. The first strategy is to keep both revisions and let the user choose. A geometric CRDT is not planned until it is shown to be necessary and safe.
- A remote update must not delete local edits that have not been published.

The current code has the local cipher and the conflict rule, and none of the relay. Offline save and load is still how a project is stored.
