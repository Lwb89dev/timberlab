#include "timberlab/storage/nostr.hpp"

#include <nlohmann/json.hpp>

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace timberlab::storage {
namespace {

constexpr std::size_t kMaxPlaintext = 131072;
constexpr std::size_t kMaxFrame = 262144;
constexpr char kBech32[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
constexpr char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr char kWsGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

void wipeString(std::string& text) {
  if (!text.empty()) OPENSSL_cleanse(text.data(), text.size());
  text.clear();
}

// Clears a buffer that held key material when the scope ends, on every return
// path. The caller reserves capacity first, so growth does not leave an
// uncleared copy behind.
template <typename Container>
class WipeOnExit {
 public:
  explicit WipeOnExit(Container& container) : container_(container) {}
  ~WipeOnExit() {
    if (!container_.empty()) OPENSSL_cleanse(container_.data(), container_.size() * sizeof(container_[0]));
  }
  WipeOnExit(const WipeOnExit&) = delete;
  WipeOnExit& operator=(const WipeOnExit&) = delete;

 private:
  Container& container_;
};

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool lowerHex(std::string_view text, std::size_t bytes) {
  if (text.size() != bytes * 2) return false;
  for (char c : text) {
    if (c >= '0' && c <= '9') continue;
    if (c >= 'a' && c <= 'f') continue;
    return false;
  }
  return true;
}

std::string trimCopy(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
  return std::string(text.substr(begin, end - begin));
}

std::string base64Encode(const unsigned char* data, std::size_t size) {
  std::string out;
  out.reserve(((size + 2) / 3) * 4);
  std::size_t index = 0;
  while (index + 3 <= size) {
    const unsigned value = (static_cast<unsigned>(data[index]) << 16) |
                           (static_cast<unsigned>(data[index + 1]) << 8) | data[index + 2];
    out.push_back(kBase64[(value >> 18) & 63]);
    out.push_back(kBase64[(value >> 12) & 63]);
    out.push_back(kBase64[(value >> 6) & 63]);
    out.push_back(kBase64[value & 63]);
    index += 3;
  }
  if (index >= size) return out;
  unsigned value = static_cast<unsigned>(data[index]) << 16;
  const bool second = index + 1 < size;
  if (second) value |= static_cast<unsigned>(data[index + 1]) << 8;
  out.push_back(kBase64[(value >> 18) & 63]);
  out.push_back(kBase64[(value >> 12) & 63]);
  out.push_back(second ? kBase64[(value >> 6) & 63] : '=');
  out.push_back('=');
  return out;
}

int base64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

bool base64Decode(std::string_view text, std::vector<unsigned char>& out) {
  if (text.size() % 4 != 0) return false;
  out.clear();
  out.reserve(text.size() / 4 * 3);
  for (std::size_t index = 0; index < text.size(); index += 4) {
    const int values[4] = {base64Value(text[index]), base64Value(text[index + 1]), base64Value(text[index + 2]),
                           base64Value(text[index + 3])};
    const int pads = (text[index + 2] == '=' ? 1 : 0) + (text[index + 3] == '=' ? 1 : 0);
    if (pads > 2 || (pads != 0 && index + 4 != text.size())) return false;
    if (values[0] < 0 || values[1] < 0) return false;
    if (pads < 2 && values[2] < 0) return false;
    if (pads < 1 && values[3] < 0) return false;
    out.push_back(static_cast<unsigned char>((values[0] << 2) | (values[1] >> 4)));
    if (pads == 2) continue;
    out.push_back(static_cast<unsigned char>(((values[1] & 15) << 4) | (values[2] >> 2)));
    if (pads == 1) continue;
    out.push_back(static_cast<unsigned char>(((values[2] & 3) << 6) | values[3]));
  }
  return true;
}

std::uint32_t bech32Polymod(const std::vector<int>& values) {
  static constexpr std::uint32_t kGen[] = {0x3b6a57b2u, 0x26508e6du, 0x1ea119fau, 0x3d4233ddu, 0x2a1462b3u};
  std::uint32_t chk = 1;
  for (int value : values) {
    const std::uint32_t top = chk >> 25;
    chk = ((chk & 0x1ffffffu) << 5) ^ static_cast<std::uint32_t>(value);
    for (int bit = 0; bit < 5; ++bit) {
      if (((top >> bit) & 1u) != 0) chk ^= kGen[bit];
    }
  }
  return chk;
}

std::vector<int> hrpExpand(std::string_view hrp) {
  std::vector<int> out;
  out.reserve(hrp.size() * 2 + 1);
  for (unsigned char c : hrp) out.push_back(c >> 5);
  out.push_back(0);
  for (unsigned char c : hrp) out.push_back(c & 31);
  return out;
}

bool convertBits(const int* input, std::size_t count, int from, int to, bool pad, std::vector<int>& output) {
  std::uint32_t acc = 0;
  int bits = 0;
  const std::uint32_t maxv = (1u << to) - 1u;
  const std::uint32_t maxAcc = (1u << (from + to - 1)) - 1u;
  for (std::size_t index = 0; index < count; ++index) {
    if (input[index] < 0 || (input[index] >> from) != 0) return false;
    acc = ((acc << from) | static_cast<std::uint32_t>(input[index])) & maxAcc;
    bits += from;
    while (bits >= to) {
      bits -= to;
      output.push_back(static_cast<int>((acc >> bits) & maxv));
    }
  }
  if (pad) {
    if (bits > 0) output.push_back(static_cast<int>((acc << (to - bits)) & maxv));
    return true;
  }
  if (bits >= from) return false;
  return ((acc << (to - bits)) & maxv) == 0;
}

bool bech32Decode(std::string_view text, std::string& hrp, std::vector<unsigned char>& data) {
  if (text.empty() || text.size() > 5000) return false;
  bool lower = false;
  bool upper = false;
  std::string folded(text.size(), '\0');
  const WipeOnExit foldedGuard(folded);
  for (std::size_t index = 0; index < text.size(); ++index) {
    const unsigned char c = static_cast<unsigned char>(text[index]);
    if (c >= 'a' && c <= 'z') lower = true;
    if (c >= 'A' && c <= 'Z') upper = true;
    folded[index] = static_cast<char>(std::tolower(c));
  }
  if (lower && upper) return false;
  const auto separator = folded.rfind('1');
  if (separator == std::string::npos || separator == 0 || separator + 7 > folded.size()) return false;
  hrp = folded.substr(0, separator);
  for (unsigned char c : hrp) {
    if (c < 33 || c > 126) return false;
  }
  std::vector<int> values;
  values.reserve(folded.size() - separator - 1);
  const WipeOnExit valuesGuard(values);
  for (std::size_t index = separator + 1; index < folded.size(); ++index) {
    // A NUL byte would match the terminator of kBech32.
    const char* found = folded[index] == '\0' ? nullptr : std::strchr(kBech32, folded[index]);
    if (found == nullptr) return false;
    values.push_back(static_cast<int>(found - kBech32));
  }
  std::vector<int> check = hrpExpand(hrp);
  check.reserve(check.size() + values.size());
  const WipeOnExit checkGuard(check);
  check.insert(check.end(), values.begin(), values.end());
  if (bech32Polymod(check) != 1) return false;
  values.resize(values.size() - 6);
  std::vector<int> bytes;
  bytes.reserve(values.size() * 5 / 8 + 1);
  const WipeOnExit bytesGuard(bytes);
  if (!convertBits(values.data(), values.size(), 5, 8, false, bytes)) return false;
  data.reserve(bytes.size());
  data.assign(bytes.begin(), bytes.end());
  return true;
}

bool bech32Encode(std::string_view hrp, const unsigned char* raw, std::size_t size, std::string& text) {
  std::vector<int> input(size);
  for (std::size_t index = 0; index < size; ++index) input[index] = raw[index];
  std::vector<int> values;
  if (!convertBits(input.data(), input.size(), 8, 5, true, values)) return false;
  std::vector<int> check = hrpExpand(hrp);
  check.insert(check.end(), values.begin(), values.end());
  check.insert(check.end(), 6, 0);
  const std::uint32_t polymod = bech32Polymod(check) ^ 1u;
  text.assign(hrp);
  text.push_back('1');
  for (int value : values) text.push_back(kBech32[value]);
  for (int shift = 0; shift < 6; ++shift) {
    const int symbol = static_cast<int>((polymod >> (5 * (5 - shift))) & 31);
    text.push_back(kBech32[symbol]);
  }
  return true;
}

bool decodeKey(std::string_view text, std::string_view hrp, unsigned char out[32]) {
  std::string found;
  std::vector<unsigned char> data;
  const WipeOnExit dataGuard(data);
  if (!bech32Decode(text, found, data)) return false;
  if (found != hrp || data.size() != 32) return false;
  std::memcpy(out, data.data(), 32);
  return true;
}

bool hmacSha256(const unsigned char* key, std::size_t keyLen, const unsigned char* data, std::size_t dataLen,
                unsigned char out[32]) {
  EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
  if (mac == nullptr) return false;
  EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(mac);
  char digest[] = "SHA256";
  OSSL_PARAM params[] = {
      OSSL_PARAM_construct_utf8_string("digest", digest, 0),
      OSSL_PARAM_construct_end(),
  };
  bool ok = ctx != nullptr && EVP_MAC_init(ctx, key, keyLen, params) == 1;
  if (ok && dataLen > 0) ok = EVP_MAC_update(ctx, data, dataLen) == 1;
  std::size_t written = 0;
  if (ok) ok = EVP_MAC_final(ctx, out, &written, 32) == 1 && written == 32;
  EVP_MAC_CTX_free(ctx);
  EVP_MAC_free(mac);
  return ok;
}

bool hkdfExpand(const unsigned char prk[32], const unsigned char* info, std::size_t infoLen, unsigned char* out,
                std::size_t outLen) {
  unsigned char block[32];
  unsigned char previous[32];
  std::size_t filled = 0;
  unsigned char counter = 1;
  std::vector<unsigned char> message;
  message.reserve(32 + infoLen + 1);
  const WipeOnExit messageGuard(message);
  while (filled < outLen) {
    message.clear();
    if (counter > 1) message.insert(message.end(), previous, previous + 32);
    message.insert(message.end(), info, info + infoLen);
    message.push_back(counter);
    if (!hmacSha256(prk, 32, message.data(), message.size(), block)) {
      OPENSSL_cleanse(block, sizeof(block));
      OPENSSL_cleanse(previous, sizeof(previous));
      return false;
    }
    const std::size_t take = std::min<std::size_t>(32, outLen - filled);
    std::memcpy(out + filled, block, take);
    std::memcpy(previous, block, 32);
    filled += take;
    ++counter;
  }
  OPENSSL_cleanse(block, sizeof(block));
  OPENSSL_cleanse(previous, sizeof(previous));
  return true;
}

bool chacha20(const unsigned char key[32], const unsigned char nonce[12], const unsigned char* input,
              std::size_t size, unsigned char* output) {
  unsigned char iv[16] = {};
  std::memcpy(iv + 4, nonce, 12);
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (ctx == nullptr) return false;
  int written = 0;
  int finalBytes = 0;
  bool ok = EVP_EncryptInit_ex(ctx, EVP_chacha20(), nullptr, key, iv) == 1;
  if (ok && size > 0) ok = EVP_EncryptUpdate(ctx, output, &written, input, static_cast<int>(size)) == 1;
  if (ok) ok = EVP_EncryptFinal_ex(ctx, output + written, &finalBytes) == 1;
  ok = ok && static_cast<std::size_t>(written + finalBytes) == size;
  EVP_CIPHER_CTX_free(ctx);
  OPENSSL_cleanse(iv, sizeof(iv));
  return ok;
}

std::uint64_t paddedLength(std::uint64_t unpadded) {
  if (unpadded <= 32) return 32;
  const std::uint64_t nextPower = std::uint64_t{1} << std::bit_width(unpadded - 1);
  const std::uint64_t chunk = nextPower <= 256 ? 32 : nextPower / 8;
  return chunk * ((unpadded - 1) / chunk + 1);
}

bool utf8Ok(std::string_view text) {
  const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
  std::size_t index = 0;
  while (index < text.size()) {
    const unsigned char lead = bytes[index];
    if (lead < 0x80) {
      ++index;
      continue;
    }
    int need = 0;
    if ((lead & 0xe0) == 0xc0 && lead >= 0xc2) need = 1;
    else if ((lead & 0xf0) == 0xe0) need = 2;
    else if ((lead & 0xf8) == 0xf0 && lead <= 0xf4) need = 3;
    else return false;
    if (index + static_cast<std::size_t>(need) >= text.size()) return false;
    for (int extra = 1; extra <= need; ++extra) {
      if ((bytes[index + static_cast<std::size_t>(extra)] & 0xc0) != 0x80) return false;
    }
    if (lead == 0xe0 && bytes[index + 1] < 0xa0) return false;
    if (lead == 0xed && bytes[index + 1] >= 0xa0) return false;
    if (lead == 0xf0 && bytes[index + 1] < 0x90) return false;
    if (lead == 0xf4 && bytes[index + 1] >= 0x90) return false;
    index += static_cast<std::size_t>(need) + 1;
  }
  return true;
}

void appendJsonString(std::string& out, std::string_view text) {
  out.push_back('"');
  for (unsigned char c : text) {
    if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if (c == '\b') out += "\\b";
    else if (c == '\f') out += "\\f";
    else if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else out.push_back(static_cast<char>(c));
  }
  out.push_back('"');
}

void appendTag(std::string& out, const std::vector<std::string>& tag) {
  out.push_back('[');
  for (std::size_t index = 0; index < tag.size(); ++index) {
    if (index != 0) out.push_back(',');
    appendJsonString(out, tag[index]);
  }
  out.push_back(']');
}

bool percentDecode(std::string_view text, std::string& out) {
  out.clear();
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] != '%') {
      out.push_back(text[index]);
      continue;
    }
    if (index + 2 >= text.size()) return false;
    const int hi = hexValue(text[index + 1]);
    const int lo = hexValue(text[index + 2]);
    if (hi < 0 || lo < 0) return false;
    out.push_back(static_cast<char>((hi << 4) | lo));
    index += 2;
  }
  return true;
}

