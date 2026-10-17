# Nostr synchronization

Synchronization is not implemented, and it stays that way until local persistence is in place. See `docs/adr/0006-persistence.md`. The application does not open a socket, does not generate a key, and does not publish an event.

What exists now is the versioned `.timberlab` archive (format 1, checksum, atomic rename) and a separate user library (`library.sqlite`, schema version 2, migrated from version 1) of custom materials and thickness presets. Preferences stay in their own SQLite schema 1 file. The library round-trips with no network. Nostr is still not implemented: this program does not open a socket, generate a key, or publish an event. A sync bug must not be able to replace the only copy of a solid.

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

The current code has none of this. Offline save and load is the whole storage story.
