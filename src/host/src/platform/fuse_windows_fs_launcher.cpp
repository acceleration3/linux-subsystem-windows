#include "lsw/host/platform/fuse_windows_fs_launcher.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/platform/libsmb_backend.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QtConcurrent>
#include <chrono>
#include <cstring>
#include <unistd.h>

namespace lsw::host::platform {

namespace {

constexpr int kSessions = 8;
constexpr int kTimeoutMs = 8000;
constexpr auto kStopWait = std::chrono::milliseconds{6000};
constexpr auto kStaleWait = std::chrono::milliseconds{3000};

}  // namespace

FuseWindowsFsLauncher::FuseWindowsFsLauncher(QString mount_point, QObject* parent)
    : QObject(parent), mount_point_(std::move(mount_point)) {}

FuseWindowsFsLauncher::~FuseWindowsFsLauncher() {
    if (mount_) {
        mount_->stop(kStopWait);
    }
}

QString FuseWindowsFsLauncher::unavailable() const {
    if (QStandardPaths::findExecutable(QStringLiteral("fusermount3")).isEmpty()) {
        return QStringLiteral(
            "fusermount3 is not installed; install fuse3 to mount the Windows files");
    }
    if (::access("/dev/fuse", R_OK | W_OK) != 0) {
        return QStringLiteral(
            "/dev/fuse cannot be opened; load the fuse module to mount the Windows files");
    }
    return {};
}

QString FuseWindowsFsLauncher::prepare_mount_point(const QString& path) {
    const QFileInfo info{path};
    if (!info.exists()) {
        if (!QDir().mkpath(path)) {
            return QStringLiteral("%1 could not be created").arg(path);
        }
        return {};
    }
    if (!info.isDir()) {
        return QStringLiteral("%1 is not a folder").arg(path);
    }
    if (!QDir(path).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                            QDir::System)) {
        return QStringLiteral("%1 is not empty, so Windows' files are not mounted there").arg(path);
    }
    return {};
}

FuseWindowsFsLauncher::Mounted FuseWindowsFsLauncher::build(const QString& share,
                                                            const QString& user,
                                                            const QString& password,
                                                            const QString& mount_point) {
    Mounted out;
    if (const QString why = prepare_mount_point(mount_point); !why.isEmpty()) {
        out.result.error = why;
        return out;
    }
    const qsizetype slash = share.indexOf(QLatin1Char('/'));
    if (slash <= 0) {
        out.result.error = QStringLiteral("the share %1 is not host:port/name").arg(share);
        return out;
    }
    auto stack = std::make_shared<WindowsFsStack>(
        std::make_unique<LibsmbBackend>(share.left(slash).toStdString(),
                                        share.mid(slash + 1).toStdString(), user.toStdString(),
                                        password.toStdString(), kTimeoutMs),
        kSessions, ::getuid(), ::getgid());
    {
        SessionPool::Lease lease = stack->pool().acquire();
        if (!lease.ok()) {
            out.result.error = QStringLiteral("Windows' share did not answer: %1")
                                   .arg(QString::fromLocal8Bit(std::strerror(lease.error())));
            return out;
        }
    }
    FuseMount::Options options;
    options.mount_point = mount_point.toStdString();
    const QString host = share.left(slash);
    const qsizetype colon = host.indexOf(QLatin1Char(':'));
    options.fsname =
        ("//" + (colon > 0 ? host.left(colon) : host) + "/" + share.mid(slash + 1)).toStdString();
    auto mount = std::make_shared<FuseMount>(stack, options);
    std::string error;
    if (!mount->start(error)) {
        out.result.error = QString::fromStdString(error);
        return out;
    }
    out.result.ok = true;
    out.mount = std::move(mount);
    return out;
}

FuseWindowsFsLauncher::Result
FuseWindowsFsLauncher::release(const std::shared_ptr<FuseMount>& mount,
                               const QString& mount_point) {
    if (mount) {
        if (!mount->stop(kStopWait)) {
            return {false, QStringLiteral("unmounting %1 took too long").arg(mount_point)};
        }
        return {true, {}};
    }
    const std::string path = mount_point.toStdString();
    if (FuseMount::unmount_path(path, false, kStaleWait) != 0 &&
        FuseMount::unmount_path(path, true, kStaleWait) != 0) {
        return {false, QStringLiteral("fusermount3 could not unmount %1").arg(mount_point)};
    }
    return {true, {}};
}

void FuseWindowsFsLauncher::mount(const QString& share, const QString& user,
                                  const QString& password, Done done) {
    LSW_INFO("mounting {} at {}", share.toStdString(), mount_point_.toStdString());
    auto* watcher = new QFutureWatcher<Mounted>(this);
    connect(watcher, &QFutureWatcher<Mounted>::finished, this, [this, watcher, done] {
        Mounted result = watcher->result();
        watcher->deleteLater();
        if (result.result.ok) {
            mount_ = std::move(result.mount);
        }
        done(result.result);
    });
    watcher->setFuture(QtConcurrent::run([share, user, password, point = mount_point_] {
        return build(share, user, password, point);
    }));
}

void FuseWindowsFsLauncher::unmount(Done done) {
    LSW_INFO("unmounting {}", mount_point_.toStdString());
    auto* watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, done] {
        const Result result = watcher->result();
        watcher->deleteLater();
        if (result.ok) {
            mount_.reset();
        }
        done(result);
    });
    watcher->setFuture(QtConcurrent::run(
        [mount = mount_, point = mount_point_] { return release(mount, point); }));
}

FuseWindowsFsLauncher::Result FuseWindowsFsLauncher::unmount_now() {
    LSW_INFO("unmounting {}", mount_point_.toStdString());
    const Result result = release(mount_, mount_point_);
    if (result.ok) {
        mount_.reset();
    }
    return result;
}

}  // namespace lsw::host::platform