bool headerValue(std::string_view headers, std::string_view name, std::string& value) {
  std::size_t cursor = 0;
  while (cursor < headers.size()) {
    auto end = headers.find("\r\n", cursor);
    if (end == std::string_view::npos) end = headers.size();
    const std::string_view line = headers.substr(cursor, end - cursor);
    const auto colon = line.find(':');
    if (colon != std::string_view::npos) {
      std::string found(line.substr(0, colon));
      std::transform(found.begin(), found.end(), found.begin(), [](unsigned char c) { return std::tolower(c); });
      if (found == name) {
        std::string_view raw = line.substr(colon + 1);
        while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front())) != 0) raw.remove_prefix(1);
        while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back())) != 0) raw.remove_suffix(1);
        value.assign(raw);
        return true;
      }
    }
    if (end == headers.size()) break;
    cursor = end + 2;
  }
  return false;
}

bool containsToken(std::string_view header, std::string_view token) {
  std::string lower(header);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
  return lower.find(token) != std::string::npos;
}

nlohmann::json eventJson(const Event& event) {
  nlohmann::json tags = nlohmann::json::array();
  for (const auto& tag : event.tags) tags.push_back(tag);
  return nlohmann::json{{"id", event.id},
                        {"pubkey", event.pubkey},
                        {"created_at", event.createdAt},
                        {"kind", event.kind},
                        {"tags", std::move(tags)},
                        {"content", event.content},
                        {"sig", event.sig}};
}

bool eventFromJson(const nlohmann::json& body, Event& event) {
  if (!body.is_object()) return false;
  if (!body.contains("id") || !body.contains("pubkey") || !body.contains("created_at") || !body.contains("kind") ||
      !body.contains("tags") || !body.contains("content") || !body.contains("sig")) {
    return false;
  }
  if (!body["id"].is_string() || !body["pubkey"].is_string() || !body["sig"].is_string() ||
      !body["content"].is_string() || !body["created_at"].is_number_integer() || !body["kind"].is_number_integer() ||
      !body["tags"].is_array()) {
    return false;
  }
  event.id = body["id"].get<std::string>();
  event.pubkey = body["pubkey"].get<std::string>();
  event.sig = body["sig"].get<std::string>();
  event.content = body["content"].get<std::string>();
  event.createdAt = body["created_at"].get<std::int64_t>();
  const auto kind = body["kind"].get<std::int64_t>();
  if (kind < 0 || kind > 65535 || event.createdAt < 0) return false;
  event.kind = static_cast<int>(kind);
  event.tags.clear();
  for (const auto& tag : body["tags"]) {
    if (!tag.is_array()) return false;
    std::vector<std::string> row;
    for (const auto& item : tag) {
      if (!item.is_string()) return false;
      row.push_back(item.get<std::string>());
    }
    event.tags.push_back(std::move(row));
  }
  return true;
}

}  // namespace

