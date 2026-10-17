#include "timberlab/core/document.hpp"
#include "timberlab/storage/nostr.hpp"
#include "timberlab/storage/sha256.hpp"

#include <nlohmann/json.hpp>
#include <openssl/crypto.h>

#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

using timberlab::storage::AmberRequest;
using timberlab::storage::BunkerUri;
using timberlab::storage::Event;
using timberlab::storage::FrameStatus;
using timberlab::storage::HandshakeStatus;
using timberlab::storage::RelayMessage;
using timberlab::storage::RemoteResult;
using timberlab::storage::SignerMethod;
using timberlab::storage::SignerSession;
using timberlab::storage::SignerState;
using timberlab::storage::WebsocketFrame;
using timberlab::storage::WebsocketReader;
using timberlab::storage::WsKind;
using timberlab::storage::amberGetPublicKeyRequest;
using timberlab::storage::conversationKey;
using timberlab::storage::decodeNpub;
using timberlab::storage::decodeNsec;
using timberlab::storage::encodeNpub;
using timberlab::storage::encodeNsec;
using timberlab::storage::eventCommitment;
using timberlab::storage::hexEncode;
using timberlab::storage::nip44Decrypt;
using timberlab::storage::nip44Encrypt;
using timberlab::storage::parseBunkerUri;
using timberlab::storage::parseHex;
using timberlab::storage::parseRelayMessage;
using timberlab::storage::publicKeyFromSecret;
using timberlab::storage::readHandshake;
using timberlab::storage::relayEventMessage;
using timberlab::storage::relayReqMessage;
using timberlab::storage::schnorrSign;
using timberlab::storage::schnorrVerify;
using timberlab::storage::sha256Hex;
using timberlab::storage::signEvent;
using timberlab::storage::verifyEvent;
using timberlab::storage::websocketAccept;
using timberlab::storage::xOnlyOnCurve;
using timberlab::storage::websocketClientFrame;
using timberlab::storage::websocketReadFrame;

namespace {

void fill32(unsigned char out[32], const char* hex) { REQUIRE(parseHex(hex, out, 32)); }

void fillBytes(std::vector<unsigned char>& out, const char* hex) {
  const std::size_t size = std::strlen(hex) / 2;
  out.assign(size, 0);
  if (size == 0) return;
  REQUIRE(parseHex(hex, out.data(), size));
}

Event bunkerReply(const unsigned char bunkerSecret[32], const Event& request, const std::string& result,
                  const std::string& remoteError, std::int64_t now) {
  unsigned char client[32];
  REQUIRE(parseHex(request.pubkey, client, 32));
  unsigned char key[32];
  std::string error;
  REQUIRE(conversationKey(bunkerSecret, client, key, error));
  std::string plain;
  REQUIRE(nip44Decrypt(key, request.content, plain, error));
  const auto body = nlohmann::json::parse(plain);
  nlohmann::json response = {{"id", body.at("id")}, {"result", result}};
  if (!remoteError.empty()) response["error"] = remoteError;
  unsigned char nonce[32] = {};
  nonce[31] = 9;
  std::string payload;
  REQUIRE(nip44Encrypt(key, response.dump(), nonce, payload, error));
  OPENSSL_cleanse(key, sizeof(key));
  Event event;
  event.kind = 24133;
  event.content = payload;
  event.tags = {{"p", request.pubkey}};
  unsigned char aux[32] = {};
  REQUIRE(signEvent(event, bunkerSecret, now, aux, error));
  return event;
}

std::vector<unsigned char> serverFrame(unsigned char opcode, bool fin, std::string_view payload) {
  std::vector<unsigned char> frame;
  frame.push_back(static_cast<unsigned char>((fin ? 0x80 : 0x00) | opcode));
  frame.push_back(static_cast<unsigned char>(payload.size()));
  frame.insert(frame.end(), payload.begin(), payload.end());
  return frame;
}

}  // namespace

