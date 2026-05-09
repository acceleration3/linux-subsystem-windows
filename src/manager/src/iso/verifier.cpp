#include "lsw/manager/iso/verifier.hpp"

#include "lsw/log.hpp"

#include <QCryptographicHash>
#include <QFile>

namespace lsw::manager::iso {

QString Verifier::sha256_hex(const QString& path) {
    QFile f{path};
    if (!f.open(QIODevice::ReadOnly)) {
        LSW_ERROR("verify open {}: {}", path.toStdString(), f.errorString().toStdString());
        return {};
    }
    QCryptographicHash h{QCryptographicHash::Sha256};
    if (!h.addData(&f)) {
        return {};
    }
    return QString::fromLatin1(h.result().toHex());
}

bool Verifier::verify(const QString& path, const QString& expected_hex) {
    if (expected_hex.isEmpty()) {
        LSW_WARN("no SHA-256 known for {} — skipping integrity check", path.toStdString());
        return true;
    }
    const auto actual = sha256_hex(path);
    if (actual.compare(expected_hex, Qt::CaseInsensitive) != 0) {
        LSW_ERROR("hash mismatch for {} (expected {}, got {})", path.toStdString(),
                  expected_hex.toStdString(), actual.toStdString());
        return false;
    }
    return true;
}

}  // namespace lsw::manager::iso