bool parseHex(std::string_view hex, unsigned char* out, std::size_t outLen) {
  if (hex.size() != outLen * 2) return false;
  for (std::size_t index = 0; index < outLen; ++index) {
    const int hi = hexValue(hex[index * 2]);
    const int lo = hexValue(hex[index * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[index] = static_cast<unsigned char>((hi << 4) | lo);
  }
  return true;
}

std::string hexEncode(const unsigned char* data, std::size_t size) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(size * 2, '0');
  for (std::size_t index = 0; index < size; ++index) {
    out[index * 2] = kHex[data[index] >> 4];
    out[index * 2 + 1] = kHex[data[index] & 0x0f];
  }
  return out;
}

bool decodeNsec(std::string_view text, unsigned char secret[32], std::string& error) {
  std::string trimmed = trimCopy(text);
  const bool ok = decodeKey(trimmed, "nsec", secret);
  wipeString(trimmed);
  if (!ok) error = "signer.nsec";
  return ok;
}

bool encodeNsec(const unsigned char secret[32], std::string& text, std::string& error) {
  if (!bech32Encode("nsec", secret, 32, text)) {
    error = "signer.nsec";
    return false;
  }
  return true;
}

bool decodeNpub(std::string_view text, unsigned char pubkey[32], std::string& error) {
  if (!decodeKey(trimCopy(text), "npub", pubkey)) {
    error = "signer.nsec";
    return false;
  }
  return true;
}

bool encodeNpub(const unsigned char pubkey[32], std::string& text, std::string& error) {
  if (!bech32Encode("npub", pubkey, 32, text)) {
    error = "signer.nsec";
    return false;
  }
  return true;
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace {

struct Secp {
  EC_GROUP* group = nullptr;
  BN_CTX* ctx = nullptr;
  BIGNUM* order = nullptr;
  BIGNUM* field = nullptr;

  Secp() {
    group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    ctx = BN_CTX_new();
    order = BN_new();
    field = BN_new();
    if (group == nullptr || ctx == nullptr || order == nullptr || field == nullptr) return;
    if (EC_GROUP_get_order(group, order, ctx) != 1 || EC_GROUP_get_curve(group, field, nullptr, nullptr, ctx) != 1) {
      EC_GROUP_free(group);
      group = nullptr;
    }
  }

  ~Secp() {
    BN_free(field);
    BN_free(order);
    BN_CTX_free(ctx);
    EC_GROUP_free(group);
  }

  bool ok() const { return group != nullptr; }
  Secp(const Secp&) = delete;
  Secp& operator=(const Secp&) = delete;
};

struct Bn {
  BIGNUM* v = nullptr;
  Bn() : v(BN_new()) {}
  ~Bn() { BN_clear_free(v); }
  Bn(const Bn&) = delete;
  Bn& operator=(const Bn&) = delete;
  BIGNUM* get() const { return v; }
};

struct EcPoint {
  EC_POINT* v = nullptr;
  explicit EcPoint(const EC_GROUP* group) : v(EC_POINT_new(group)) {}
  ~EcPoint() { EC_POINT_clear_free(v); }
  EcPoint(const EcPoint&) = delete;
  EcPoint& operator=(const EcPoint&) = delete;
  EC_POINT* get() const { return v; }
};

bool loadScalar(const Secp& secp, const unsigned char secret[32], Bn& out) {
  if (BN_bin2bn(secret, 32, out.get()) == nullptr) return false;
  BN_set_flags(out.get(), BN_FLG_CONSTTIME);
  return !BN_is_zero(out.get()) && BN_ucmp(out.get(), secp.order) < 0;
}

bool pointX(const Secp& secp, const EC_POINT* point, unsigned char out[32], bool& evenY) {
  if (EC_POINT_is_at_infinity(secp.group, point) == 1) return false;
  Bn x;
  Bn y;
  if (EC_POINT_get_affine_coordinates(secp.group, point, x.get(), y.get(), secp.ctx) != 1) return false;
  evenY = !BN_is_odd(y.get());
  return BN_bn2binpad(x.get(), out, 32) == 32;
}

bool liftX(const Secp& secp, const unsigned char raw[32], EcPoint& point) {
  Bn x;
  if (BN_bin2bn(raw, 32, x.get()) == nullptr) return false;
  if (BN_ucmp(x.get(), secp.field) >= 0) return false;
  return EC_POINT_set_compressed_coordinates(secp.group, point.get(), x.get(), 0, secp.ctx) == 1;
}

void taggedHash(const char* tag, const unsigned char* message, std::size_t size, unsigned char out[32]) {
  unsigned char tagHash[32];
  SHA256(reinterpret_cast<const unsigned char*>(tag), std::strlen(tag), tagHash);
  SHA256_CTX ctx;
  SHA256_Init(&ctx);
  SHA256_Update(&ctx, tagHash, 32);
  SHA256_Update(&ctx, tagHash, 32);
  if (size > 0) SHA256_Update(&ctx, message, size);
  SHA256_Final(out, &ctx);
}

}  // namespace

bool publicKeyFromSecret(const unsigned char secret[32], unsigned char pubkey[32], std::string& error) {
  Secp secp;
  if (!secp.ok()) {
    error = "signer.crypto";
    return false;
  }
  Bn scalar;
  if (!loadScalar(secp, secret, scalar)) {
    error = "signer.nsec";
    return false;
  }
  EcPoint point(secp.group);
  if (EC_POINT_mul(secp.group, point.get(), scalar.get(), nullptr, nullptr, secp.ctx) != 1) {
    error = "signer.crypto";
    return false;
  }
  bool even = false;
  if (!pointX(secp, point.get(), pubkey, even)) {
    error = "signer.crypto";
    return false;
  }
  return true;
}

bool xOnlyOnCurve(const unsigned char pubkey[32]) {
  Secp secp;
  if (!secp.ok()) return false;
  EcPoint point(secp.group);
  return liftX(secp, pubkey, point);
}

bool schnorrSign(const unsigned char secret[32], const unsigned char* message, std::size_t messageLen,
                 const unsigned char aux[32], unsigned char signature[64], std::string& error) {
  Secp secp;
  if (!secp.ok()) {
    error = "signer.crypto";
    return false;
  }
  Bn scalar;
  if (!loadScalar(secp, secret, scalar)) {
    error = "signer.nsec";
    return false;
  }
  EcPoint point(secp.group);
  if (EC_POINT_mul(secp.group, point.get(), scalar.get(), nullptr, nullptr, secp.ctx) != 1) {
    error = "signer.crypto";
    return false;
  }
  unsigned char px[32];
  bool even = false;
  if (!pointX(secp, point.get(), px, even)) {
    error = "signer.crypto";
    return false;
  }
  if (!even && BN_sub(scalar.get(), secp.order, scalar.get()) != 1) {
    error = "signer.crypto";
    return false;
  }
  unsigned char secretBytes[32];
  unsigned char auxHash[32];
  unsigned char tweak[32];
  if (BN_bn2binpad(scalar.get(), secretBytes, 32) != 32) {
    error = "signer.crypto";
    return false;
  }
  taggedHash("BIP0340/aux", aux, 32, auxHash);
  for (int index = 0; index < 32; ++index) tweak[index] = static_cast<unsigned char>(secretBytes[index] ^ auxHash[index]);
  std::vector<unsigned char> nonceInput;
  nonceInput.reserve(64 + messageLen);
  const WipeOnExit nonceInputGuard(nonceInput);
  nonceInput.insert(nonceInput.end(), tweak, tweak + 32);
  nonceInput.insert(nonceInput.end(), px, px + 32);
  if (messageLen > 0) nonceInput.insert(nonceInput.end(), message, message + messageLen);
  unsigned char nonceHash[32];
  taggedHash("BIP0340/nonce", nonceInput.data(), nonceInput.size(), nonceHash);
  Bn nonce;
  BN_set_flags(nonce.get(), BN_FLG_CONSTTIME);
  if (BN_bin2bn(nonceHash, 32, nonce.get()) == nullptr || BN_mod(nonce.get(), nonce.get(), secp.order, secp.ctx) != 1 ||
      BN_is_zero(nonce.get())) {
    error = "signer.crypto";
    OPENSSL_cleanse(secretBytes, sizeof(secretBytes));
    OPENSSL_cleanse(tweak, sizeof(tweak));
    OPENSSL_cleanse(nonceHash, sizeof(nonceHash));
    return false;
  }
  OPENSSL_cleanse(nonceHash, sizeof(nonceHash));
  EcPoint noncePoint(secp.group);
  unsigned char rx[32];
  bool nonceEven = false;
  if (EC_POINT_mul(secp.group, noncePoint.get(), nonce.get(), nullptr, nullptr, secp.ctx) != 1 ||
      !pointX(secp, noncePoint.get(), rx, nonceEven)) {
    error = "signer.crypto";
    OPENSSL_cleanse(secretBytes, sizeof(secretBytes));
    OPENSSL_cleanse(tweak, sizeof(tweak));
    return false;
  }
  if (!nonceEven && BN_sub(nonce.get(), secp.order, nonce.get()) != 1) {
    error = "signer.crypto";
    OPENSSL_cleanse(secretBytes, sizeof(secretBytes));
    OPENSSL_cleanse(tweak, sizeof(tweak));
    return false;
  }
  std::vector<unsigned char> challengeInput;
  challengeInput.insert(challengeInput.end(), rx, rx + 32);
  challengeInput.insert(challengeInput.end(), px, px + 32);
  if (messageLen > 0) challengeInput.insert(challengeInput.end(), message, message + messageLen);
  unsigned char challengeHash[32];
  taggedHash("BIP0340/challenge", challengeInput.data(), challengeInput.size(), challengeHash);
  Bn challenge;
  Bn product;
  Bn sum;
  bool ok = BN_bin2bn(challengeHash, 32, challenge.get()) != nullptr &&
            BN_mod(challenge.get(), challenge.get(), secp.order, secp.ctx) == 1 &&
            BN_mod_mul(product.get(), challenge.get(), scalar.get(), secp.order, secp.ctx) == 1 &&
            BN_mod_add(sum.get(), nonce.get(), product.get(), secp.order, secp.ctx) == 1 &&
            BN_bn2binpad(sum.get(), signature + 32, 32) == 32;
  if (ok) std::memcpy(signature, rx, 32);
  else error = "signer.crypto";
  OPENSSL_cleanse(secretBytes, sizeof(secretBytes));
  OPENSSL_cleanse(tweak, sizeof(tweak));
  return ok;
}

bool schnorrVerify(const unsigned char pubkey[32], const unsigned char* message, std::size_t messageLen,
                   const unsigned char signature[64]) {
  Secp secp;
  if (!secp.ok()) return false;
  Bn r;
  Bn s;
  if (BN_bin2bn(signature, 32, r.get()) == nullptr || BN_ucmp(r.get(), secp.field) >= 0) return false;
  if (BN_bin2bn(signature + 32, 32, s.get()) == nullptr || BN_ucmp(s.get(), secp.order) >= 0) return false;
  EcPoint point(secp.group);
  if (!liftX(secp, pubkey, point)) return false;
  std::vector<unsigned char> challengeInput;
  challengeInput.insert(challengeInput.end(), signature, signature + 32);
  challengeInput.insert(challengeInput.end(), pubkey, pubkey + 32);
  if (messageLen > 0) challengeInput.insert(challengeInput.end(), message, message + messageLen);
  unsigned char challengeHash[32];
  taggedHash("BIP0340/challenge", challengeInput.data(), challengeInput.size(), challengeHash);
  Bn challenge;
  Bn coefficient;
  if (BN_bin2bn(challengeHash, 32, challenge.get()) == nullptr) return false;
  if (BN_mod(challenge.get(), challenge.get(), secp.order, secp.ctx) != 1) return false;
  if (BN_mod_sub(coefficient.get(), secp.order, challenge.get(), secp.order, secp.ctx) != 1) return false;
  EcPoint nonce(secp.group);
  if (EC_POINT_mul(secp.group, nonce.get(), s.get(), point.get(), coefficient.get(), secp.ctx) != 1) return false;
  unsigned char rx[32];
  bool even = false;
  if (!pointX(secp, nonce.get(), rx, even) || !even) return false;
  return CRYPTO_memcmp(rx, signature, 32) == 0;
}

bool conversationKey(const unsigned char secret[32], const unsigned char pubkey[32], unsigned char out[32],
                     std::string& error) {
  Secp secp;
  if (!secp.ok()) {
    error = "signer.crypto";
    return false;
  }
  Bn scalar;
  if (!loadScalar(secp, secret, scalar)) {
    error = "signer.nsec";
    return false;
  }
  EcPoint point(secp.group);
  if (!liftX(secp, pubkey, point)) {
    error = "signer.crypto";
    return false;
  }
  EcPoint shared(secp.group);
  if (EC_POINT_mul(secp.group, shared.get(), nullptr, point.get(), scalar.get(), secp.ctx) != 1) {
    error = "signer.crypto";
    return false;
  }
  unsigned char sharedX[32];
  bool even = false;
  if (!pointX(secp, shared.get(), sharedX, even)) {
    error = "signer.crypto";
    return false;
  }
  const auto* salt = reinterpret_cast<const unsigned char*>("nip44-v2");
  const bool ok = hmacSha256(salt, 9, sharedX, 32, out);
  OPENSSL_cleanse(sharedX, sizeof(sharedX));
  if (!ok) error = "signer.crypto";
  return ok;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

bool nip44Encrypt(const unsigned char key[32], std::string_view plaintext, const unsigned char nonce[32],
                  std::string& payload, std::string& error) {
  if (plaintext.empty() || plaintext.size() > kMaxPlaintext || !utf8Ok(plaintext)) {
    error = "signer.crypto";
    return false;
  }
  unsigned char keys[76];
  if (!hkdfExpand(key, nonce, 32, keys, sizeof(keys))) {
    error = "signer.crypto";
    return false;
  }
  const std::uint64_t body = paddedLength(plaintext.size());
  const bool extended = plaintext.size() >= 65536;
  const std::size_t prefix = extended ? 6 : 2;
  std::vector<unsigned char> padded(prefix + static_cast<std::size_t>(body), 0);
  if (extended) {
    const auto length = static_cast<std::uint32_t>(plaintext.size());
    padded[2] = static_cast<unsigned char>((length >> 24) & 0xff);
    padded[3] = static_cast<unsigned char>((length >> 16) & 0xff);
    padded[4] = static_cast<unsigned char>((length >> 8) & 0xff);
    padded[5] = static_cast<unsigned char>(length & 0xff);
  } else {
    const auto length = static_cast<std::uint16_t>(plaintext.size());
    padded[0] = static_cast<unsigned char>((length >> 8) & 0xff);
    padded[1] = static_cast<unsigned char>(length & 0xff);
  }
  std::memcpy(padded.data() + prefix, plaintext.data(), plaintext.size());
  std::vector<unsigned char> cipher(padded.size());
  unsigned char mac[32];
  bool ok = chacha20(keys, keys + 32, padded.data(), padded.size(), cipher.data());
  if (ok) {
    std::vector<unsigned char> aad(nonce, nonce + 32);
    aad.insert(aad.end(), cipher.begin(), cipher.end());
    ok = hmacSha256(keys + 44, 32, aad.data(), aad.size(), mac);
  }
  if (ok) {
    std::vector<unsigned char> raw;
    raw.reserve(1 + 32 + cipher.size() + 32);
    raw.push_back(2);
    raw.insert(raw.end(), nonce, nonce + 32);
    raw.insert(raw.end(), cipher.begin(), cipher.end());
    raw.insert(raw.end(), mac, mac + 32);
    payload = base64Encode(raw.data(), raw.size());
  } else {
    error = "signer.crypto";
  }
  OPENSSL_cleanse(keys, sizeof(keys));
  if (!padded.empty()) OPENSSL_cleanse(padded.data(), padded.size());
  return ok;
}

bool nip44Decrypt(const unsigned char key[32], std::string_view payload, std::string& plaintext, std::string& error) {
  error = "signer.crypto";
  plaintext.clear();
  if (payload.empty() || payload.front() == '#') return false;
  if (payload.size() < 132 || payload.size() > ((1 + 32 + kMaxPlaintext + 65536 + 32 + 2) / 3 + 1) * 4) return false;
  std::vector<unsigned char> raw;
  if (!base64Decode(payload, raw) || raw.size() < 99 || raw[0] != 2) return false;
  const unsigned char* nonce = raw.data() + 1;
  if (raw.size() < 1 + 32 + 32) return false;
  const std::size_t cipherLen = raw.size() - 65;
  const unsigned char* cipher = raw.data() + 33;
  const unsigned char* mac = raw.data() + raw.size() - 32;
  unsigned char keys[76];
  if (!hkdfExpand(key, nonce, 32, keys, sizeof(keys))) return false;
  std::vector<unsigned char> aad(nonce, nonce + 32);
  aad.insert(aad.end(), cipher, cipher + cipherLen);
  unsigned char calculated[32];
  bool ok = hmacSha256(keys + 44, 32, aad.data(), aad.size(), calculated) && CRYPTO_memcmp(calculated, mac, 32) == 0;
  std::vector<unsigned char> padded(cipherLen);
  const WipeOnExit paddedGuard(padded);
  if (ok) ok = chacha20(keys, keys + 32, cipher, cipherLen, padded.data());
  OPENSSL_cleanse(keys, sizeof(keys));
  if (!ok || padded.size() < 2) return false;
  const unsigned first = (static_cast<unsigned>(padded[0]) << 8) | padded[1];
  std::size_t prefix = 2;
  std::uint64_t length = first;
  if (first == 0) {
    if (padded.size() < 6) return false;
    length = (static_cast<std::uint64_t>(padded[2]) << 24) | (static_cast<std::uint64_t>(padded[3]) << 16) |
             (static_cast<std::uint64_t>(padded[4]) << 8) | padded[5];
    if (length < 65536) return false;
    prefix = 6;
  }
  if (length == 0 || length > kMaxPlaintext) return false;
  if (padded.size() != prefix + paddedLength(length)) return false;
  plaintext.assign(reinterpret_cast<const char*>(padded.data() + prefix), static_cast<std::size_t>(length));
  if (!utf8Ok(plaintext)) {
    wipeString(plaintext);
    return false;
  }
  error.clear();
  return true;
}

bool eventCommitment(const Event& event, std::string& json, std::string& error) {
  if (!lowerHex(event.pubkey, 32) || event.createdAt < 0 || event.kind < 0 || event.kind > 65535) {
    error = "signer.crypto";
    return false;
  }
  json.clear();
  json.push_back('[');
  json += "0,";
  appendJsonString(json, event.pubkey);
  json.push_back(',');
  json += std::to_string(event.createdAt);
  json.push_back(',');
  json += std::to_string(event.kind);
  json.push_back(',');
  json.push_back('[');
  for (std::size_t index = 0; index < event.tags.size(); ++index) {
    if (index != 0) json.push_back(',');
    appendTag(json, event.tags[index]);
  }
  json += "],";
  appendJsonString(json, event.content);
  json.push_back(']');
  return true;
}

bool signEvent(Event& event, const unsigned char secret[32], std::int64_t createdAt, const unsigned char aux[32],
               std::string& error) {
  unsigned char pubkey[32];
  if (!publicKeyFromSecret(secret, pubkey, error)) return false;
  event.pubkey = hexEncode(pubkey, 32);
  event.createdAt = createdAt;
  std::string commitment;
  if (!eventCommitment(event, commitment, error)) return false;
  unsigned char id[32];
  SHA256(reinterpret_cast<const unsigned char*>(commitment.data()), commitment.size(), id);
  event.id = hexEncode(id, 32);
  unsigned char signature[64];
  if (!schnorrSign(secret, id, 32, aux, signature, error)) return false;
  event.sig = hexEncode(signature, 64);
  return true;
}

bool verifyEvent(const Event& event) {
  if (!lowerHex(event.id, 32) || !lowerHex(event.sig, 64)) return false;
  std::string commitment;
  std::string error;
  if (!eventCommitment(event, commitment, error)) return false;
  unsigned char id[32];
  SHA256(reinterpret_cast<const unsigned char*>(commitment.data()), commitment.size(), id);
  unsigned char claimed[32];
  unsigned char signature[64];
  unsigned char pubkey[32];
  if (!parseHex(event.id, claimed, 32) || !parseHex(event.sig, signature, 64) || !parseHex(event.pubkey, pubkey, 32)) {
    return false;
  }
  if (CRYPTO_memcmp(id, claimed, 32) != 0) return false;
  return schnorrVerify(pubkey, id, 32, signature);
}

namespace {

// A host name or an IPv4 address. Anything else, a CR or LF included, would
// reach the Host header and the TLS peer name.
bool hostNameOk(std::string_view host) {
  if (host.empty() || host.size() > 253 || host.front() == '.' || host.front() == '-') return false;
  for (const unsigned char c : host) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) continue;
    if (c == '.' || c == '-' || c == '_') continue;
    return false;
  }
  return true;
}

bool ipv6LiteralOk(std::string_view host) {
  if (host.size() < 2 || host.size() > 45) return false;
  for (const unsigned char c : host) {
    if (hexValue(static_cast<char>(c)) >= 0 || c == ':' || c == '.') continue;
    return false;
  }
  return true;
}

// RFC 3986 path and query bytes. A fragment, a space, or a control byte is
// refused, so the request line cannot be split.
bool pathOk(std::string_view path) {
  constexpr std::string_view kRefused = "\"<>\\^`{|}#";
  for (const unsigned char c : path) {
    if (c <= 0x20 || c >= 0x7f || kRefused.find(static_cast<char>(c)) != std::string_view::npos) return false;
  }
  return true;
}

bool authUrlOk(std::string_view url) {
  constexpr std::string_view kScheme = "https://";
  if (url.size() <= kScheme.size() || url.size() > 512 || url.substr(0, kScheme.size()) != kScheme) return false;
  for (const unsigned char c : url) {
    if (c <= 0x20 || c >= 0x7f) return false;
  }
  return true;
}

bool libraryIdOk(std::string_view id) {
  if (id.empty() || id.size() > 64) return false;
  for (const unsigned char c : id) {
    if (c >= 'a' && c <= 'z') continue;
    if (c >= '0' && c <= '9') continue;
    if (c == '_' || c == '-' || c == '.') continue;
    return false;
  }
  return true;
}

bool splitLibraryTag(std::string_view tag, std::string& kind, std::string& id) {
  constexpr std::string_view kMaterial = "timberlab:material:";
  constexpr std::string_view kPreset = "timberlab:preset:";
  std::string_view prefix;
  if (tag.size() > kMaterial.size() && tag.substr(0, kMaterial.size()) == kMaterial) {
    kind = "material";
    prefix = kMaterial;
  } else if (tag.size() > kPreset.size() && tag.substr(0, kPreset.size()) == kPreset) {
    kind = "preset";
    prefix = kPreset;
  } else {
    return false;
  }
  const std::string_view rest = tag.substr(prefix.size());
  if (!libraryIdOk(rest)) return false;
  id.assign(rest);
  return true;
}

const std::string* libraryTagOf(const Event& event) {
  const std::string* found = nullptr;
  for (const auto& tag : event.tags) {
    if (tag.size() < 2 || tag[0] != "d") continue;
    if (found != nullptr) return nullptr;
    found = &tag[1];
  }
  return found;
}

}  // namespace

bool libraryTagFor(std::string_view kind, std::string_view id, std::string& tag, std::string& error) {
  tag.clear();
  if ((kind != "material" && kind != "preset") || !libraryIdOk(id)) {
    error = "sync.mismatch";
    return false;
  }
  tag = "timberlab:";
  tag.append(kind);
  tag.push_back(':');
  tag.append(id);
  error.clear();
  return true;
}

bool sealLibraryEvent(const unsigned char secret[32], std::string_view tag, std::string_view plaintext,
                      std::int64_t createdAt, const unsigned char nonce[32], const unsigned char aux[32], Event& event,
                      std::string& error) {
  event = Event{};
  std::string kind;
  std::string id;
  if (createdAt < 0 || !splitLibraryTag(tag, kind, id)) {
    error = createdAt < 0 ? "signer.crypto" : "sync.mismatch";
    return false;
  }
  unsigned char pubkey[32];
  if (!publicKeyFromSecret(secret, pubkey, error)) return false;
  unsigned char shared[32];
  if (!conversationKey(secret, pubkey, shared, error)) return false;
  std::string content;
  const bool encrypted = nip44Encrypt(shared, plaintext, nonce, content, error);
  OPENSSL_cleanse(shared, sizeof(shared));
  if (!encrypted) return false;
  event.kind = 30078;
  event.tags = {{"d", std::string(tag)}};
  event.content = std::move(content);
  return signEvent(event, secret, createdAt, aux, error);
}

bool openLibraryEvent(const unsigned char secret[32], const Event& event, std::string& tag, std::string& plaintext,
                      std::string& error) {
  tag.clear();
  plaintext.clear();
  if (!verifyEvent(event) || event.kind != 30078) {
    error = "sync.mismatch";
    return false;
  }
  const std::string* dTag = libraryTagOf(event);
  std::string kind;
  std::string id;
  if (dTag == nullptr || !splitLibraryTag(*dTag, kind, id)) {
    error = "sync.mismatch";
    return false;
  }
  unsigned char pubkey[32];
  if (!publicKeyFromSecret(secret, pubkey, error)) return false;
  if (event.pubkey != hexEncode(pubkey, 32)) {
    error = "sync.mismatch";
    return false;
  }
  unsigned char shared[32];
  if (!conversationKey(secret, pubkey, shared, error)) return false;
  const bool opened = nip44Decrypt(shared, event.content, plaintext, error);
  OPENSSL_cleanse(shared, sizeof(shared));
  if (!opened) {
    wipeString(plaintext);
    error = "sync.crypto";
    return false;
  }
  tag = *dTag;
  error.clear();
  return true;
}

std::string eventToJson(const Event& event) { return eventJson(event).dump(); }

bool parseEventJson(std::string_view json, Event& event, std::string& error) {
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(json);
  } catch (const nlohmann::json::exception&) {
    error = "sync.mismatch";
    return false;
  }
  if (!eventFromJson(body, event)) {
    error = "sync.mismatch";
    return false;
  }
  error.clear();
  return true;
}