TEST_CASE("NIP-19 bech32 matches the published nsec and npub") {
  unsigned char pubkey[32];
  unsigned char secret[32];
  std::string error;
  REQUIRE(parseHex("3bf0c63fcb93463407af97a5e5ee64fa883d107ef9e558472c4eb9aaaefa459d", pubkey, 32));
  std::string npub;
  REQUIRE(encodeNpub(pubkey, npub, error));
  REQUIRE(npub == "npub180cvv07tjdrrgpa0j7j7tmnyl2yr6yr7l8j4s3evf6u64th6gkwsyjh6w6");
  unsigned char decoded[32];
  REQUIRE(decodeNpub(npub, decoded, error));
  REQUIRE(hexEncode(decoded, 32) == "3bf0c63fcb93463407af97a5e5ee64fa883d107ef9e558472c4eb9aaaefa459d");

  REQUIRE(decodeNsec("nsec1vl029mgpspedva04g90vltkh6fvh240zqtv9k0t9af8935ke9laqsnlfe5", secret, error));
  REQUIRE(hexEncode(secret, 32) == "67dea2ed018072d675f5415ecfaed7d2597555e202d85b3d65ea4e58d2d92ffa");
  std::string encoded;
  REQUIRE(encodeNsec(secret, encoded, error));
  REQUIRE(encoded == "nsec1vl029mgpspedva04g90vltkh6fvh240zqtv9k0t9af8935ke9laqsnlfe5");
  REQUIRE_FALSE(decodeNsec("nsec1ql029mgpspedva04g90vltkh6fvh240zqtv9k0t9af8935ke9laqsnlfe5", secret, error));
  REQUIRE_FALSE(decodeNsec("NSEC1vl029mgpspedva04g90vltkh6fvh240zqtv9k0t9af8935ke9laqsnlfe5", secret, error));
  OPENSSL_cleanse(secret, sizeof(secret));
}

TEST_CASE("BIP340 signatures match the published vectors") {
  struct Row {
    const char* secret;
    const char* pubkey;
    const char* aux;
    const char* message;
    const char* signature;
    bool ok;
  };
  const Row rows[] = {
      {"0000000000000000000000000000000000000000000000000000000000000003",
       "F9308A019258C31049344F85F89D5229B531C845836F99B08601F113BCE036F9",
       "0000000000000000000000000000000000000000000000000000000000000000",
       "0000000000000000000000000000000000000000000000000000000000000000",
       "E907831F80848D1069A5371B402410364BDF1C5F8307B0084C55F1CE2DCA821525F66A4A85EA8B71E482A74F382D2CE5EBEEE8FDB2172F477DF4900D310536C0",
       true},
      {"B7E151628AED2A6ABF7158809CF4F3C762E7160F38B4DA56A784D9045190CFEF",
       "DFF1D77F2A671C5F36183726DB2341BE58FEAE1DA2DECED843240F7B502BA659",
       "0000000000000000000000000000000000000000000000000000000000000001",
       "243F6A8885A308D313198A2E03707344A4093822299F31D0082EFA98EC4E6C89",
       "6896BD60EEAE296DB48A229FF71DFE071BDE413E6D43F917DC8DCF8C78DE33418906D11AC976ABCCB20B091292BFF4EA897EFCB639EA871CFA95F6DE339E4B0A",
       true},
      {"C90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B14E5C9",
       "DD308AFEC5777E13121FA72B9CC1B7CC0139715309B086C960E18FD969774EB8",
       "C87AA53824B4D7AE2EB035A2B5BBBCCC080E76CDC6D1692C4B0B62D798E6D906",
       "7E2D58D8B3BCDF1ABADEC7829054F90DDA9805AAB56C77333024B9D0A508B75C",
       "5831AAEED7B44BB74E5EAB94BA9D4294C49BCF2A60728D8B4C200F50DD313C1BAB745879A5AD954A72C45A91C3A51D3C7ADEA98D82F8481E0E1E03674A6F3FB7",
       true},
      {"", "D69C3509BB99E412E68B0FE8544E72837DFA30746D8BE2AA65975F29D22DC7B9", "",
       "4DF3C3F68FCC83B27E9D42C90431A72499F17875C81A599B566C9889B9696703",
       "00000000000000000000003B78CE563F89A0ED9414F5AA28AD0D96D6795F9C6376AFB1548AF603B3EB45C9F8207DEE1060CB71C04E80F593060B07D28308D7F4",
       true},
      {"", "EEFDEA4CDB677750A420FEE807EACF21EB9898AE79B9768766E4FAA04A2D4A34", "",
       "243F6A8885A308D313198A2E03707344A4093822299F31D0082EFA98EC4E6C89",
       "6CFF5C3BA86C69EA4B7376F31A9BCB4F74C1976089B2D9963DA2E5543E17776969E89B4C5564D00349106B8497785DD7D1D713A8AE82B32FA79D5F7FC407D39B",
       false},
      {"", "DFF1D77F2A671C5F36183726DB2341BE58FEAE1DA2DECED843240F7B502BA659", "",
       "243F6A8885A308D313198A2E03707344A4093822299F31D0082EFA98EC4E6C89",
       "FFF97BD5755EEEA420453A14355235D382F6472F8568A18B2F057A14602975563CC27944640AC607CD107AE10923D9EF7A73C643E166BE5EBEAFA34B1AC553E2",
       false},
      {"", "DFF1D77F2A671C5F36183726DB2341BE58FEAE1DA2DECED843240F7B502BA659", "",
       "243F6A8885A308D313198A2E03707344A4093822299F31D0082EFA98EC4E6C89",
       "6CFF5C3BA86C69EA4B7376F31A9BCB4F74C1976089B2D9963DA2E5543E177769FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141",
       false},
  };
  for (const Row& row : rows) {
    unsigned char pubkey[32];
    unsigned char signature[64];
    std::vector<unsigned char> message;
    fill32(pubkey, row.pubkey);
    REQUIRE(parseHex(row.signature, signature, 64));
    fillBytes(message, row.message);
    const unsigned char* msg = message.empty() ? nullptr : message.data();
    REQUIRE(schnorrVerify(pubkey, msg, message.size(), signature) == row.ok);
    if (row.secret[0] == '\0') continue;
    unsigned char secret[32];
    unsigned char aux[32];
    unsigned char signedBytes[64];
    fill32(secret, row.secret);
    fill32(aux, row.aux);
    std::string error;
    REQUIRE(schnorrSign(secret, msg, message.size(), aux, signedBytes, error));
    std::string lower = row.signature;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    REQUIRE(hexEncode(signedBytes, 64) == lower);
    OPENSSL_cleanse(secret, sizeof(secret));
  }
}

