#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace timberlab::storage {

// A library-shaped record compared before any network exists. The key that
// seals the body is passed in by the caller and is not stored here.
struct SyncRecord {
  std::string id;
  std::string kind;
  std::int64_t revision = 0;
  std::string contentHash;
};

struct SealedBlob {
  // 12-byte nonce, then ciphertext, then the 16-byte GCM tag.
  std::vector<unsigned char> bytes;
};

// AES-256-GCM. The key is 32 bytes. A failed open is sync.crypto and does not
// return a partial plaintext.
bool sealAes256Gcm(const unsigned char key[32], const std::string& plaintext, SealedBlob& sealed, std::string& error);
bool openAes256Gcm(const unsigned char key[32], const SealedBlob& sealed, std::string& plaintext, std::string& error);

// winner is "local" or "remote". Equal content keeps local. The same revision
// with a different hash is sync.conflict. A negative revision is sync.range.
// A different id or kind is sync.mismatch. Nothing is written to disk.
bool chooseRevision(const SyncRecord& local, const SyncRecord& remote, std::string& winner, std::string& error);

}  // namespace timberlab::storage