std::string libraryEventsToJson(const std::vector<Event>& events) {
  nlohmann::json array = nlohmann::json::array();
  for (const Event& event : events) array.push_back(eventJson(event));
  return array.dump();
}

bool parseLibraryEvents(std::string_view json, std::vector<Event>& events, std::string& error) {
  events.clear();
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(json);
  } catch (const nlohmann::json::exception&) {
    error = "sync.mismatch";
    return false;
  }
  if (!body.is_array()) {
    error = "sync.mismatch";
    return false;
  }
  for (const auto& item : body) {
    Event event;
    if (!eventFromJson(item, event)) {
      events.clear();
      error = "sync.mismatch";
      return false;
    }
    events.push_back(std::move(event));
  }
  error.clear();
  return true;
}

bool parseRelayUrl(std::string_view text, RelayLocation& out, std::string& error) {
  error = "signer.bunker";
  constexpr std::string_view kScheme = "wss://";
  if (text.size() > 2000 || text.size() <= kScheme.size() || text.substr(0, kScheme.size()) != kScheme) return false;
  if (text.find('@') != std::string_view::npos || text.find(' ') != std::string_view::npos) return false;
  const std::string_view rest = text.substr(kScheme.size());
  std::string host;
  std::string_view after;
  if (rest.front() == '[') {
    const auto end = rest.find(']');
    if (end == std::string_view::npos || end < 2) return false;
    host.assign(rest.substr(1, end - 1));
    after = rest.substr(end + 1);
    if (!ipv6LiteralOk(host)) return false;
  } else {
    const auto cut = rest.find_first_of(":/?");
    if (cut == std::string_view::npos) host.assign(rest);
    else {
      host.assign(rest.substr(0, cut));
      after = rest.substr(cut);
    }
    if (!hostNameOk(host)) return false;
  }
  int port = 443;
  if (!after.empty() && after.front() == ':') {
    std::size_t index = 1;
    if (index >= after.size() || std::isdigit(static_cast<unsigned char>(after[index])) == 0) return false;
    int value = 0;
    while (index < after.size() && std::isdigit(static_cast<unsigned char>(after[index])) != 0) {
      value = value * 10 + (after[index] - '0');
      if (value > 65535) return false;
      ++index;
    }
    if (value <= 0) return false;
    port = value;
    after.remove_prefix(index);
  }
  std::string path = "/";
  if (!after.empty()) {
    if (after.front() != '/' && after.front() != '?') return false;
    if (!pathOk(after)) return false;
    path.assign(after);
    if (path.front() == '?') path.insert(path.begin(), '/');
  }
  out.host = host;
  out.port = port;
  out.pathAndQuery = path;
  const std::string bracketed = host.find(':') == std::string::npos ? host : "[" + host + "]";
  out.hostHeader = port == 443 ? bracketed : bracketed + ":" + std::to_string(port);
  error.clear();
  return true;
}