TEST_CASE("BIP340 signs an empty message and a one-byte message") {
  unsigned char secret[32];
  unsigned char aux[32] = {};
  unsigned char signature[64];
  fill32(secret, "0340034003400340034003400340034003400340034003400340034003400340");
  std::string error;
  REQUIRE(schnorrSign(secret, nullptr, 0, aux, signature, error));
  REQUIRE(hexEncode(signature, 64) ==
          "71535db165ecd9fbbc046e5ffaea61186bb6ad436732fccc25291a55895464cf6069ce26bf03466228f19a3a62db8a649f2d560fac652827d1af0574e427ab63");
  const unsigned char one[] = {0x11};
  REQUIRE(schnorrSign(secret, one, 1, aux, signature, error));
  REQUIRE(hexEncode(signature, 64) ==
          "08a20a0afef64124649232e0693c583ab1b9934ae63b4c3511f3ae1134c6a303ea3173bfea6683bd101fa5aa5dbc1996fe7cacfc5a577d33ec14564cec2bacbf");
  OPENSSL_cleanse(secret, sizeof(secret));
}

TEST_CASE("NIP-44 matches the published conversation and payload") {
  unsigned char sec1[32] = {};
  unsigned char sec2[32] = {};
  sec1[31] = 1;
  sec2[31] = 2;
  unsigned char pub2[32];
  unsigned char pub1[32];
  std::string error;
  REQUIRE(publicKeyFromSecret(sec2, pub2, error));
  REQUIRE(publicKeyFromSecret(sec1, pub1, error));
  unsigned char left[32];
  unsigned char right[32];
  REQUIRE(conversationKey(sec1, pub2, left, error));
  REQUIRE(conversationKey(sec2, pub1, right, error));
  REQUIRE(hexEncode(left, 32) == "c41c775356fd92eadc63ff5a0dc1da211b268cbea22316767095b2871ea1412d");
  REQUIRE(hexEncode(right, 32) == hexEncode(left, 32));
  unsigned char nonce[32] = {};
  nonce[31] = 1;
  std::string payload;
  REQUIRE(nip44Encrypt(left, "a", nonce, payload, error));
  REQUIRE(payload ==
          "AgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABee0G5VSK0/9YypIObAtDKfYEAjD35uVkHyB0F4DwrcNaCXlCWZKaArsGrY6M9wnuTMxWfp1RTN9Xga8no+kF5Vsb");
  std::string plain;
  REQUIRE(nip44Decrypt(right, payload, plain, error));
  REQUIRE(plain == "a");
  payload.back() = payload.back() == 'b' ? 'c' : 'b';
  REQUIRE_FALSE(nip44Decrypt(right, payload, plain, error));
  REQUIRE(plain.empty());
  REQUIRE_FALSE(nip44Decrypt(right, "#not-base64", plain, error));
  unsigned char offCurve[32];
  REQUIRE(parseHex("eefdea4cdb677750a420fee807eacf21eb9898ae79b9768766e4faa04a2d4a34", offCurve, 32));
  REQUIRE_FALSE(xOnlyOnCurve(offCurve));
  REQUIRE_FALSE(conversationKey(sec1, offCurve, left, error));
  OPENSSL_cleanse(sec1, sizeof(sec1));
  OPENSSL_cleanse(sec2, sizeof(sec2));
  OPENSSL_cleanse(left, sizeof(left));
  OPENSSL_cleanse(right, sizeof(right));
}

