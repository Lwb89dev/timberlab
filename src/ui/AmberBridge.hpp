#pragma once

namespace timberlab::ui {

class DocumentController;

// Amber is an Android NIP-55 signer. This desktop build reports that it is
// not available and does not contact a device.
bool amberSignerAvailable();
bool requestAmberPublicKey(DocumentController* owner);

}  // namespace timberlab::ui
