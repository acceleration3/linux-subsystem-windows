#include "lsw/host/manager/guest_driver_update.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/manager/guest_driver_installer.hpp"

#include <QDir>
#include <QFileInfo>

namespace lsw::host::manager {

namespace {

// Exit code of the staging when a file could not be put in place.
constexpr int kStagingFailed = 4;

// The Windows directory a loader architecture goes to: a 32-bit process
// looks in SysWOW64.
QString system_dir(const QString& arch) {
    return arch == QLatin1String("x86") ? QStringLiteral("SysWOW64") : QStringLiteral("System32");
}

QString quoted(const QString& path) {
    return QLatin1Char('"') + path + QLatin1Char('"');
}

}  // namespace

GuestDriverUpdate::GuestDriverUpdate(link::AgentRpcClient& rpc, GuestDriverInstaller& installer,
                                     QObject* parent)
    : QObject(parent), transfer_(rpc), installer_(installer) {
    connect(&transfer_, &apps::GuestFileTransfer::transferred, this, [this](const QString& path) {
        uploaded_.push_back(path);
        next();
    });
    connect(&transfer_, &apps::GuestFileTransfer::failed, this, [this](const QString& error) {
        stop(QStringLiteral("uploading %1: %2")
                 .arg(QFileInfo(files_.value(uploaded_.size()).local).fileName(), error));
    });
    connect(&transfer_, &apps::GuestFileTransfer::cancelled, this,
            [this] { stop(QStringLiteral("the upload was cancelled")); });
}

QVector<GuestDriverUpdate::File> GuestDriverUpdate::files_of(const QString& tree,
                                                            const QString& vulkan_x64,
                                                            const QString& vulkan_x86) {
    const QDir root{tree};
    const QDir package{root.filePath(QStringLiteral("package"))};
    if (!package.exists(QStringLiteral("package.sha256"))) {
        return {};
    }
    QVector<File> out;
    for (const QFileInfo& file : package.entryInfoList(QDir::Files, QDir::Name)) {
        out.push_back({file.filePath(),
                       QStringLiteral("C:\\av\\incoming\\package\\") + file.fileName()});
    }
    if (root.exists(QStringLiteral("provision.ps1"))) {
        out.push_back({root.filePath(QStringLiteral("provision.ps1")),
                       QStringLiteral("C:\\av\\provision.ps1")});
    }
    static const QString kVulkan = QStringLiteral("vulkan-1.dll");
    for (const QString& arch : {QStringLiteral("x64"), QStringLiteral("x86")}) {
        const QString guest = QStringLiteral("C:\\av\\loaders\\%1\\").arg(arch);
        const QString& vulkan = arch == QLatin1String("x64") ? vulkan_x64 : vulkan_x86;
        const QDir loaders{root.filePath(QStringLiteral("loaders/") + arch)};
        for (const QFileInfo& file : loaders.entryInfoList(QDir::Files, QDir::Name)) {
            // Ours goes in its place, as setup put it: the one this host
            // built and tested against.
            if (!vulkan.isEmpty() && file.fileName().compare(kVulkan, Qt::CaseInsensitive) == 0) {
                continue;
            }
            out.push_back({file.filePath(), guest + file.fileName()});
        }
        if (!vulkan.isEmpty() && QFileInfo::exists(vulkan)) {
            out.push_back({vulkan, guest + kVulkan});
        }
    }
    return out;
}

QString GuestDriverUpdate::manifest_name(const File& file) {
    static const QString kRoot = QStringLiteral("C:\\av\\");
    static const QString kPackage = QStringLiteral("C:\\av\\incoming\\package\\");
    if (file.guest.startsWith(kPackage) || !file.guest.startsWith(kRoot)) {
        return {};
    }
    return file.guest.mid(kRoot.size()).replace(QLatin1Char('\\'), QLatin1Char('/'));
}

QString GuestDriverUpdate::staging(const QVector<File>& files, const QStringList& uploaded) {
    const QString fail = QStringLiteral(" || exit /b %1\r\n").arg(kStagingFailed);
    QString out = QStringLiteral(
        "echo Putting the new driver package in place\r\n"
        "if exist C:\\av\\incoming rmdir /s /q C:\\av\\incoming\r\n"
        "mkdir C:\\av\\incoming\\package") + fail;
    for (qsizetype i = 0; i < files.size() && i < uploaded.size(); ++i) {
        const QString& to = files[i].guest;
        const QString& from = uploaded[i];
        const QString dir = to.left(to.lastIndexOf(QLatin1Char('\\')));
        out += QStringLiteral("if not exist %1 mkdir %1").arg(quoted(dir)) + fail;
        static const QString kLoaders = QStringLiteral("C:\\av\\loaders\\");
        if (to.startsWith(kLoaders)) {
            // The Windows directory's copy is ours when it is the one setup
            // put there, which is the one in C:\av\loaders until this.
            const QString arch = to.mid(kLoaders.size()).section(QLatin1Char('\\'), 0, 0);
            const QString installed = QStringLiteral("C:\\Windows\\%1\\%2")
                                          .arg(system_dir(arch), to.section(QLatin1Char('\\'), -1));
            out += QStringLiteral("set OURS=0\r\n"
                                  "fc /b %1 %2 >nul 2>&1 && set OURS=1\r\n")
                       .arg(quoted(installed), quoted(to));
            out += QStringLiteral("copy /y %1 %2 >nul").arg(quoted(from), quoted(to)) + fail;
            out += QStringLiteral("if \"%OURS%\"==\"1\" copy /y %1 %2 >nul\r\n")
                       .arg(quoted(to), quoted(installed));
        } else {
            out += QStringLiteral("copy /y %1 %2 >nul").arg(quoted(from), quoted(to)) + fail;
        }
        const QString upload_dir = from.left(from.lastIndexOf(QLatin1Char('\\')));
        out += QStringLiteral("rmdir /s /q %1\r\n").arg(quoted(upload_dir));
    }
    return out + QStringLiteral("set SOURCE=C:\\av\\incoming\r\n");
}

bool GuestDriverUpdate::start(const QVector<File>& files) {
    if (uploading() || installer_.running()) {
        return true;
    }
    files_ = files;
    uploaded_.clear();
    if (files_.isEmpty()) {
        Q_EMIT failed(QStringLiteral("LSW has no driver package to install"));
        return false;
    }
    LSW_INFO("graphics: updating avgpu, {} files", files_.size());
    next();
    return true;
}

void GuestDriverUpdate::next() {
    if (uploaded_.size() < files_.size()) {
        const File& file = files_[uploaded_.size()];
        Q_EMIT progress(QStringLiteral("Uploading %1 (%2 of %3)")
                            .arg(QFileInfo(file.local).fileName())
                            .arg(uploaded_.size() + 1)
                            .arg(files_.size()));
        transfer_.start(file.local);
        return;
    }
    const QString script = staging(files_, uploaded_);
    files_.clear();
    uploaded_.clear();
    (void)installer_.start(script);
}

void GuestDriverUpdate::abandon(const QString& reason) {
    if (!uploading()) {
        return;
    }
    transfer_.cancel();
    stop(reason);
}

void GuestDriverUpdate::stop(const QString& reason) {
    if (!uploading()) {
        return;
    }
    files_.clear();
    uploaded_.clear();
    LSW_WARN("graphics: driver update stopped: {}", reason.toStdString());
    Q_EMIT failed(reason);
}

}  // namespace lsw::host::manager