TEST_CASE("NIP-44 long payloads match the published checksums") {
  unsigned char key[32];
  unsigned char nonce[32] = {};
  nonce[31] = 1;
  fill32(key, "c41c775356fd92eadc63ff5a0dc1da211b268cbea22316767095b2871ea1412d");
  const std::pair<std::size_t, const char*> rows[] = {
      {65535, "6d8c2810d1e870fbaa1f0a0937126cca837a15f9260e27060c331d70a3c0bc84"},
      {65536, "b7b4edb36ba92e267d322d56d9aebc22e7fa96ff52e3c12adc07f07a43cbc616"},
      {65537, "eeb7c7c5373894ea2c1547cfd3ccb15d5a0b2d619da852e5c79df792dcc9e435"},
  };
  const char* plainHashes[] = {
      "6e1bebca6a8229364a162a72ef064826c4cd7457bf54f190ef782bd9deff3e42",
      "bf718b6f653bebc184e1479f1935b8da974d701b893afcf49e701f3e2f9f9c5a",
      "008ffc88d3c96a9f307524eb361e47c5222a887fc45fa0c1fb8d429c5c23b430",
  };
  for (int index = 0; index < 3; ++index) {
    const std::string plain(rows[index].first, 'a');
    REQUIRE(sha256Hex(plain) == plainHashes[index]);
    std::string payload;
    std::string error;
    REQUIRE(nip44Encrypt(key, plain, nonce, payload, error));
    REQUIRE(sha256Hex(payload) == rows[index].second);
    std::string opened;
    REQUIRE(nip44Decrypt(key, payload, opened, error));
    REQUIRE(opened == plain);
  }
}

TEST_CASE("NIP-01 event id is the hash of the canonical array") {
  unsigned char secret[32];
  unsigned char aux[32] = {};
  fill32(secret, "0000000000000000000000000000000000000000000000000000000000000003");
  Event event;
  event.kind = 1;
  event.content = "hello";
  std::string error;
  REQUIRE(signEvent(event, secret, 1700000000, aux, error));
  REQUIRE(event.pubkey == "f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f9");
  std::string commitment;
  REQUIRE(eventCommitment(event, commitment, error));
  REQUIRE(commitment ==
          "[0,\"f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f9\",1700000000,1,[],\"hello\"]");
  REQUIRE(event.id == "a9d53fee641fe563de947fa330a3b4902e52e249894660aaa521cd039e896128");
  REQUIRE(verifyEvent(event));
  unsigned char raw[32];
  unsigned char signature[64];
  REQUIRE(parseHex(event.id, raw, 32));
  REQUIRE(parseHex(event.sig, signature, 64));
  REQUIRE(schnorrVerify(secret, raw, 32, signature) == false);
  unsigned char pubkey[32];
  REQUIRE(parseHex(event.pubkey, pubkey, 32));
  REQUIRE(schnorrVerify(pubkey, raw, 32, signature));
  REQUIRE_FALSE(schnorrVerify(pubkey, reinterpret_cast<const unsigned char*>(event.id.data()), event.id.size(), signature));
  event.content.push_back('!');
  REQUIRE_FALSE(verifyEvent(event));
  Event quoted;
  quoted.pubkey = event.pubkey;
  quoted.createdAt = 10;
  quoted.kind = 1;
  quoted.tags = {{"p", "abcd"}};
  quoted.content = "say \"hi\"\n";
  REQUIRE(eventCommitment(quoted, commitment, error));
  REQUIRE(commitment.find("say \\\"hi\\\"\\n") != std::string::npos);
  REQUIRE(commitment.find("[\"p\",\"abcd\"]") != std::string::npos);
  OPENSSL_cleanse(secret, sizeof(secret));
}

