# Nostr synchronization

Sign-in exists. Publishing a project does not. Milestone 6 is not finished: no CAD event kind is assigned, and the signer socket cannot replace the `.timberlab` file. See `docs/adr/0006-persistence.md` and `docs/adr/0011-local-sync.md`.

What exists now:

- The versioned `.timberlab` archive (format 1, checksum, atomic rename) and a separate user library (`library.sqlite`, schema version 3) of custom materials and thickness presets. Version 1 migrates through version 2. Version 2 gains a `revision` column, default 1. Preferences stay in their own SQLite schema 1 file.
- `sealAes256Gcm` and `openAes256Gcm` in `include/timberlab/storage/sync.hpp`. The caller passes a 32-byte key. The key is not written into the project, the library, or a file by this code. The packed bytes are a 12-byte random nonce, the ciphertext, and a 16-byte GCM tag. A bad tag or a short buffer is `sync.crypto` and returns no plaintext.
- `chooseRevision` compares two records that already have an id, a kind, a revision, and a content hash. The same hash keeps the local record. The same revision with a different hash is `sync.conflict` and changes nothing. The higher revision wins. A negative revision is `sync.range`. A different id or kind is `sync.mismatch`.
- `Library::sealMaterial`, `Library::sealPreset`, and `Library::importEnvelope` put one library row in that envelope. The plaintext carries the id, the kind, the revision, the content hash, and the row. Import opens the blob and calls `chooseRevision`. A remote win replaces that library row. A local win or a conflict writes nothing. These calls do not open the project file.
- Desktop nsec sign-in. The text is an `nsec1` bech32 string or 64 hex digits. It is checked as a BIP-340 scalar and kept in process memory. The status line shows the npub. The secret is not written into preferences, `library.sqlite`, or the project, and this login opens no socket. Logout clears it with `OPENSSL_cleanse`. Closing the application drops it. A local event can be signed with that key. It is not published.
- Bunker sign-in, NIP-46. The user pastes a `bunker://` link. The client generates an ephemeral key with `RAND_bytes`. That key is not the user's identity and is not written to disk. The connect request asks only for `get_public_key`, is encrypted with NIP-44 to the bunker, and is sent as a kind 24133 event on a `wss://` socket. TLS uses the default peer check. The session is signed in only after a decrypted `ack` and a following `get_public_key` whose result is a 64-hex public key on the curve. Parsing the link is not a completed login. `nostrconnect://` is refused. If a relay sends NIP-42 `AUTH`, the ephemeral key signs kind 22242. The user's nsec is not used for that. Logout clears the local key and does not reopen the socket.
- Amber, NIP-55, is the Android signer. The request is a view intent for package `com.greenart7c3.nostrsigner`, data `nostrsigner:`, type `get_public_key`, and a permission list that does not include `sign_event`. On this Linux build the menu item returns an error and does not start an activity. The JNI path is compiled only for Android. It has not been built or run. This tree has no Android package and no completed Amber login.
- The codec is checked against the published NIP-19, BIP-340, NIP-44, and NIP-01 vectors, and against the WebSocket accept value in RFC 6455. Those tests do not open a network connection.

A sync bug must not be able to replace the only copy of a solid. The envelope functions do not read or write the project file. The signer socket does not call save, load, restore, or library import. Milestone 6 is not finished: nothing publishes a project.

## Intent, when it is built

- The CAD tools keep working with no account and no network.
- Identity is a Nostr key. The user's secret is not sent to a relay, not written into a project file, and not written into logs. On Android the signer is Amber. On the desktop the user can paste an nsec or a bunker link. The nsec stays in memory for the session. The bunker path uses an ephemeral client key, and only that key may answer a relay `AUTH` challenge.
- Only categories the user enables are published: custom materials, presets, and later project revisions.
- Payloads are encrypted before they are published. NIP-44 is the payload cipher to evaluate. NIP-44 does not define multi-device storage, conflict handling, or blob layout, so TimberLab will need its own documented schema.
- This document does not assign an event kind. No TimberLab NIP exists. When events are introduced, the kind, tags, and replacement rules will be written here as an application schema, not described as a standard NIP.
- Cleartext tags must not carry project names or other identifying content. Relays will still see timing, size, and the author's public key. That leakage will be stated in the client.
- Large projects are not stuffed into a relay event. Small encrypted records, a manifest, and content-addressed encrypted blobs stay separate. Local use never requires a relay.
- Conflicting geometry is not resolved by last-write-wins. The first strategy is to keep both revisions and let the user choose. A geometric CRDT is not planned until it is shown to be necessary and safe.
- A remote update must not delete local edits that have not been published.

The current code has the local cipher, the conflict rule, and sign-in. It does not publish a project. Offline save and load is still how a project is stored.
