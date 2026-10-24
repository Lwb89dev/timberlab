#include "RelaySocket.hpp"

#include <openssl/rand.h>

#include <QSignalBlocker>
#include <QSslConfiguration>
#include <QSslError>

#include <string_view>
#include <vector>

namespace timberlab::ui {

namespace {

// Two full frames. A relay that sends faster than this is read waits in the
// kernel instead of in this process.
constexpr qint64 kReadBuffer = 512 * 1024;

}  // namespace

RelaySocket::RelaySocket(QObject* parent) : QObject(parent), socket_(this) {
  socket_.setPeerVerifyMode(QSslSocket::VerifyPeer);
  socket_.setReadBufferSize(kReadBuffer);
  QSslConfiguration tls = socket_.sslConfiguration();
  tls.setProtocol(QSsl::TlsV1_2OrLater);
  socket_.setSslConfiguration(tls);
  connect(&socket_, &QSslSocket::encrypted, this, [this] {
    if (closing_ || httpRequest_.empty()) return;
    socket_.write(httpRequest_.data(), static_cast<qint64>(httpRequest_.size()));
  });
  connect(&socket_, &QSslSocket::readyRead, this, [this] {
    const QByteArray chunk = socket_.readAll();
    if (chunk.isEmpty() || closing_) return;
    if (!upgraded_) {
      httpBuffer_.append(chunk.constData(), static_cast<std::size_t>(chunk.size()));
      std::size_t used = 0;
      std::string error;
      const auto status = storage::readHandshake(httpBuffer_, expectedAccept_, used, error);
      if (status == storage::HandshakeStatus::NeedMore) return;
      if (status != storage::HandshakeStatus::Ok) {
        fail(QStringLiteral("signer.relay"));
        return;
      }
      const std::string leftover = httpBuffer_.substr(used);
      httpBuffer_.clear();
      upgraded_ = true;
      emit connected();
      if (!leftover.empty()) consume(reinterpret_cast<const unsigned char*>(leftover.data()), static_cast<int>(leftover.size()));
      return;
    }
    consume(reinterpret_cast<const unsigned char*>(chunk.constData()), chunk.size());
  });
  connect(&socket_, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
    if (!closing_) fail(QStringLiteral("signer.relay"));
  });
  connect(&socket_, &QSslSocket::sslErrors, this, [this](const QList<QSslError>&) {
    if (!closing_) fail(QStringLiteral("signer.relay"));
  });
}

void RelaySocket::open(const QString& url) {
  close();
  storage::RelayLocation location;
  std::string error;
  if (!storage::parseRelayUrl(url.toStdString(), location, error)) {
    emit failed(QStringLiteral("signer.relay"));
    return;
  }
  storage::WebsocketOpening opening;
  if (!storage::websocketOpening(location.hostHeader, location.pathAndQuery, opening, error)) {
    emit failed(QStringLiteral("signer.crypto"));
    return;
  }
  expectedAccept_ = std::move(opening.accept);
  httpRequest_ = std::move(opening.httpRequest);
  closing_ = false;
  socket_.connectToHostEncrypted(QString::fromStdString(location.host), static_cast<quint16>(location.port));
}

void RelaySocket::sendText(const QString& text) {
  if (!upgraded_ || closing_) return;
  const QByteArray utf8 = text.toUtf8();
  unsigned char mask[4];
  if (RAND_bytes(mask, sizeof(mask)) != 1) {
    fail(QStringLiteral("signer.crypto"));
    return;
  }
  const auto frame = storage::websocketClientFrame(0x1, std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())), mask);
  socket_.write(reinterpret_cast<const char*>(frame.data()), static_cast<qint64>(frame.size()));
}

void RelaySocket::close() {
  closing_ = true;
  upgraded_ = false;
  httpBuffer_.clear();
  httpRequest_.clear();
  expectedAccept_.clear();
  reader_.reset();
  const QSignalBlocker blocker(&socket_);
  socket_.abort();
}

void RelaySocket::fail(const QString& code) {
  if (closing_) return;
  close();
  emit failed(code);
}

void RelaySocket::consume(const unsigned char* data, int size) {
  if (size <= 0 || closing_) return;
  std::vector<storage::WsIncoming> incoming;
  std::string error;
  if (!reader_.consume(data, static_cast<std::size_t>(size), incoming, error)) {
    fail(QStringLiteral("signer.relay"));
    return;
  }
  for (const storage::WsIncoming& item : incoming) {
    if (item.kind == storage::WsKind::Text) {
      emit textMessage(QByteArray(item.data.data(), static_cast<qsizetype>(item.data.size())));
      if (closing_ || !upgraded_) return;
      continue;
    }
    if (item.kind == storage::WsKind::Close) {
      close();
      emit disconnected();
      return;
    }
    if (item.kind != storage::WsKind::Ping) continue;
    unsigned char mask[4];
    if (RAND_bytes(mask, sizeof(mask)) != 1) {
      fail(QStringLiteral("signer.crypto"));
      return;
    }
    const auto frame = storage::websocketClientFrame(0xA, item.data, mask);
    socket_.write(reinterpret_cast<const char*>(frame.data()), static_cast<qint64>(frame.size()));
  }
}

}  // namespace timberlab::ui