TEST_CASE("nsec login stays in memory and bunker login waits for the user key") {
  timberlab::Document document;
  const std::string before = document.toJson();
  SignerSession session;
  std::string error;
  REQUIRE(session.loginWithNsec("  nsec1vl029mgpspedva04g90vltkh6fvh240zqtv9k0t9af8935ke9laqsnlfe5\n", error));
  REQUIRE(session.signedIn());
  REQUIRE(session.method() == SignerMethod::Nsec);
  const std::string npub = session.npub();
  REQUIRE(npub.rfind("npub1", 0) == 0);
  Event note;
  note.kind = 1;
  note.content = "local";
  REQUIRE(session.signLocal(note, 1700000001, error));
  REQUIRE(verifyEvent(note));
  REQUIRE(note.pubkey != npub);
  session.logout();
  REQUIRE_FALSE(session.signedIn());
  REQUIRE_FALSE(session.signLocal(note, 1700000001, error));
  REQUIRE(session.loginWithNsec("67dea2ed018072d675f5415ecfaed7d2597555e202d85b3d65ea4e58d2d92ffa", error));
  REQUIRE(session.npub() == npub);
  session.logout();
  unsigned char invalid[32];
  std::memset(invalid, 0xff, sizeof(invalid));
  std::string badNsec;
  REQUIRE(encodeNsec(invalid, badNsec, error));
  REQUIRE_FALSE(session.loginWithNsec(badNsec, error));
  REQUIRE(error == "signer.nsec");
  REQUIRE(document.toJson() == before);

  unsigned char bunkerSecret[32] = {};
  bunkerSecret[31] = 2;
  unsigned char bunkerPub[32];
  REQUIRE(publicKeyFromSecret(bunkerSecret, bunkerPub, error));
  const std::string bunkerHex = hexEncode(bunkerPub, 32);
  const std::string uri = "bunker://" + bunkerHex +
                          "?relay=ws%3A%2F%2Fnope.example&relay=wss%3A%2F%2Frelay.example.com%2Fpath&secret=alpha";
  BunkerUri parsed;
  REQUIRE(parseBunkerUri(uri, parsed, error));
  REQUIRE(parsed.relays.size() == 1);
  REQUIRE(parsed.relays[0] == "wss://relay.example.com/path");
  REQUIRE(parsed.secret == "alpha");
  REQUIRE_FALSE(parseBunkerUri("nostrconnect://aa?relay=wss://relay.example.com", parsed, error));
  REQUIRE_FALSE(parseBunkerUri("bunker://" + bunkerHex + "?relay=ws://nope.example", parsed, error));

  REQUIRE(session.beginBunker(uri, 1700000100, error));
  REQUIRE(session.state() == SignerState::WaitConnect);
  REQUIRE_FALSE(session.signedIn());
  Event connect;
  REQUIRE(session.pendingEvent(connect));
  REQUIRE(verifyEvent(connect));
  REQUIRE(connect.kind == 24133);
  REQUIRE(connect.tags.at(0).at(0) == "p");
  REQUIRE(connect.tags.at(0).at(1) == bunkerHex);
  REQUIRE(connect.content.find("alpha") == std::string::npos);
  REQUIRE(connect.content.front() != '{');
  unsigned char client[32];
  REQUIRE(parseHex(connect.pubkey, client, 32));
  unsigned char conversation[32];
  REQUIRE(conversationKey(bunkerSecret, client, conversation, error));
  std::string plain;
  REQUIRE(nip44Decrypt(conversation, connect.content, plain, error));
  const auto request = nlohmann::json::parse(plain);
  REQUIRE(request.at("method") == "connect");
  REQUIRE(request.at("params").at(0) == bunkerHex);
  REQUIRE(request.at("params").at(1) == "alpha");
  REQUIRE(request.at("params").at(2) == "get_public_key");
  REQUIRE(request.at("params").at(3) == "{\"name\":\"TimberLab\"}");
  OPENSSL_cleanse(conversation, sizeof(conversation));

  Event junk = connect;
  junk.content.push_back('A');
  REQUIRE(session.acceptRemoteEvent(junk, 1700000101, error) == RemoteResult::Ignored);
  REQUIRE(session.state() == SignerState::WaitConnect);

  Event auth = bunkerReply(bunkerSecret, connect, "auth_url", "https://example.com/approve", 1700000101);
  REQUIRE(session.acceptRemoteEvent(auth, 1700000102, error) == RemoteResult::AuthRequired);
  REQUIRE(session.detail() == "https://example.com/approve");
  REQUIRE_FALSE(session.signedIn());

  Event ack = bunkerReply(bunkerSecret, connect, "ack", "", 1700000103);
  REQUIRE(session.acceptRemoteEvent(ack, 1700000104, error) == RemoteResult::Continue);
  REQUIRE(session.state() == SignerState::WaitPublicKey);
  Event second;
  REQUIRE(session.pendingEvent(second));
  Event rejected = bunkerReply(bunkerSecret, second, "", "nope", 1700000105);
  SignerSession failed;
  REQUIRE(failed.beginBunker(uri, 1700000100, error));
  Event failedConnect;
  REQUIRE(failed.pendingEvent(failedConnect));
  Event failedReply = bunkerReply(bunkerSecret, failedConnect, "", "nope", 1700000106);
  REQUIRE(failed.acceptRemoteEvent(failedReply, 1700000106, error) == RemoteResult::Failed);
  REQUIRE(error == "signer.rejected");
  REQUIRE(failed.detail() == "nope");
  REQUIRE_FALSE(failed.signedIn());

  unsigned char userSecret[32];
  fill32(userSecret, "0000000000000000000000000000000000000000000000000000000000000003");
  unsigned char userPub[32];
  REQUIRE(publicKeyFromSecret(userSecret, userPub, error));
  Event identity = bunkerReply(bunkerSecret, second, hexEncode(userPub, 32), "", 1700000107);
  REQUIRE(session.acceptRemoteEvent(identity, 1700000108, error) == RemoteResult::SignedIn);
  REQUIRE(session.signedIn());
  REQUIRE(session.method() == SignerMethod::Bunker);
  std::string userNpub;
  REQUIRE(encodeNpub(userPub, userNpub, error));
  REQUIRE(session.npub() == userNpub);
  REQUIRE(session.pubkeyHex() == hexEncode(userPub, 32));
  REQUIRE_FALSE(session.signLocal(note, 1700000109, error));
  REQUIRE(document.toJson() == before);
  session.logout();
  REQUIRE_FALSE(session.signedIn());
  OPENSSL_cleanse(bunkerSecret, sizeof(bunkerSecret));
  OPENSSL_cleanse(userSecret, sizeof(userSecret));
}

