#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace timberlab::storage {

// Nostr sign-in and a library event. Secret bytes stay in memory. Nothing here
// opens a project file, writes a key, or opens a socket. A library event is
// kind 30078, an application schema, not a NIP. Its d tag is
// timberlab:material:<id> or timberlab:preset:<id>. The content is NIP-44
// ciphertext for the author's own key. The plaintext is the library envelope.

struct Event {
  std::string id;
  std::string pubkey;
  std::int64_t createdAt = 0;
  int kind = 0;
  std::vector<std::vector<std::string>> tags;
  std::string content;
  std::string sig;
};

struct BunkerUri {
  std::string remotePubkeyHex;
  std::vector<std::string> relays;
  std::string secret;
};

struct AmberRequest {
  std::string packageName;
  std::string action;
  std::string dataUri;
  std::string typeExtra;
  std::string permissionsJson;
  std::string contentAuthority;
};

struct RelayLocation {
  std::string host;
  std::string hostHeader;
  int port = 443;
  std::string pathAndQuery;
};

enum class SignerMethod { None, Nsec, Bunker, Amber };
enum class SignerState { SignedOut, WaitConnect, WaitPublicKey, SignedIn };
enum class RemoteResult { Ignored, AuthRequired, Continue, SignedIn, Failed };
enum class FrameStatus { NeedMore, Ok, Failed };
enum class HandshakeStatus { NeedMore, Ok, Failed };

struct WebsocketFrame {
  bool fin = true;
  unsigned char opcode = 0;
  std::string payload;
};

struct WebsocketOpening {
  std::string httpRequest;
  std::string accept;
};

enum class WsKind { Text, Ping, Pong, Close };

struct WsIncoming {
  WsKind kind = WsKind::Text;
  std::string data;
};

struct RelayMessage {
  std::string type;
  std::string sub;
  std::string eventId;
  bool accepted = false;
  std::string text;
  Event event;
};

bool parseHex(std::string_view hex, unsigned char* out, std::size_t outLen);
std::string hexEncode(const unsigned char* data, std::size_t size);

bool decodeNsec(std::string_view text, unsigned char secret[32], std::string& error);
bool encodeNsec(const unsigned char secret[32], std::string& text, std::string& error);
bool decodeNpub(std::string_view text, unsigned char pubkey[32], std::string& error);
bool encodeNpub(const unsigned char pubkey[32], std::string& text, std::string& error);

bool publicKeyFromSecret(const unsigned char secret[32], unsigned char pubkey[32], std::string& error);
bool xOnlyOnCurve(const unsigned char pubkey[32]);
bool schnorrSign(const unsigned char secret[32], const unsigned char* message, std::size_t messageLen,
                 const unsigned char aux[32], unsigned char signature[64], std::string& error);
bool schnorrVerify(const unsigned char pubkey[32], const unsigned char* message, std::size_t messageLen,
                   const unsigned char signature[64]);

bool conversationKey(const unsigned char secret[32], const unsigned char pubkey[32], unsigned char out[32],
                     std::string& error);
bool nip44Encrypt(const unsigned char key[32], std::string_view plaintext, const unsigned char nonce[32],
                  std::string& payload, std::string& error);
bool nip44Decrypt(const unsigned char key[32], std::string_view payload, std::string& plaintext, std::string& error);

bool eventCommitment(const Event& event, std::string& json, std::string& error);
bool signEvent(Event& event, const unsigned char secret[32], std::int64_t createdAt, const unsigned char aux[32],
               std::string& error);
bool verifyEvent(const Event& event);

bool parseBunkerUri(std::string_view text, BunkerUri& out, std::string& error);
bool parseRelayUrl(std::string_view text, RelayLocation& out, std::string& error);
AmberRequest amberGetPublicKeyRequest();

std::string websocketAccept(std::string_view clientKey);
bool websocketOpening(std::string_view hostHeader, std::string_view pathAndQuery, WebsocketOpening& out,
                      std::string& error);
HandshakeStatus readHandshake(std::string_view bytes, std::string_view expect, std::size_t& used, std::string& error);
std::vector<unsigned char> websocketClientFrame(unsigned char opcode, std::string_view payload,
                                                const unsigned char mask[4]);
