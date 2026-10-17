#include "timberlab/storage/sha256.hpp"
#include "timberlab/storage/sync.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using timberlab::storage::SealedBlob;
using timberlab::storage::SyncRecord;
using timberlab::storage::chooseRevision;
using timberlab::storage::openAes256Gcm;
using timberlab::storage::sealAes256Gcm;
using timberlab::storage::sha256Hex;

namespace {

void fillKey(unsigned char key[32], unsigned char seed) {
  for (int i = 0; i < 32; ++i) key[i] = static_cast<unsigned char>(seed + i);
}

SyncRecord record(const char* id, const char* kind, std::int64_t revision, const std::string& hash) {
  SyncRecord item;
  item.id = id;
  item.kind = kind;
  item.revision = revision;
  item.contentHash = hash;
  return item;
}

}  // namespace

TEST_CASE("AES-256-GCM round-trips and rejects a bad tag or key") {
  unsigned char key[32];
  fillKey(key, 1);
  const std::string plaintext("shop oak\n#A67C52");
  SealedBlob sealed;
  std::string error;
  REQUIRE(sealAes256Gcm(key, plaintext, sealed, error));
  REQUIRE(sealed.bytes.size() == plaintext.size() + 28);
  REQUIRE(std::string(sealed.bytes.begin(), sealed.bytes.end()).find(plaintext) == std::string::npos);

  std::string opened;
  REQUIRE(openAes256Gcm(key, sealed, opened, error));
  REQUIRE(opened == plaintext);

  const std::string withNul("a\0b", 3);
  REQUIRE(sealAes256Gcm(key, withNul, sealed, error));
  REQUIRE(openAes256Gcm(key, sealed, opened, error));
  REQUIRE(opened == withNul);

  unsigned char other[32];
  fillKey(other, 9);
  REQUIRE_FALSE(openAes256Gcm(other, sealed, opened, error));
  REQUIRE(error == "sync.crypto");
  REQUIRE(opened.empty());

  REQUIRE(sealAes256Gcm(key, plaintext, sealed, error));
  sealed.bytes.back() ^= 0x01;
  REQUIRE_FALSE(openAes256Gcm(key, sealed, opened, error));
  REQUIRE(error == "sync.crypto");

  sealed.bytes.resize(10);
  REQUIRE_FALSE(openAes256Gcm(key, sealed, opened, error));
  REQUIRE(error == "sync.crypto");
}

TEST_CASE("revision choice keeps equal content and reports a conflict") {
  const std::string oak = sha256Hex("oak");
  const std::string beech = sha256Hex("beech");
  std::string winner;
  std::string error;
  REQUIRE(chooseRevision(record("m1", "material", 2, oak), record("m1", "material", 4, oak), winner, error));
  REQUIRE(winner == "local");

  REQUIRE_FALSE(chooseRevision(record("m1", "material", 3, oak), record("m1", "material", 3, beech), winner, error));
  REQUIRE(error == "sync.conflict");
  REQUIRE(winner.empty());

  REQUIRE(chooseRevision(record("m1", "material", 2, oak), record("m1", "material", 5, beech), winner, error));
  REQUIRE(winner == "remote");
  REQUIRE(chooseRevision(record("m1", "material", 6, oak), record("m1", "material", 5, beech), winner, error));
  REQUIRE(winner == "local");

  REQUIRE_FALSE(chooseRevision(record("m1", "material", -1, oak), record("m1", "material", 1, beech), winner, error));
  REQUIRE(error == "sync.range");
  REQUIRE_FALSE(chooseRevision(record("m1", "material", 1, oak), record("m2", "material", 2, beech), winner, error));
  REQUIRE(error == "sync.mismatch");
  REQUIRE_FALSE(chooseRevision(record("m1", "material", 1, oak), record("m1", "preset", 2, beech), winner, error));
  REQUIRE(error == "sync.mismatch");
}