namespace {

// One key=value pair of a bunker link. Every copy of the value is cleared,
// because the secret is one of them.
bool readBunkerPair(std::string_view pair, BunkerUri& out) {
  const auto eq = pair.find('=');
  if (eq == std::string_view::npos) return false;
  std::string key;
  std::string value;
  const bool decoded = percentDecode(pair.substr(0, eq), key) && percentDecode(pair.substr(eq + 1), value);
  RelayLocation location;
  std::string relayError;
  if (decoded && key == "relay" && out.relays.size() < 8 && parseRelayUrl(value, location, relayError) &&
      std::find(out.relays.begin(), out.relays.end(), value) == out.relays.end()) {
    out.relays.push_back(value);
  } else if (decoded && key == "secret") {
    wipeString(out.secret);
    out.secret = value;
  }
  wipeString(value);
  return decoded;
}

bool readBunkerText(std::string_view trimmed, BunkerUri& out) {
  constexpr std::string_view kPrefix = "bunker://";
  if (trimmed.size() > 5000 || trimmed.size() <= kPrefix.size() || trimmed.substr(0, kPrefix.size()) != kPrefix) {
    return false;
  }
  const auto queryAt = trimmed.find('?');
  if (queryAt == std::string_view::npos) return false;
  const std::string_view pubkeyHex = trimmed.substr(kPrefix.size(), queryAt - kPrefix.size());
  unsigned char pubkey[32];
  if (!parseHex(pubkeyHex, pubkey, 32) || !xOnlyOnCurve(pubkey)) return false;
  out.remotePubkeyHex = hexEncode(pubkey, 32);
  std::string_view query = trimmed.substr(queryAt + 1);
  while (!query.empty()) {
    const auto end = query.find('&');
    if (!readBunkerPair(query.substr(0, end), out)) return false;
    if (end == std::string_view::npos) break;
    query.remove_prefix(end + 1);
  }
  return !out.relays.empty();
}

}  // namespace