FrameStatus websocketReadFrame(const unsigned char* data, std::size_t size, std::size_t& used, WebsocketFrame& frame,
                               std::string& error);

class WebsocketReader {
 public:
  void reset();
  bool consume(const unsigned char* data, std::size_t size, std::vector<WsIncoming>& out, std::string& error);

 private:
  bool accept(WebsocketFrame& frame, std::vector<WsIncoming>& out, std::string& error);

  std::vector<unsigned char> buffer_;
  std::string fragment_;
  bool assembling_ = false;
};

bool randomHex(std::size_t bytes, std::string& hex, std::string& error);
std::string relayEventMessage(const Event& event);
std::string relayReqMessage(std::string_view sub, std::string_view clientPubkeyHex, std::int64_t since);
std::string relayCloseMessage(std::string_view sub);
std::string relayAuthMessage(const Event& event);
bool parseRelayMessage(std::string_view json, RelayMessage& out, std::string& error);

// The id uses the library charset: lowercase letters, digits, underscore,
// hyphen, and dot, 1 to 64 bytes. A bad kind or id is sync.mismatch.
bool libraryTagFor(std::string_view kind, std::string_view id, std::string& tag, std::string& error);
bool sealLibraryEvent(const unsigned char secret[32], std::string_view tag, std::string_view plaintext,
                      std::int64_t createdAt, const unsigned char nonce[32], const unsigned char aux[32], Event& event,
                      std::string& error);
bool openLibraryEvent(const unsigned char secret[32], const Event& event, std::string& tag, std::string& plaintext,
                      std::string& error);
std::string eventToJson(const Event& event);
bool parseEventJson(std::string_view json, Event& event, std::string& error);
std::string libraryEventsToJson(const std::vector<Event>& events);
bool parseLibraryEvents(std::string_view json, std::vector<Event>& events, std::string& error);

class SignerSession {
 public:
  SignerSession();
  ~SignerSession();
  SignerSession(const SignerSession&) = delete;
  SignerSession& operator=(const SignerSession&) = delete;

  bool loginWithNsec(std::string_view secret, std::string& error);
  bool loginWithPublicKey(std::string_view pubkeyHex, SignerMethod method, std::string& error);
  bool beginBunker(std::string_view uri, std::int64_t now, std::string& error);
  bool pendingEvent(Event& event) const;
  RemoteResult acceptRemoteEvent(const Event& event, std::int64_t now, std::string& error);
  bool signAuth(std::string_view relayUrl, std::string_view challenge, std::int64_t now, Event& event,
                std::string& error);
  bool signLocal(Event& event, std::int64_t now, std::string& error);
  // Nsec only. Bunker and Amber do not hold the user secret, so they cannot
  // seal or open a library event.
  bool sealLibrary(std::string_view tag, std::string_view plaintext, std::int64_t now, Event& event, std::string& error);
  bool openLibrary(const Event& event, std::string& tag, std::string& plaintext, std::string& error);
  void logout();

  bool signedIn() const { return state_ == SignerState::SignedIn; }
  SignerMethod method() const { return method_; }
  SignerState state() const { return state_; }
  const std::string& npub() const { return npub_; }
  const std::string& pubkeyHex() const { return pubkeyHex_; }
  const std::string& detail() const { return detail_; }
  const std::vector<std::string>& relays() const { return relays_; }

 private:
  void wipe();
  bool signWith(Event& event, std::int64_t createdAt, std::string& error);
  bool encryptToRemote(const std::string& plaintext, std::string& payload, std::string& error);
  bool nextRequest(const std::string& method, const std::vector<std::string>& params, std::int64_t now,
                   std::string& error);

  SignerMethod method_ = SignerMethod::None;
  SignerState state_ = SignerState::SignedOut;
  unsigned char secret_[32] = {};
  bool hasSecret_ = false;
  unsigned char userPub_[32] = {};
  bool hasUser_ = false;
  unsigned char remotePub_[32] = {};
  std::string npub_;
  std::string pubkeyHex_;
  std::string clientHex_;
  std::string remoteHex_;
  std::string bunkerSecret_;
  std::string requestId_;
  std::string detail_;
  std::string authUrl_;
  std::vector<std::string> relays_;
  Event pending_;
};

}  // namespace timberlab::storage
