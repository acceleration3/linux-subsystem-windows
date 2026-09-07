#include "lsw/host/vm/render_server.hpp"

#include "lsw/base/log.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QThread>

namespace lsw::host::vm {

namespace {

// One session, because one ivshmem device is one region is one session
// and the domain declares one device. A second concurrent application
// in the guest reports "no free session"; DXVK surfaces that as "Failed
// to create Vulkan instance" with its extension list perfectly intact,
// which is worth knowing before it is seen.
constexpr int kSessions = 1;

// The command ring and the reply slot. Small and fixed: they are sized
// by what a single round trip needs, not by what an application
// allocates.
constexpr int kRingKib = 1024;
constexpr int kReplyKib = 8192;

// What an application's own allocations are suballocated from — the
// buffers, images and staging memory it maps, and the frame slots it
// carves to present through.
//
// This is a floor, not the final size. ``--vm`` rounds the whole region
// up to the next power of two and gives every byte of the rounding to
// the blob area, so the numbers above plus this land on a 1 GiB region
// whose blob area is ~1014 MiB — very nearly the whole of it, which is
// what it should be.
//
// Fixed for now. It wants to be a per-VM setting alongside RAM and
// disk, in the same place the wizard asks for those.
constexpr int kBlobMib = 512;

// How long to wait for the server to create its region before starting
// the domain without it. Generous relative to what it takes (the server
// creates the file before it serves anything), because the cost of
// being wrong is a guest that boots with no GPU path at all.
constexpr int kRegionWaitMs = 10000;
constexpr int kRegionPollMs = 50;

[[nodiscard]] QString region_prefix(const QUuid& uuid) {
    // Per VM, so two domains never share a region — and short, because
    // this ends up as a file name and as an XML attribute.
    return QStringLiteral("lsw-rvk-%1")
        .arg(uuid.toString(QUuid::WithoutBraces).left(8));
}

}  // namespace

RenderServer::RenderServer(QObject* parent) : QObject(parent) {
    process_.setProcessChannelMode(QProcess::ForwardedChannels);
}

RenderServer::~RenderServer() { stop(); }

QString RenderServer::region_name(const QUuid& uuid) {
    // The server appends the session index to the name it is given, and
    // one device carries one region, so session 0 is the only device.
    return region_prefix(uuid) + QStringLiteral("-0");
}

QString RenderServer::executable_path() {
    QString from_env = qEnvironmentVariable("LSW_RENDER_SERVER");
    if (!from_env.isEmpty() && QFile::exists(from_env)) {
        return from_env;
    }
    const QString candidate = QDir::cleanPath(
        QCoreApplication::applicationDirPath()
        + QStringLiteral("/../../render-server/src/server/rvk-render-server"));
    return QFile::exists(candidate) ? candidate : QString{};
}

bool RenderServer::running() const {
    return process_.state() != QProcess::NotRunning;
}

bool RenderServer::ensure_running(const QUuid& uuid) {
    if (running() && serving_ == uuid) {
        return true;
    }
    stop();

    const QString exe = executable_path();
    if (exe.isEmpty()) {
        LSW_WARN("render server: no rvk-render-server binary found; guest applications "
                 "will report no GPU driver. Build it, or set LSW_RENDER_SERVER");
        return false;
    }

    const QStringList args = {
        QStringLiteral("--name"), QStringLiteral("/") + region_prefix(uuid),
        QStringLiteral("--sessions"), QString::number(kSessions),
        // Sizes the region for a guest that reaches it as an ivshmem
        // BAR: rounded to a power of two, its backing file reused
        // across restarts, and the ring polled rather than waited on —
        // a futex wake does not cross the VM boundary.
        QStringLiteral("--vm"),
        QStringLiteral("--ring-kib"), QString::number(kRingKib),
        QStringLiteral("--reply-kib"), QString::number(kReplyKib),
        QStringLiteral("--blob-mib"), QString::number(kBlobMib),
    };
    LSW_INFO("render server: {} {}", exe.toStdString(),
             args.join(QLatin1Char(' ')).toStdString());
    process_.start(exe, args);
    if (!process_.waitForStarted(kRegionWaitMs)) {
        LSW_ERROR("render server: {} did not start: {}", exe.toStdString(),
                  process_.errorString().toStdString());
        return false;
    }
    serving_ = uuid;

    // Wait for the region, not for the process: QEMU opens the file by
    // path, so "started" is not the state the domain needs — "the file
    // exists" is.
    const QString region = QStringLiteral("/dev/shm/") + region_name(uuid);
    QDeadlineTimer deadline(kRegionWaitMs);
    while (!deadline.hasExpired()) {
        if (QFile::exists(region)) {
            LSW_INFO("render server: serving {} ({} MiB)", region.toStdString(), kRegionMib);
            return true;
        }
        if (!running()) {
            LSW_ERROR("render server: exited with code {} before creating {}",
                      process_.exitCode(), region.toStdString());
            return false;
        }
        QThread::msleep(kRegionPollMs);
    }
    LSW_ERROR("render server: {} never appeared; starting the domain without it",
              region.toStdString());
    return false;
}

void RenderServer::stop() {
    if (!running()) {
        serving_ = QUuid();
        return;
    }
    process_.terminate();
    if (!process_.waitForFinished(3000)) {
        process_.kill();
        (void)process_.waitForFinished(1000);
    }
    serving_ = QUuid();
}

}  // namespace lsw::host::vm
