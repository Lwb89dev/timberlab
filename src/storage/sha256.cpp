#include <openssl/sha.h>

#include <sstream>
#include <string>

namespace timberlab::storage {

std::string sha256Hex(const std::string& data) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash);
  std::ostringstream out;
  out << std::hex;
  for (unsigned char byte : hash) {
    out.width(2);
    out.fill('0');
    out << static_cast<int>(byte);
  }
  return out.str();
}

}  // namespace timberlab::storage