bool parseBunkerUri(std::string_view text, BunkerUri& out, std::string& error) {
  out = BunkerUri{};
  std::string trimmed = trimCopy(text);
  const bool ok = readBunkerText(trimmed, out);
  wipeString(trimmed);
  if (!ok) {
    wipeString(out.secret);
    out = BunkerUri{};
    error = "signer.bunker";
    return false;
  }
  error.clear();
  return true;
}

AmberRequest amberGetPublicKeyRequest() {
  AmberRequest request;
  request.packageName = "com.greenart7c3.nostrsigner";
  request.action = "android.intent.action.VIEW";
  request.dataUri = "nostrsigner:";
  request.typeExtra = "get_public_key";
  request.permissionsJson = "[{\"type\":\"get_public_key\"}]";
  request.contentAuthority = "com.greenart7c3.nostrsigner.GET_PUBLIC_KEY";
  return request;
}

std::string websocketAccept(std::string_view clientKey) {
  const std::string input = std::string(clientKey) + kWsGuid;
  unsigned char digest[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);
  return base64Encode(digest, sizeof(digest));
}

bool websocketOpening(std::string_view hostHeader, std::string_view pathAndQuery, WebsocketOpening& out,
                      std::string& error) {
  unsigned char raw[16];
  if (RAND_bytes(raw, sizeof(raw)) != 1) {
    error = "signer.crypto";
    return false;
  }
  const std::string key = base64Encode(raw, sizeof(raw));
  out.accept = websocketAccept(key);
  out.httpRequest = "GET " + std::string(pathAndQuery) + " HTTP/1.1\r\nHost: " + std::string(hostHeader) +
                    "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
                    "\r\nSec-WebSocket-Version: 13\r\n\r\n";
  return true;
}

HandshakeStatus readHandshake(std::string_view bytes, std::string_view expect, std::size_t& used, std::string& error) {
  const auto end = bytes.find("\r\n\r\n");
  if (end == std::string_view::npos) {
    if (bytes.size() > 8192) {
      error = "signer.relay";
      return HandshakeStatus::Failed;
    }
    return HandshakeStatus::NeedMore;
  }
  used = end + 4;
  const auto lineEnd = bytes.find("\r\n");
  const std::string_view status = bytes.substr(0, lineEnd);
  constexpr std::string_view kSwitching = "HTTP/1.1 101";
  if (status.substr(0, kSwitching.size()) != kSwitching ||
      (status.size() > kSwitching.size() && status[kSwitching.size()] != ' ')) {
    error = "signer.relay";
    return HandshakeStatus::Failed;
  }
  const std::string_view headers = bytes.substr(lineEnd + 2, end - (lineEnd + 2));
  std::string upgrade;
  std::string connection;
  std::string accept;
  if (!headerValue(headers, "upgrade", upgrade) || !headerValue(headers, "connection", connection) ||
      !headerValue(headers, "sec-websocket-accept", accept) || !containsToken(upgrade, "websocket") ||
      !containsToken(connection, "upgrade") || accept != expect) {
    error = "signer.relay";
    return HandshakeStatus::Failed;
  }
  return HandshakeStatus::Ok;
}

std::vector<unsigned char> websocketClientFrame(unsigned char opcode, std::string_view payload,
                                                const unsigned char mask[4]) {
  std::vector<unsigned char> frame;
  frame.push_back(static_cast<unsigned char>(0x80 | (opcode & 0x0f)));
  const auto length = payload.size();
  if (length < 126) {
    frame.push_back(static_cast<unsigned char>(0x80 | length));
  } else if (length <= 65535) {
    frame.push_back(0x80 | 126);
    frame.push_back(static_cast<unsigned char>((length >> 8) & 0xff));
    frame.push_back(static_cast<unsigned char>(length & 0xff));
  } else {
    frame.push_back(0x80 | 127);
    for (int shift = 7; shift >= 0; --shift) {
      frame.push_back(static_cast<unsigned char>((static_cast<std::uint64_t>(length) >> (shift * 8)) & 0xff));
    }
  }
  frame.insert(frame.end(), mask, mask + 4);
  for (std::size_t index = 0; index < payload.size(); ++index) {
    frame.push_back(static_cast<unsigned char>(payload[index] ^ mask[index % 4]));
  }
  return frame;
}

FrameStatus websocketReadFrame(const unsigned char* data, std::size_t size, std::size_t& used, WebsocketFrame& frame,
                               std::string& error) {
  used = 0;
  if (size < 2) return FrameStatus::NeedMore;
  const unsigned char first = data[0];
  const unsigned char second = data[1];
  if ((first & 0x70) != 0 || (second & 0x80) != 0) {
    error = "signer.relay";
    return FrameStatus::Failed;
  }
  std::uint64_t length = second & 0x7f;
  std::size_t header = 2;
  if (length == 126) {
    if (size < 4) return FrameStatus::NeedMore;
    length = (static_cast<std::uint64_t>(data[2]) << 8) | data[3];
    header = 4;
    if (length < 126) {
      error = "signer.relay";
      return FrameStatus::Failed;
    }
  } else if (length == 127) {
    if (size < 10) return FrameStatus::NeedMore;
    if ((data[2] & 0x80) != 0) {
      error = "signer.relay";
      return FrameStatus::Failed;
    }
    length = 0;
    for (int index = 0; index < 8; ++index) length = (length << 8) | data[2 + index];
    header = 10;
    if (length <= 65535) {
      error = "signer.relay";
      return FrameStatus::Failed;
    }
  }
  const unsigned char opcode = first & 0x0f;
  const bool fin = (first & 0x80) != 0;
  if (opcode >= 8 && (!fin || length > 125)) {
    error = "signer.relay";
    return FrameStatus::Failed;
  }
  if (length > kMaxFrame) {
    error = "signer.relay";
    return FrameStatus::Failed;
  }
  if (size < header + length) return FrameStatus::NeedMore;
  frame.fin = fin;
  frame.opcode = opcode;
  frame.payload.assign(reinterpret_cast<const char*>(data + header), static_cast<std::size_t>(length));
  used = header + static_cast<std::size_t>(length);
  return FrameStatus::Ok;
}

