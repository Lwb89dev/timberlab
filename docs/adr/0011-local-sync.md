# ADR 0011 — Encrypt and compare before any relay

## Status

Accepted for the local half. A Nostr client is not accepted because it does not exist.

## Context

The user library and the project file already round-trip on disk. A sync bug must not be able to replace the only copy of a solid. Publishing comes after the bytes can be sealed and after two revisions can be told apart without a network.

## Decision

`sealAes256Gcm` takes a 32-byte key from the caller and a plaintext. The result is a 12-byte nonce from `RAND_bytes`, the ciphertext, and a 16-byte GCM tag. `openAes256Gcm` returns the plaintext only when the tag matches. Otherwise the error is `sync.crypto` and the plaintext is empty. This code does not generate a key, does not write a key, and does not open a socket.

`chooseRevision` looks at two records. Each has an id, a kind, a non-negative revision, and a content hash. The same hash keeps the local record. The same revision with a different hash is `sync.conflict` and selects neither. Otherwise the higher revision wins. A different id or kind is `sync.mismatch`. Neither function reads or writes the project.

`Library::importEnvelope` opens a sealed library row and calls `chooseRevision`. The library schema is 3, with a revision on each material and preset. A remote win upserts that one row. A local win or a conflict leaves the database unchanged. The call does not take a project path. A test saves a project, imports an envelope, and checks the project bytes are unchanged.

## Consequences

- Milestone 6 is not finished. There is no CAD event kind. A bunker sign-in socket exists and does not import, save, or load a project. An nsec is not written to disk. Amber has not been executed on Android.
- NIP-44 is still the cipher to evaluate when a payload is published. This AES-GCM layout is the local record format, not a claim that a relay will store it.
- A conflict leaves both records with the caller. The function does not merge geometry, and it does not replace a `.timberlab` file.
