#include "AmberBridge.hpp"

#include "DocumentController.hpp"
#include "timberlab/storage/nostr.hpp"

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#include <QMetaObject>
#include <QPointer>
#include <QtCore/private/qandroidextras_p.h>
#endif

namespace timberlab::ui {

bool amberSignerAvailable() {
#if defined(Q_OS_ANDROID)
  return true;
#else
  return false;
#endif
}

#if defined(Q_OS_ANDROID)

QString amberResult(int resultCode, const QJniObject& data) {
  constexpr int kResultOk = -1;
  if (resultCode != kResultOk || !data.isValid()) return QStringLiteral("signer.amber");
  const QJniObject rejectedKey = QJniObject::fromString(QStringLiteral("rejected"));
  const bool rejected = data.callMethod<jboolean>("getBooleanExtra", "(Ljava/lang/String;Z)Z", rejectedKey.object<jstring>(),
                                                  jboolean(false));
  if (rejected) return QStringLiteral("signer.rejected");
  return QString();
}

QString amberPubkey(const QJniObject& data) {
  const QJniObject key = QJniObject::fromString(QStringLiteral("result"));
  const QJniObject value = data.callObjectMethod("getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;", key.object<jstring>());
  return value.toString();
}

void putExtra(QJniObject& intent, const char* key, const QString& value) {
  const QJniObject name = QJniObject::fromString(QString::fromUtf8(key));
  const QJniObject text = QJniObject::fromString(value);
  intent.callObjectMethod("putExtra", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;", name.object<jstring>(),
                          text.object<jstring>());
}

#endif

bool requestAmberPublicKey(DocumentController* owner) {
#if !defined(Q_OS_ANDROID)
  (void)owner;
  return false;
#else
  if (owner == nullptr) return false;
  const storage::AmberRequest request = storage::amberGetPublicKeyRequest();
  const QJniObject action = QJniObject::fromString(QString::fromStdString(request.action));
  QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", action.object<jstring>());
  const QJniObject uriText = QJniObject::fromString(QString::fromStdString(request.dataUri));
  const QJniObject uri = QJniObject::callStaticObjectMethod("android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                                                            uriText.object<jstring>());
  intent.callObjectMethod("setData", "(Landroid/net/Uri;)Landroid/content/Intent;", uri.object());
  const QJniObject packageName = QJniObject::fromString(QString::fromStdString(request.packageName));
  intent.callObjectMethod("setPackage", "(Ljava/lang/String;)Landroid/content/Intent;", packageName.object<jstring>());
  putExtra(intent, "type", QString::fromStdString(request.typeExtra));
  putExtra(intent, "permissions", QString::fromStdString(request.permissionsJson));
  QPointer<DocumentController> alive(owner);
  QtAndroidPrivate::startActivity(intent, 55, [alive](int, int resultCode, const QJniObject& data) {
    const QString code = amberResult(resultCode, data);
    const QString pubkey = code.isEmpty() ? amberPubkey(data) : QString();
    if (alive == nullptr) return;
    QMetaObject::invokeMethod(alive.data(), [alive, pubkey, code] {
      if (alive != nullptr) alive->completeAmberLogin(pubkey, code);
    }, Qt::QueuedConnection);
  });
  return true;
#endif
}

}  // namespace timberlab::ui