void WebsocketReader::reset() {
  buffer_.clear();
  fragment_.clear();
  assembling_ = false;
}

bool WebsocketReader::accept(WebsocketFrame& frame, std::vector<WsIncoming>& out, std::string& error) {
  if (frame.opcode == 0x9 || frame.opcode == 0xA || frame.opcode == 0x8) {
    const WsKind kind = frame.opcode == 0x9 ? WsKind::Ping : frame.opcode == 0x8 ? WsKind::Close : WsKind::Pong;
    out.push_back(WsIncoming{kind, std::move(frame.payload)});
    return true;
  }
  // Binary frames and reserved opcodes are refused. A continuation needs an
  // open text message, and a new text frame needs none.
  const bool continuation = frame.opcode == 0x0;
  if ((frame.opcode != 0x0 && frame.opcode != 0x1) || continuation != assembling_) {
    error = "signer.relay";
    return false;
  }
  if (!continuation && frame.fin) {
    out.push_back(WsIncoming{WsKind::Text, std::move(frame.payload)});
    return true;
  }
  if (fragment_.size() + frame.payload.size() > kMaxFrame) {
    error = "signer.relay";
    return false;
  }
  fragment_ += frame.payload;
  assembling_ = !frame.fin;
  if (frame.fin) {
    out.push_back(WsIncoming{WsKind::Text, std::move(fragment_)});
    fragment_.clear();
  }
  return true;
}

bool WebsocketReader::consume(const unsigned char* data, std::size_t size, std::vector<WsIncoming>& out,
                              std::string& error) {
  if (size > 0) buffer_.insert(buffer_.end(), data, data + size);
  // Frames are read at an offset and the buffer is trimmed once, so a burst of
  // small frames costs one pass, not one erase per frame.
  std::size_t offset = 0;
  bool ok = true;
  while (ok && offset < buffer_.size()) {
    std::size_t used = 0;
    WebsocketFrame frame;
    const FrameStatus status = websocketReadFrame(buffer_.data() + offset, buffer_.size() - offset, used, frame, error);
    if (status == FrameStatus::NeedMore) break;
    ok = status == FrameStatus::Ok && accept(frame, out, error);
    offset += used;
  }
  if (!ok) {
    reset();
    return false;
  }
  buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset));
  return true;
}

bool randomHex(std::size_t bytes, std::string& hex, std::string& error) {
  if (bytes == 0 || bytes > 64) {
    error = "signer.crypto";
    return false;
  }
  unsigned char buffer[64];
  if (RAND_bytes(buffer, static_cast<int>(bytes)) != 1) {
    error = "signer.crypto";
    return false;
  }
  hex = hexEncode(buffer, bytes);
  OPENSSL_cleanse(buffer, sizeof(buffer));
  return true;
}

std::string relayEventMessage(const Event& event) {
  const nlohmann::json message = nlohmann::json::array({"EVENT", eventJson(event)});
  return message.dump();
}

std::string relayReqMessage(std::string_view sub, std::string_view clientPubkeyHex, std::int64_t since) {
  if (since < 0) since = 0;
  const nlohmann::json filter = {{"kinds", nlohmann::json::array({24133})},
                                 {"#p", nlohmann::json::array({std::string(clientPubkeyHex)})},
                                 {"since", since}};
  const nlohmann::json message = nlohmann::json::array({"REQ", std::string(sub), filter});
  return message.dump();
}

std::string relayCloseMessage(std::string_view sub) {
  return nlohmann::json::array({"CLOSE", std::string(sub)}).dump();
}

std::string relayAuthMessage(const Event& event) {
  return nlohmann::json::array({"AUTH", eventJson(event)}).dump();
}

bool parseRelayMessage(std::string_view text, RelayMessage& out, std::string& error) {
  const nlohmann::json message = nlohmann::json::parse(text, nullptr, false);
  if (message.is_discarded() || !message.is_array() || message.empty() || !message[0].is_string()) {
    error = "signer.relay";
    return false;
  }
  out = RelayMessage{};
  out.type = message[0].get<std::string>();
  try {
    if (out.type == "EVENT" && message.size() == 3 && message[1].is_string() && eventFromJson(message[2], out.event)) {
      out.sub = message[1].get<std::string>();
      return true;
    }
    if (out.type == "OK" && message.size() == 4 && message[1].is_string() && message[2].is_boolean() &&
        message[3].is_string()) {
      out.eventId = message[1].get<std::string>();
      out.accepted = message[2].get<bool>();
      out.text = message[3].get<std::string>();
      return true;
    }
    if (out.type == "EOSE" && message.size() == 2 && message[1].is_string()) {
      out.sub = message[1].get<std::string>();
      return true;
    }
    if (out.type == "NOTICE" && message.size() == 2 && message[1].is_string()) {
      out.text = message[1].get<std::string>();
      return true;
    }
    if (out.type == "CLOSED" && message.size() == 3 && message[1].is_string() && message[2].is_string()) {
      out.sub = message[1].get<std::string>();
      out.text = message[2].get<std::string>();
      return true;
    }
    if (out.type == "AUTH" && message.size() == 2 && message[1].is_string()) {
      out.text = message[1].get<std::string>();
      return true;
    }
  } catch (const std::exception&) {
    error = "signer.relay";
    return false;
  }
  out.type = "IGNORE";
  return true;
}

SignerSession::SignerSession() = default;

SignerSession::~SignerSession() { wipe(); }

void SignerSession::wipe() {
  OPENSSL_cleanse(secret_, sizeof(secret_));
  hasSecret_ = false;
  OPENSSL_cleanse(userPub_, sizeof(userPub_));
  hasUser_ = false;
  OPENSSL_cleanse(remotePub_, sizeof(remotePub_));
  wipeString(npub_);
  wipeString(pubkeyHex_);
  wipeString(clientHex_);
  wipeString(remoteHex_);
  wipeString(bunkerSecret_);
  wipeString(requestId_);
  detail_.clear();
  wipeString(authUrl_);
  relays_.clear();
  pending_ = Event{};
  method_ = SignerMethod::None;
  state_ = SignerState::SignedOut;
}

bool SignerSession::loginWithNsec(std::string_view secret, std::string& error) {
  wipe();
  std::string text = trimCopy(secret);
  unsigned char key[32];
  bool parsed = false;
  if (text.size() == 64 && parseHex(text, key, 32)) parsed = true;
  else parsed = decodeNsec(text, key, error);
  wipeString(text);
  if (!parsed) {
    OPENSSL_cleanse(key, sizeof(key));
    error = "signer.nsec";
    return false;
  }
  if (!publicKeyFromSecret(key, userPub_, error)) {
    OPENSSL_cleanse(key, sizeof(key));
    wipe();
    error = "signer.nsec";
    return false;
  }
  std::memcpy(secret_, key, 32);
  OPENSSL_cleanse(key, sizeof(key));
  hasSecret_ = true;
  hasUser_ = true;
  pubkeyHex_ = hexEncode(userPub_, 32);
  if (!encodeNpub(userPub_, npub_, error)) {
    wipe();
    return false;
  }
  method_ = SignerMethod::Nsec;
  state_ = SignerState::SignedIn;
  return true;
}

bool SignerSession::loginWithPublicKey(std::string_view pubkeyHex, SignerMethod method, std::string& error) {
  wipe();
  if (method != SignerMethod::Amber) {
    error = "signer.state";
    return false;
  }
  // NIP-55 signers have returned the key both as hex and as an npub.
  const std::string text = trimCopy(pubkeyHex);
  unsigned char pubkey[32];
  std::string decodeError;
  const bool parsed = text.rfind("npub1", 0) == 0 ? decodeNpub(text, pubkey, decodeError) : parseHex(text, pubkey, 32);
  if (!parsed || !xOnlyOnCurve(pubkey)) {
    error = "signer.rejected";
    return false;
  }
  std::memcpy(userPub_, pubkey, 32);
  hasUser_ = true;
  pubkeyHex_ = hexEncode(userPub_, 32);
  if (!encodeNpub(userPub_, npub_, error)) {
    wipe();
    return false;
  }
  method_ = SignerMethod::Amber;
  state_ = SignerState::SignedIn;
  return true;
}

bool SignerSession::encryptToRemote(const std::string& plaintext, std::string& payload, std::string& error) {
  unsigned char key[32];
  unsigned char nonce[32];
  if (!conversationKey(secret_, remotePub_, key, error)) return false;
  if (RAND_bytes(nonce, sizeof(nonce)) != 1) {
    OPENSSL_cleanse(key, sizeof(key));
    error = "signer.crypto";
    return false;
  }
  const bool ok = nip44Encrypt(key, plaintext, nonce, payload, error);
  OPENSSL_cleanse(key, sizeof(key));
  OPENSSL_cleanse(nonce, sizeof(nonce));
  return ok;
}

