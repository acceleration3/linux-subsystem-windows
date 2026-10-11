#pragma once

#include "lsw/host/apps/guest_file_transfer.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::manager {

class GuestDriverInstaller;

// Puts the driver package LSW ships into the guest and installs it.
//
// The package travels with LSW (AccelVirtPaths::driver_tree), so an LSW
// update can bring a new one; the guest keeps the one it last installed in
// C:\av\pristine. Updating is: upload each of the tree's files through the
// agent, one after another, and then the installer's re-sign and install,
// with a staging step first that moves the uploads into place - the package
// to C:\av\incoming\package, which the installer installs from and makes
// the pristine one only once it is installed (so an update that fails is
// still offered), provision.ps1 to C:\av, the OpenCL loaders to
// C:\av\loaders. A loader is also replaced in the
// Windows directory when the copy there is the one setup put there: one
// Windows shipped of its own is kept, as setup keeps it.
class GuestDriverUpdate : public QObject {
    Q_OBJECT
public:
    // A file of the tree and where it goes in the guest.
    struct File {
        QString local;
        QString guest;
    };

    GuestDriverUpdate(link::AgentRpcClient& rpc, GuestDriverInstaller& installer,
                      QObject* parent = nullptr);

    // Everything an update brings and where each goes: the files of
    // ``tree`` (AccelVirtPaths::driver_tree), and the Vulkan loaders,
    // which setup put beside the tree's OpenCL ones from this project's
    // own builds (AccelVirtPaths::vulkan_loader; empty when there is
    // none). Empty when the tree holds no package.
    [[nodiscard]] static QVector<File> files_of(const QString& tree, const QString& vulkan_x64,
                                                const QString& vulkan_x86);

    // How the guest's gpu_status names ``file`` beside the package's own
    // ("provision.ps1", "loaders/x64/vulkan-1.dll"): its place under
    // C:\av. Empty for a file of the package, which package.sha256 names.
    [[nodiscard]] static QString manifest_name(const File& file);

    // The installer's staging for ``files``, uploaded to ``uploaded`` (the
    // agent's paths, in the same order).
    [[nodiscard]] static QString staging(const QVector<File>& files, const QStringList& uploaded);

    // Upload ``files`` (files_of) and install them. False, with ``failed``
    // emitted, when there are none. Once the uploads are done the
    // installer runs, and its own signals say how that went.
    bool start(const QVector<File>& files);
    [[nodiscard]] bool uploading() const noexcept { return !files_.isEmpty(); }

    // The agent went away during the uploads.
    void abandon(const QString& reason);

Q_SIGNALS:
    void progress(const QString& message);
    void failed(const QString& reason);

private:
    void next();
    void stop(const QString& reason);

    apps::GuestFileTransfer transfer_;
    GuestDriverInstaller& installer_;
    QVector<File> files_;
    QStringList uploaded_;
};

}  // namespace lsw::host::manager
