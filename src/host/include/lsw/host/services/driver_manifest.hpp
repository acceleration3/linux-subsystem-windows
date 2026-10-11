#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

namespace lsw::host::services {

// A driver package's package.sha256: the SHA-256 of each of its files, as
// sha256sum writes them ("<hash>  <name>"). The manager ships one with the
// package, and the guest's agent reports the package Windows has in the
// same form, so comparing the two says whether LSW brought a different
// driver than Windows has, and which files changed. Both also carry
// provision.ps1 and the OpenCL loaders, which an update brings too.
//
// Names compare without case, as Windows' do. A line that is not a hash and
// a name is skipped.
class DriverManifest {
public:
    DriverManifest() = default;
    explicit DriverManifest(const QString& text);

    // The file at ``path``; empty when it cannot be read.
    [[nodiscard]] static DriverManifest read(const QString& path);

    // The file at ``path`` under ``name`` ("provision.ps1",
    // "loaders/x64/vulkan-1.dll"), by its SHA-256; nothing when it cannot
    // be read.
    void add(const QString& name, const QString& path);

    [[nodiscard]] bool empty() const noexcept { return files_.isEmpty(); }

    // The files whose hash differs between the two, or that only one of
    // them has, by name, sorted.
    [[nodiscard]] QStringList differences(const DriverManifest& other) const;

private:
    QMap<QString, QString> files_;  // lower-case name -> lower-case hash
};

}  // namespace lsw::host::services