bool SignerSession::nextRequest(const std::string& method, const std::vector<std::string>& params, std::int64_t now,
                                std::string& error) {
  std::string id;
  if (!randomHex(16, id, error)) return false;
  const nlohmann::json body = {{"id", id}, {"method", method}, {"params", params}};
  std::string plain = body.dump();
  std::string payload;
  if (!encryptToRemote(plain, payload, error)) {
    wipeString(plain);
    return false;
  }
  wipeString(plain);
  Event event;
  event.kind = 24133;
  event.tags = {{"p", remoteHex_}};
  event.content = std::move(payload);
  unsigned char aux[32];
  if (RAND_bytes(aux, sizeof(aux)) != 1) {
    error = "signer.crypto";
    return false;
  }
  const bool ok = signEvent(event, secret_, now, aux, error);
  OPENSSL_cleanse(aux, sizeof(aux));
  if (!ok) return false;
  pending_ = std::move(event);
  requestId_ = id;
  return true;
}

bool SignerSession::beginBunker(std::string_view uri, std::int64_t now, std::string& error) {
  wipe();
  BunkerUri parsed;
  if (!parseBunkerUri(uri, parsed, error)) return false;
  if (!parseHex(parsed.remotePubkeyHex, remotePub_, 32)) {
    wipeString(parsed.secret);
    wipe();
    error = "signer.bunker";
    return false;
  }
  remoteHex_ = parsed.remotePubkeyHex;
  relays_ = parsed.relays;
  bunkerSecret_ = parsed.secret;
  wipeString(parsed.secret);
  std::string keyError;
  bool generated = false;
  for (int attempt = 0; attempt < 8 && !generated; ++attempt) {
    if (RAND_bytes(secret_, sizeof(secret_)) != 1) break;
    unsigned char clientPub[32];
    generated = publicKeyFromSecret(secret_, clientPub, keyError);
    if (!generated) continue;
    clientHex_ = hexEncode(clientPub, 32);
    hasSecret_ = true;
  }
  if (!generated) {
    wipe();
    error = "signer.crypto";
    return false;
  }
  method_ = SignerMethod::Bunker;
  state_ = SignerState::WaitConnect;
  std::vector<std::string> params = {remoteHex_, bunkerSecret_, "get_public_key", "{\"name\":\"TimberLab\"}"};
  // The secret stays until the connect reply, because NIP-46 lets the bunker
  // answer with it instead of "ack".
  const bool ok = nextRequest("connect", params, now, error);
  for (std::string& param : params) wipeString(param);
  if (!ok) {
    wipe();
    return false;
  }
  return true;
}

bool SignerSession::pendingEvent(Event& event) const {
  if (pending_.id.empty()) return false;
  event = pending_;
  return true;
}

RemoteResult SignerSession::acceptRemoteEvent(const Event& event, std::int64_t now, std::string& error) {
  if (state_ != SignerState::WaitConnect && state_ != SignerState::WaitPublicKey) return RemoteResult::Ignored;
  if (event.kind != 24133 || event.pubkey != remoteHex_) return RemoteResult::Ignored;
  bool tagged = false;
  for (const auto& tag : event.tags) {
    if (tag.size() >= 2 && tag[0] == "p" && tag[1] == clientHex_) tagged = true;
  }
  if (!tagged || !verifyEvent(event)) return RemoteResult::Ignored;
  unsigned char key[32];
  std::string plain;
  if (!conversationKey(secret_, remotePub_, key, error)) {
    wipe();
    return RemoteResult::Failed;
  }
  const bool opened = nip44Decrypt(key, event.content, plain, error);
  OPENSSL_cleanse(key, sizeof(key));
  if (!opened) {
    wipeString(plain);
    return RemoteResult::Ignored;
  }
  const nlohmann::json body = nlohmann::json::parse(plain, nullptr, false);
  wipeString(plain);
  if (body.is_discarded() || !body.is_object() || !body.contains("id") || !body["id"].is_string()) {
    return RemoteResult::Ignored;
  }
  if (body["id"].get<std::string>() != requestId_) return RemoteResult::Ignored;
  const bool hasError = body.contains("error") && body["error"].is_string() && !body["error"].get<std::string>().empty();
  const std::string result = body.contains("result") && body["result"].is_string() ? body["result"].get<std::string>() : "";
  if (hasError && result == "auth_url") {
    // The page is shown, never opened, and only as a plain https address. A
    // cut address would point somewhere else, so a long one is refused.
    const std::string url = body["error"].get<std::string>();
    if (!authUrlOk(url)) {
      wipe();
      error = "signer.rejected";
      return RemoteResult::Failed;
    }
    detail_ = url;
    authUrl_ = url;
    error = "signer.auth";
    return RemoteResult::AuthRequired;
  }
  if (hasError || result.empty()) {
    std::string why;
    if (hasError) why = body["error"].get<std::string>();
    if (why.size() > 180) why.resize(180);
    wipe();
    detail_ = std::move(why);
    error = "signer.rejected";
    return RemoteResult::Failed;
  }
  if (state_ == SignerState::WaitConnect) {
    const bool echoed = !bunkerSecret_.empty() && result.size() == bunkerSecret_.size() &&
                        CRYPTO_memcmp(result.data(), bunkerSecret_.data(), result.size()) == 0;
    if (result != "ack" && !echoed) {
      wipe();
      error = "signer.rejected";
      return RemoteResult::Failed;
    }
    wipeString(bunkerSecret_);
    if (!nextRequest("get_public_key", {}, now, error)) {
      wipe();
      return RemoteResult::Failed;
    }
    state_ = SignerState::WaitPublicKey;
    return RemoteResult::Continue;
  }
  unsigned char user[32];
  if (!parseHex(result, user, 32) || !xOnlyOnCurve(user)) {
    wipe();
    error = "signer.rejected";
    return RemoteResult::Failed;
  }
  std::memcpy(userPub_, user, 32);
  hasUser_ = true;
  pubkeyHex_ = hexEncode(userPub_, 32);
  if (!encodeNpub(userPub_, npub_, error)) {
    wipe();
    return RemoteResult::Failed;
  }
  state_ = SignerState::SignedIn;
  pending_ = Event{};
  return RemoteResult::SignedIn;
}

bool SignerSession::signWith(Event& event, std::int64_t createdAt, std::string& error) {
  unsigned char aux[32];
  if (RAND_bytes(aux, sizeof(aux)) != 1) {
    error = "signer.crypto";
    return false;
  }
  const bool ok = signEvent(event, secret_, createdAt, aux, error);
  OPENSSL_cleanse(aux, sizeof(aux));
  return ok;
}

bool SignerSession::signAuth(std::string_view relayUrl, std::string_view challenge, std::int64_t now, Event& event,
                             std::string& error) {
  if (method_ != SignerMethod::Bunker || !hasSecret_ || relayUrl.empty() || challenge.empty() || challenge.size() > 256) {
    error = "signer.state";
    return false;
  }
  event = Event{};
  event.kind = 22242;
  event.content.clear();
  event.tags = {{"relay", std::string(relayUrl)}, {"challenge", std::string(challenge)}};
  return signWith(event, now, error);
}

bool SignerSession::signLocal(Event& event, std::int64_t now, std::string& error) {
  if (method_ != SignerMethod::Nsec || !hasSecret_ || state_ != SignerState::SignedIn) {
    error = "signer.state";
    return false;
  }
  return signWith(event, now, error);
}

bool SignerSession::sealLibrary(std::string_view tag, std::string_view plaintext, std::int64_t now, Event& event,
                                std::string& error) {
  if (method_ != SignerMethod::Nsec || !hasSecret_ || state_ != SignerState::SignedIn) {
    error = "signer.state";
    return false;
  }
  unsigned char nonce[32];
  unsigned char aux[32];
  if (RAND_bytes(nonce, sizeof(nonce)) != 1 || RAND_bytes(aux, sizeof(aux)) != 1) {
    OPENSSL_cleanse(nonce, sizeof(nonce));
    OPENSSL_cleanse(aux, sizeof(aux));
    error = "signer.crypto";
    return false;
  }
  const bool ok = sealLibraryEvent(secret_, tag, plaintext, now, nonce, aux, event, error);
  OPENSSL_cleanse(nonce, sizeof(nonce));
  OPENSSL_cleanse(aux, sizeof(aux));
  return ok;
}

bool SignerSession::openLibrary(const Event& event, std::string& tag, std::string& plaintext, std::string& error) {
  if (method_ != SignerMethod::Nsec || !hasSecret_ || state_ != SignerState::SignedIn) {
    error = "signer.state";
    return false;
  }
  return openLibraryEvent(secret_, event, tag, plaintext, error);
}

void SignerSession::logout() { wipe(); }

}  // namespace timberlab::storage
