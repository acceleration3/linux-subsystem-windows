#pragma once

#include <QObject>
#include <QSet>
#include <QString>

class QProcess;

namespace lsw::host::platform {

// Lets the user open one kind of USB device.
//
// /dev/bus/usb nodes belong to root, and QEMU runs as the user, so a
// device cannot be handed to the guest until udev says the user may open
// it. That is a rule for one vendor:product (packaging/lsw-usb-access,
// run as root through pkexec): it persists across replugs, and nothing
// else on the bus is opened by it.
class UsbAccess : public QObject {
    Q_OBJECT
public:
    explicit UsbAccess(QObject* parent = nullptr);

    // The helper script: installed beside the manager, or in the source
    // tree's packaging/ for a manager run from its build tree. Empty when
    // there is none.
    [[nodiscard]] static QString helper_path();

    // Ask for the right to open devices of this type; the password prompt
    // is pkexec's. Returns at once and answers with ``finished``. False
    // when a request for the same type is already waiting.
    bool grant(int vendor, int product);

    [[nodiscard]] bool granting(int vendor, int product) const;

Q_SIGNALS:
    // ``error`` is a sentence when ``ok`` is false: the prompt was
    // dismissed, pkexec or the helper is missing, or the helper failed.
    void finished(int vendor, int product, bool ok, const QString& error);

private:
    [[nodiscard]] static QString key(int vendor, int product);
    void done(const QString& key, int vendor, int product, bool ok, const QString& error);

    QSet<QString> granting_;
};

}  // namespace lsw::host::platform
