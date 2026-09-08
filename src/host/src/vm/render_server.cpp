#include "lsw/host/vm/render_server.hpp"

#include "lsw/base/log.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QThread>

namespace lsw::host::vm {

namespace {

// The live sizing, installed by the manager from its settings before
// anything starts. Defaults match what the server would have done
// unasked, so a manager that never touches this behaves as before.
RenderServer::Sizing g_sizing{};

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
    // Separate channels rather than forwarded: the manager wants to
    // read the server's warnings, and a forwarded stream goes to the
    // terminal where nothing can. Standard output still carries
    // everything it did, because drain_output logs each line on.
    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_, &QProcess::readyReadStandardOutput, this,
            &RenderServer::drain_output);
}

void RenderServer::set_sizing(const Sizing& sizing) {
    g_sizing = sizing;
}

RenderServer::Sizing RenderServer::sizing() {
    return g_sizing;
}

int RenderServer::region_mib() {
    // The three areas plus the header, rounded up to a power of two,
    // which is what ``--vm`` does on the server's side. Both ends have
    // to agree: the domain XML declares the size QEMU decodes as a BAR.
    const int wanted = g_sizing.blob_mib + (g_sizing.ring_kib / 1024)
                       + (g_sizing.reply_kib / 1024) + 1;
    int rounded = 1;
    while (rounded < wanted) {
        rounded <<= 1;
    }
    return rounded;
}

void RenderServer::drain_output() {
    pending_ += process_.readAllStandardOutput();
    int newline = pending_.indexOf('\n');
    while (newline >= 0) {
        const QString line = QString::fromUtf8(pending_.left(newline)).trimmed();
        pending_.remove(0, newline + 1);
        if (!line.isEmpty()) {
            LSW_INFO("render server: {}", line.toStdString());
            Q_EMIT line_logged(line);
        }
        newline = pending_.indexOf('\n');
    }
}

RenderServer::~RenderServer() { stop(); }

QString RenderServer::region_name(const QUuid& uuid, int index) {
    // The server appends the session index to the name it is given.
    return region_prefix(uuid) + QStringLiteral("-%1").arg(index);
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
        QStringLiteral("--sessions"), QString::number(g_sizing.sessions),
        // Sizes the region for a guest that reaches it as an ivshmem
        // BAR: rounded to a power of two, its backing file reused
        // across restarts, and the ring polled rather than waited on —
        // a futex wake does not cross the VM boundary.
        QStringLiteral("--vm"),
        QStringLiteral("--ring-kib"), QString::number(g_sizing.ring_kib),
        QStringLiteral("--reply-kib"), QString::number(g_sizing.reply_kib),
        QStringLiteral("--blob-mib"), QString::number(g_sizing.blob_mib),
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
            LSW_INFO("render server: serving {} ({} MiB)", region.toStdString(), region_mib());
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
