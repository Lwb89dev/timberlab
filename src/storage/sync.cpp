#include "timberlab/storage/sync.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <limits>
#include <memory>

namespace timberlab::storage {
namespace {

struct EvpFree {
  void operator()(EVP_CIPHER_CTX* context) const { EVP_CIPHER_CTX_free(context); }
};

using EvpPtr = std::unique_ptr<EVP_CIPHER_CTX, EvpFree>;

bool cryptoFailed(std::string& error) {
  error = "sync.crypto";
  return false;
}

}  // namespace

bool sealAes256Gcm(const unsigned char key[32], const std::string& plaintext, SealedBlob& sealed, std::string& error) {
  sealed.bytes.clear();
  EvpPtr context(EVP_CIPHER_CTX_new());
  if (!context) return cryptoFailed(error);
  unsigned char nonce[12];
  if (plaintext.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return cryptoFailed(error);
  if (RAND_bytes(nonce, static_cast<int>(sizeof(nonce))) != 1) return cryptoFailed(error);
  if (EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) return cryptoFailed(error);
  if (EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(sizeof(nonce)), nullptr) != 1) {
    return cryptoFailed(error);
  }
  if (EVP_EncryptInit_ex(context.get(), nullptr, nullptr, key, nonce) != 1) return cryptoFailed(error);

  std::vector<unsigned char> cipher(plaintext.size() + 16);
  int written = 0;
  if (!plaintext.empty() &&
      EVP_EncryptUpdate(context.get(), cipher.data(), &written,
                        reinterpret_cast<const unsigned char*>(plaintext.data()),
                        static_cast<int>(plaintext.size())) != 1) {
    return cryptoFailed(error);
  }
  int finalWritten = 0;
  if (EVP_EncryptFinal_ex(context.get(), cipher.data() + written, &finalWritten) != 1) return cryptoFailed(error);
  const int cipherBytes = written + finalWritten;
  unsigned char tag[16];
  if (EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(sizeof(tag)), tag) != 1) {
    return cryptoFailed(error);
  }

  sealed.bytes.reserve(sizeof(nonce) + static_cast<std::size_t>(cipherBytes) + sizeof(tag));
  sealed.bytes.insert(sealed.bytes.end(), nonce, nonce + sizeof(nonce));
  sealed.bytes.insert(sealed.bytes.end(), cipher.begin(), cipher.begin() + cipherBytes);
  sealed.bytes.insert(sealed.bytes.end(), tag, tag + sizeof(tag));
  return true;
}

bool openAes256Gcm(const unsigned char key[32], const SealedBlob& sealed, std::string& plaintext, std::string& error) {
  plaintext.clear();
  constexpr std::size_t kNonce = 12;
  constexpr std::size_t kTag = 16;
  if (sealed.bytes.size() < kNonce + kTag || sealed.bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return cryptoFailed(error);
  }
  const std::size_t cipherBytes = sealed.bytes.size() - kNonce - kTag;
  EvpPtr context(EVP_CIPHER_CTX_new());
  if (!context) return cryptoFailed(error);
  if (EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) return cryptoFailed(error);
  if (EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kNonce), nullptr) != 1) {
    return cryptoFailed(error);
  }
  if (EVP_DecryptInit_ex(context.get(), nullptr, nullptr, key, sealed.bytes.data()) != 1) return cryptoFailed(error);

  std::string opened(cipherBytes, '\0');
  int written = 0;
  if (cipherBytes > 0 &&
      EVP_DecryptUpdate(context.get(), reinterpret_cast<unsigned char*>(opened.data()), &written,
                        sealed.bytes.data() + kNonce, static_cast<int>(cipherBytes)) != 1) {
    plaintext.clear();
    return cryptoFailed(error);
  }
  if (EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(kTag),
                          const_cast<unsigned char*>(sealed.bytes.data() + kNonce + cipherBytes)) != 1) {
    plaintext.clear();
    return cryptoFailed(error);
  }
  int finalWritten = 0;
  unsigned char finalBuffer[16];
  if (EVP_DecryptFinal_ex(context.get(), finalBuffer, &finalWritten) != 1) {
    plaintext.clear();
    return cryptoFailed(error);
  }
  opened.resize(static_cast<std::size_t>(written));
  opened.append(reinterpret_cast<char*>(finalBuffer), static_cast<std::size_t>(finalWritten));
  plaintext = std::move(opened);
  return true;
}

bool chooseRevision(const SyncRecord& local, const SyncRecord& remote, std::string& winner, std::string& error) {
  winner.clear();
  if (local.id != remote.id || local.kind != remote.kind) {
    error = "sync.mismatch";
    return false;
  }
  if (local.revision < 0 || remote.revision < 0) {
    error = "sync.range";
    return false;
  }
  if (local.contentHash == remote.contentHash) {
    winner = "local";
    return true;
  }
  if (local.revision == remote.revision) {
    error = "sync.conflict";
    return false;
  }
  winner = remote.revision > local.revision ? "remote" : "local";
  return true;
}

}  // namespace timberlab::storage
