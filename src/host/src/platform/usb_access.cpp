#include "lsw/host/platform/usb_access.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/platform/build_artifact.hpp"

#include <QProcess>
#include <QStandardPaths>

namespace lsw::host::platform {

namespace {
constexpr int kNotAuthorized = 126;
constexpr int kDismissed = 127;

}

UsbAccess::UsbAccess(QObject* parent) : QObject(parent) {}

QString UsbAccess::helper_path() {
    return locate_build_artifact(QStringLiteral("LSW_USB_ACCESS"),
                                 QStringLiteral("/../../../packaging/lsw-usb-access"),
                                 QStringLiteral("/../lib/lsw/lsw-usb-access"));
}

QString UsbAccess::key(int vendor, int product) {
    return QStringLiteral("%1:%2")
        .arg(vendor, 4, 16, QLatin1Char('0'))
        .arg(product, 4, 16, QLatin1Char('0'));
}

bool UsbAccess::granting(int vendor, int product) const {
    return granting_.contains(key(vendor, product));
}

void UsbAccess::done(const QString& k, int vendor, int product, bool ok, const QString& error) {
    granting_.remove(k);
    if (ok) {
        LSW_INFO("USB access for {} granted", k.toStdString());
    } else {
        LSW_WARN("USB access for {}: {}", k.toStdString(), error.toStdString());
    }
    Q_EMIT finished(vendor, product, ok, error);
}

bool UsbAccess::grant(int vendor, int product) {
    const QString k = key(vendor, product);
    if (granting_.contains(k)) {
        return false;
    }
    granting_.insert(k);
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pkexec.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, k, vendor, product] {
            done(k, vendor, product, false,
                 QStringLiteral("pkexec is not installed; install polkit to allow access to this device"));
        }, Qt::QueuedConnection);
        return true;
    }
    const QString helper = helper_path();
    if (helper.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, k, vendor, product] {
            done(k, vendor, product, false, QStringLiteral("lsw-usb-access is not installed"));
        }, Qt::QueuedConnection);
        return true;
    }
    const QStringList arguments = {helper, QStringLiteral("%1").arg(vendor, 4, 16, QLatin1Char('0')),
                                   QStringLiteral("%1").arg(product, 4, 16, QLatin1Char('0'))};
    LSW_INFO("asking for access to USB {} through pkexec", k.toStdString());
    auto* process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::errorOccurred, this, [this, process, k, vendor, product](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            process->deleteLater();
            done(k, vendor, product, false, QStringLiteral("pkexec could not be started"));
        }
    });
    connect(process, &QProcess::finished, this,
            [this, process, k, vendor, product](int code, QProcess::ExitStatus status) {
                const QString output = QString::fromLocal8Bit(process->readAll()).trimmed();
                LSW_DEBUG("USB {}: the helper exited with code {}", k.toStdString(), code);
                process->deleteLater();
                if (status == QProcess::NormalExit && code == 0) {
                    done(k, vendor, product, true, QString{});
                } else if (status == QProcess::NormalExit && (code == kDismissed || code == kNotAuthorized)) {
                    done(k, vendor, product, false,
                         code == kNotAuthorized
                             ? QStringLiteral("you are not allowed to open this device")
                             : QStringLiteral("cancelled: the password was not given"));
                } else {
                    done(k, vendor, product, false,
                         output.isEmpty() ? QStringLiteral("the helper failed (exit %1)").arg(code) : output);
                }
            });
    LSW_DEBUG("pkexec {} {}", pkexec.toStdString(), helper.toStdString());
    process->start(pkexec, arguments);
    return true;
}

}
