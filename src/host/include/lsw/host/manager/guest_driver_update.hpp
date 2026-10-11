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
// with a staging step first that moves the uploads to where setup put
// them - the package to C:\av\pristine\package, provision.ps1 to C:\av,
// the OpenCL loaders to C:\av\loaders. A loader is also replaced in the
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

    // The files of ``tree`` and where each goes; empty when it holds no
    // package.
    [[nodiscard]] static QVector<File> files_of(const QString& tree);

    // The installer's staging for ``files``, uploaded to ``uploaded`` (the
    // agent's paths, in the same order).
    [[nodiscard]] static QString staging(const QVector<File>& files, const QStringList& uploaded);

    // Upload ``tree`` and install it. False, with ``failed`` emitted, when
    // it holds no package. Once the uploads are done the installer runs,
    // and its own signals say how that went.
    bool start(const QString& tree);
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
