# ADR 0011 — Encrypt and compare before any relay

## Status

Accepted for the local half and for a library event file. A relay client that stores the event is not accepted: this build did not publish one.

## Context

The user library and the project file already round-trip on disk. A sync bug must not be able to replace the only copy of a solid. Publishing comes after the bytes can be sealed and after two revisions can be told apart without a network.

## Decision

`sealAes256Gcm` takes a 32-byte key from the caller and a plaintext. The result is a 12-byte nonce from `RAND_bytes`, the ciphertext, and a 16-byte GCM tag. `openAes256Gcm` returns the plaintext only when the tag matches. Otherwise the error is `sync.crypto` and the plaintext is empty. This code does not generate a key, does not write a key, and does not open a socket.

`chooseRevision` looks at two records. Each has an id, a kind, a non-negative revision, and a content hash. The same hash keeps the local record. The same revision with a different hash is `sync.conflict` and selects neither. Otherwise the higher revision wins. A different id or kind is `sync.mismatch`. Neither function reads or writes the project.

`Library::importEnvelope` opens a sealed library row and calls `chooseRevision`. The library schema is 3, with a revision on each material and preset. A remote win upserts that one row. A local win or a conflict leaves the database unchanged. The call does not take a project path. A test saves a project, imports an envelope, and checks the project bytes are unchanged.

`importPlaintext` is the same compare after the ciphertext is already open. A library event is kind 30078, NIP-78's addressable kind, with a TimberLab `d` tag and NIP-44 ciphertext for the author's own key. The plaintext is the envelope JSON. `sealLibraryEvent` does not open a socket. A test saves a project, imports the opened plaintext, and checks those project bytes too. AES-GCM remains the local record format. NIP-44 is the event content. Neither is a claim that a relay stored the row.

## Consequences

- Milestone 6 is not finished. The library event is an application schema for one row. A project is not that event. A bunker sign-in socket exists and does not import, save, or load a project. An nsec is not written to disk. Amber has not been executed on Android. No event from this tree was sent to a relay.
- NIP-44 encrypts the library event to the author's own key. AES-GCM remains the local record format. Neither result was stored by a relay in this build.
- A conflict leaves both records with the caller. The function does not merge geometry, and it does not replace a `.timberlab` file.
