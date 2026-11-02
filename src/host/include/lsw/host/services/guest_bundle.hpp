#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

#include <memory>
#include <string>

namespace lsw::host::config { class ManagerSettings; class Settings; }
namespace lsw::host::vm { struct DomainDescription; }

namespace lsw::host::services {

// Every file LSW puts into the guest, as this manager would ship it now,
// each with its SHA-256 and the path it takes there, and the release those
// make (docs/guest-update.md, "The release").
//
// The agent, the update script and the driver tree are required; the
// mirror's guest half, the Vulkan loaders and the shared folders' files are
// optional and logged where missing. The payload ISO carries this bundle
// for a fresh install (PayloadIsoStager). Two files are made here rather
// than found: ``C:\av\adapters.txt`` (this host's GPUs named, versioned
// from the Update Catalog) and the mount list for lsw-shares.ps1, which is
// carried at a fixed pseudo path under the update directory so that a
// changed list is a new release.
//
// Built off the GUI thread (the files are read and hashed, the GPUs
// enumerated) apart from the catalog search, which is the network manager's.
class GuestBundle : public QObject {
    Q_OBJECT
public:
    struct File {
        std::string guest_path;
        QByteArray bytes;
        std::string sha256;
    };

    struct Contents {
        // Why the bundle cannot be made; empty when it can. Nothing else is
        // meaningful then.
        QString error;
        // Sorted by guest path.
        QVector<File> files;
        // "<sha256>  <guest path>\n" per file.
        std::string manifest;
        // The SHA-256 of ``manifest``, in lowercase hex.
        std::string release;
        // The value of lsw-shares.ps1's -Shares.
        std::string shares;
        // The LSW version that made it. Not in the manifest: it names the
        // release for people and does not change it.
        std::string version;
    };

    // One shared folder as Windows mounts it (lsw-shares.ps1).
    struct Mount {
        QString tag;    // the virtio-fs device's
        QString drive;  // "Z:"
        QString name;   // what Explorer calls the drive: the folder's name
    };

    // The mounts the shares of ``definition`` make. Each tag is its drive
    // letter, lower-cased (domain_inputs_from_settings), so the drive is
    // the tag upper-cased; a tag that is not a single letter is no share
    // of LSW's and is left out.
    [[nodiscard]] static QVector<Mount> mounts_of(const vm::DomainDescription& definition);

    // The same from the settings alone, for a machine that has no definition
    // yet (the install): the shares the definition will be made with
    // (domain_inputs_from_settings).
    [[nodiscard]] static QVector<Mount> mounts_of(const config::ManagerSettings& settings);

    // Where the update script finds a file of the bundle in its staging
    // directory: the guest path with the drive's colon removed
    // (``C:\av\a.sys`` is ``C\av\a.sys``).
    [[nodiscard]] static std::string staged_path(const std::string& guest_path);

    // lsw-shares.ps1's -Shares for ``mounts``: tag=drive=name between commas,
    // the name in base64 of its UTF-8, or "none".
    [[nodiscard]] static QString shares_argument(const QVector<Mount>& mounts);

    // Where the mount list is in the manifest.
    static constexpr const char* kSharesGuestPath = "C:\\ProgramData\\LSW\\update\\shares.txt";

    // The files on disk, hashed, with ``adapter_names`` (already versioned)
    // as adapters.txt and ``shares`` as the mount list. Blocking, and no
    // network: public so that the bundle can be checked without the GPU
    // enumeration and the catalog search that ``build`` does first.
    [[nodiscard]] static std::shared_ptr<const Contents> collect(const QString& adapter_names,
                                                                 const std::string& shares);

    explicit GuestBundle(config::Settings& settings, QObject* parent = nullptr);

    // Makes the bundle for a machine with ``mounts`` and announces it with
    // ``built``, once per call. A call while one is going is ignored.
    void build(const QVector<Mount>& mounts);

    [[nodiscard]] bool building() const noexcept { return building_; }

Q_SIGNALS:
    void built(std::shared_ptr<const GuestBundle::Contents> contents);

private:
    config::Settings& settings_;
    bool building_ = false;
};

}  // namespace lsw::host::services