TEST_CASE("Amber login is a public key from the Android signer") {
  const AmberRequest request = amberGetPublicKeyRequest();
  REQUIRE(request.packageName == "com.greenart7c3.nostrsigner");
  REQUIRE(request.action == "android.intent.action.VIEW");
  REQUIRE(request.dataUri == "nostrsigner:");
  REQUIRE(request.typeExtra == "get_public_key");
  REQUIRE(request.contentAuthority == "com.greenart7c3.nostrsigner.GET_PUBLIC_KEY");
  REQUIRE(request.permissionsJson.find("sign_event") == std::string::npos);
  REQUIRE(request.permissionsJson.find("get_public_key") != std::string::npos);

  unsigned char pubkey[32];
  std::string error;
  REQUIRE(decodeNpub("npub180cvv07tjdrrgpa0j7j7tmnyl2yr6yr7l8j4s3evf6u64th6gkwsyjh6w6", pubkey, error));
  SignerSession session;
  REQUIRE(session.loginWithPublicKey(hexEncode(pubkey, 32), SignerMethod::Amber, error));
  REQUIRE(session.signedIn());
  REQUIRE(session.method() == SignerMethod::Amber);
  REQUIRE(session.npub() == "npub180cvv07tjdrrgpa0j7j7tmnyl2yr6yr7l8j4s3evf6u64th6gkwsyjh6w6");
  Event note;
  note.kind = 1;
  REQUIRE_FALSE(session.signLocal(note, 10, error));
  REQUIRE_FALSE(session.loginWithPublicKey(hexEncode(pubkey, 32), SignerMethod::Nsec, error));
  REQUIRE_FALSE(session.signedIn());
}

