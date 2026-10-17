#pragma once

#include "timberlab/storage/nostr.hpp"

#include <QSslSocket>

#include <string>

namespace timberlab::ui {

// A bunker relay socket. It carries signer messages only. It does not open a
// project file.
class RelaySocket : public QObject {
  Q_OBJECT
 public:
  explicit RelaySocket(QObject* parent = nullptr);

  void open(const QString& url);
  void sendText(const QString& text);
  void close();

 signals:
  void connected();
  void textMessage(const QString& text);
  void failed(const QString& code);
  void disconnected();

 private:
  void fail(const QString& code);
  void consume(const unsigned char* data, int size);

  QSslSocket socket_;
  storage::WebsocketReader reader_;
  std::string expectedAccept_;
  std::string httpRequest_;
  std::string httpBuffer_;
  bool upgraded_ = false;
  bool closing_ = false;
};

}  // namespace timberlab::ui
