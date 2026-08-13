#pragma once

#include <QObject>
#include <QString>

namespace lsw::host::provision::iso {

// SHA-256 hash verifier. Streams the file in chunks so multi-GB ISOs
// don't blow up RAM. Runs synchronously — call from a worker thread
// (the wizard does this via QtConcurrent).
class Verifier {
public:
    [[nodiscard]] static QString sha256_hex(const QString& path);

    // ``expected_hex`` may be empty (returns true with a warning log).
    [[nodiscard]] static bool verify(const QString& path, const QString& expected_hex);
};

}  // namespace lsw::host::provision::iso