TEST_CASE("WebSocket frames and relay messages round-trip") {
  REQUIRE(websocketAccept("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
  const unsigned char mask[4] = {0x01, 0x02, 0x03, 0x04};
  const auto client = websocketClientFrame(0x1, "hi", mask);
  const unsigned char expected[] = {0x81, 0x82, 0x01, 0x02, 0x03, 0x04, 0x69, 0x6b};
  REQUIRE(client.size() == sizeof(expected));
  REQUIRE(std::memcmp(client.data(), expected, sizeof(expected)) == 0);
  const std::string longer(200, 'a');
  const unsigned char longMask[4] = {0, 0, 0, 0};
  const auto wide = websocketClientFrame(0x1, longer, longMask);
  REQUIRE(wide[1] == static_cast<unsigned char>(0x80 | 126));
  REQUIRE(wide[2] == 0);
  REQUIRE(wide[3] == 200);

  const auto hello = serverFrame(0x1, true, "hi");
  WebsocketFrame frame;
  std::size_t used = 0;
  std::string error;
  REQUIRE(websocketReadFrame(hello.data(), hello.size(), used, frame, error) == FrameStatus::Ok);
  REQUIRE(used == hello.size());
  REQUIRE(frame.payload == "hi");
  REQUIRE(websocketReadFrame(hello.data(), 1, used, frame, error) == FrameStatus::NeedMore);
  unsigned char masked[4] = {0x81, 0x82, 0x00, 0x00};
  REQUIRE(websocketReadFrame(masked, sizeof(masked), used, frame, error) == FrameStatus::Failed);

  WebsocketReader reader;
  std::vector<timberlab::storage::WsIncoming> incoming;
  const auto part = serverFrame(0x1, false, "hel");
  const auto ping = serverFrame(0x9, true, "p");
  const auto rest = serverFrame(0x0, true, "lo");
  REQUIRE(reader.consume(part.data(), part.size(), incoming, error));
  REQUIRE(incoming.empty());
  REQUIRE(reader.consume(ping.data(), ping.size(), incoming, error));
  REQUIRE(incoming.size() == 1);
  REQUIRE(incoming[0].kind == WsKind::Ping);
  incoming.clear();
  REQUIRE(reader.consume(rest.data(), rest.size(), incoming, error));
  REQUIRE(incoming.size() == 1);
  REQUIRE(incoming[0].kind == WsKind::Text);
  REQUIRE(incoming[0].data == "hello");

  const std::string handshake =
      "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\nleftover";
  REQUIRE(readHandshake(handshake, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", used, error) == HandshakeStatus::Ok);
  REQUIRE(handshake.substr(used) == "leftover");
  REQUIRE(readHandshake("HTTP/1.1 101 Switching Protocols\r\n", "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", used, error) ==
          HandshakeStatus::NeedMore);

  unsigned char secret[32];
  fill32(secret, "0000000000000000000000000000000000000000000000000000000000000003");
  Event event;
  event.kind = 1;
  event.content = "wire";
  unsigned char aux[32] = {};
  REQUIRE(signEvent(event, secret, 20, aux, error));
  const auto posted = nlohmann::json::parse(relayEventMessage(event));
  const nlohmann::json server = {"EVENT", "sub1", posted.at(1)};
  RelayMessage message;
  REQUIRE(parseRelayMessage(server.dump(), message, error));
  REQUIRE(message.type == "EVENT");
  REQUIRE(message.sub == "sub1");
  REQUIRE(message.event.id == event.id);
  REQUIRE(verifyEvent(message.event));
  REQUIRE(parseRelayMessage("[\"OK\",\"abc\",false,\"blocked: no\"]", message, error));
  REQUIRE_FALSE(message.accepted);
  REQUIRE(message.text == "blocked: no");
  REQUIRE(parseRelayMessage("[\"AUTH\",\"challenge\"]", message, error));
  REQUIRE(message.type == "AUTH");
  REQUIRE(message.text == "challenge");
  const std::string req = relayReqMessage("sub1", event.pubkey, 100);
  REQUIRE(req.find(event.pubkey) != std::string::npos);
  REQUIRE(req.find("24133") != std::string::npos);
  REQUIRE(req.find("nsec") == std::string::npos);
  OPENSSL_cleanse(secret, sizeof(secret));
}
