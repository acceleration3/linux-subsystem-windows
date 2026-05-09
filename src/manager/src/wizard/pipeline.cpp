#include "lsw/manager/wizard/pipeline.hpp"

#include "lsw/log.hpp"
#include "lsw/manager/app.hpp"
#include "lsw/manager/install/autounattend.hpp"
#include "lsw/manager/install/payload_iso.hpp"
#include "lsw/manager/iso/catalog.hpp"
#include "lsw/manager/install/post_install.hpp"
#include "lsw/manager/vm/agent_rpc_client.hpp"
#include "lsw/manager/vm/domain_xml.hpp"
#include "lsw/manager/vm/lifecycle.hpp"
#include "setup_steps_template.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QtConcurrent>
#include <libvirt/libvirt.h>

#include <chrono>
#include <thread>

#include <nlohmann/json.hpp>

namespace lsw::manager::wizard {

namespace {

constexpr quint32 kAgentPort = 5102;
constexpr int kVncRetryMaxAttempts = 80;  // ~20s @ 250ms
constexpr int kPressAnyKeyTicks = 16;     // ~4s @ 250ms

// Pull the live VNC graphics port out of the running domain's XML.
// Returns -1 if the domain isn't up yet or hasn't been assigned one.
int find_vnc_port(virConnectPtr conn, const QUuid& uuid) {
    if (conn == nullptr) {
        return -1;
    }
    const auto uuid_bytes = uuid.toRfc4122();
    auto* d = ::virDomainLookupByUUID(
        conn, reinterpret_cast<const unsigned char*>(uuid_bytes.constData()));
    if (d == nullptr) {
        return -1;
    }
    char* xml = ::virDomainGetXMLDesc(d, 0);
    ::virDomainFree(d);
    if (xml == nullptr) {
        return -1;
    }
    const QString text = QString::fromUtf8(xml);
    ::free(xml);

    static const QRegularExpression re(
        QStringLiteral("<graphics type='vnc'[^>]*port='(-?\\d+)'"));
    const auto m = re.match(text);
    if (!m.hasMatch()) {
        return -1;
    }
    bool ok = false;
    const int port = m.captured(1).toInt(&ok);
    if (!ok || port <= 0) {
        return -1;
    }
    return port;
}

[[nodiscard]] QString format_bytes(quint64 n) {
    if (n < 1024) {
        return QStringLiteral("%1 B").arg(n);
    }
    const double dn = static_cast<double>(n);
    if (n < 1024ULL * 1024ULL) {
        return QStringLiteral("%1 KiB").arg(dn / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 MiB").arg(dn / (1024.0 * 1024.0), 0, 'f', 1);
}

// Resolve the cross-built guest-agent .exe. Priority:
//   1. $LSW_AGENT_EXE env var (CI, developers)
//   2. <manager-dir>/../../../build-agent/src/agent/lsw-guest-agent.exe
// Returns empty QString if neither exists.
[[nodiscard]] QString resolve_agent_exe_path() {
    QString agent = qEnvironmentVariable("LSW_AGENT_EXE");
    if (!agent.isEmpty() && QFile::exists(agent)) {
        return agent;
    }
    const QString candidate =
        QDir::cleanPath(QCoreApplication::applicationDirPath()
                        + "/../../../build-agent/src/agent/lsw-guest-agent.exe");
    if (QFile::exists(candidate)) {
        return candidate;
    }
    return {};
}

}  // namespace

Pipeline::Pipeline(App& app, QObject* parent) : QObject(parent), app_(app) {
    press_any_key_timer_ = new QTimer(this);
    press_any_key_timer_->setInterval(250);
    connect(press_any_key_timer_, &QTimer::timeout, this, &Pipeline::on_press_any_key_tick);

    vnc_retry_timer_ = new QTimer(this);
    vnc_retry_timer_->setInterval(250);
    connect(vnc_retry_timer_, &QTimer::timeout, this, &Pipeline::on_vnc_retry_tick);
}

Pipeline::~Pipeline() {
    // ``detach_rpc_handlers`` should already have run from the terminal
    // state hook; calling it again is a no-op. We deliberately do NOT
    // touch ``rpc_`` from here unconditionally: ``App`` holds the rpc
    // as a value member, so during the App destructor its lifetime can
    // end before this child QObject is torn down. Hitting a destroyed
    // ``rpc_`` from here is exactly the SIGSEGV-at-shutdown we saw.
    detach_rpc_handlers();
}

void Pipeline::detach_rpc_handlers() {
    if (rpc_ == nullptr) return;
    if (setup_progress_token_ != 0) {
        rpc_->off_event(setup_progress_token_);
        setup_progress_token_ = 0;
    }
    if (setup_done_token_ != 0) {
        rpc_->off_event(setup_done_token_);
        setup_done_token_ = 0;
    }
    rpc_ = nullptr;
}

void Pipeline::emit_failed(Step step, const QString& error) {
    detach_rpc_handlers();
    Q_EMIT failed(step, error);
}

void Pipeline::emit_completed(const QUuid& uuid) {
    detach_rpc_handlers();
    Q_EMIT completed(uuid);
}

void Pipeline::set_inputs(Inputs in) {
    inputs_ = std::move(in);
}

void Pipeline::start() {
    if (started_) {
        return;
    }
    started_ = true;
    // Defer one event-loop turn so the first wizard page (Download or
    // Payload) has been shown and its signal connections are live
    // before any step_* / payload_step_* is emitted — otherwise the
    // page misses the opening signals and renders blank (same class of
    // race the setup phase had).
    QTimer::singleShot(0, this, [this] {
        if (!inputs_.iso_local_path.isEmpty()) {
            run_build_payload();   // Windows ISO already local
        } else {
            run_download();
        }
    });
}

void Pipeline::cancel() {
    if (cancelled_) {
        return;
    }
    cancelled_ = true;
    ++generation_;
    if (press_any_key_timer_ != nullptr) press_any_key_timer_->stop();
    if (vnc_retry_timer_ != nullptr) vnc_retry_timer_->stop();
    if (watcher_ != nullptr) watcher_->stop();
    if (downloader_ != nullptr) downloader_->cancel();
}

void Pipeline::emit_status(Step step, const QString& detail) {
    Q_EMIT step_progress(step, std::numeric_limits<double>::quiet_NaN(), detail);
}

// ----- Download phase -----------------------------------------------------

void Pipeline::run_download() {
    Q_EMIT step_started(Step::DownloadingIso,
        QStringLiteral("Downloading %1...").arg(inputs_.iso_display_label.isEmpty()
                                                     ? QStringLiteral("Windows")
                                                     : inputs_.iso_display_label));
    if (inputs_.iso_download_url.isEmpty()) {
        emit_failed(Step::DownloadingIso,
                      QStringLiteral("no download URL supplied and no local ISO provided"));
        return;
    }
    const QString dest = QDir(app_.settings().isos_dir()).filePath("windows.iso");
    inputs_.iso_local_path = dest;  // tentative — confirmed on Downloader::finished

    downloader_ = new iso::Downloader(this);
    connect(downloader_, &iso::Downloader::progress, this,
            [this](qint64 received, qint64 total) {
                const double frac = total > 0 ? static_cast<double>(received) / total : -1.0;
                Q_EMIT step_progress(Step::DownloadingIso, frac,
                    QStringLiteral("Downloaded %1 MiB%2")
                        .arg(received / (1024 * 1024))
                        .arg(total > 0 ? QStringLiteral(" / %1 MiB").arg(total / (1024 * 1024))
                                       : QString{}));
            });
    connect(downloader_, &iso::Downloader::finished, this,
            [this](const QString& path) {
                inputs_.iso_local_path = path;
                Q_EMIT step_finished(Step::DownloadingIso);
                run_build_payload();
            });
    connect(downloader_, &iso::Downloader::failed, this,
            [this](const QString& msg) {
                emit_failed(Step::DownloadingIso, msg);
            });
    downloader_->start(inputs_.iso_download_url, dest);
}

// ----- Build payload ISO --------------------------------------------------

void Pipeline::run_build_payload() {
    Q_EMIT step_started(Step::BuildingPayloadIso,
                        QStringLiteral("Preparing the install payload..."));

    // First half of this stage: make sure we have a virtio-win.iso
    // (latest fedorapeople stable). Its drivers get injected into
    // Setup so Windows installs straight onto a virtio disk. Reuse a
    // caller-supplied or previously-cached copy; otherwise download it
    // here, reporting progress on this same BuildingPayloadIso step.
    // ``looks_like_iso``: size sane AND ISO9660 PVD magic ("CD001" at
    // byte 0x8001). fedorapeople is behind an Anubis anti-bot wall
    // that serves a ~4 KB HTML challenge page to QNetworkAccessManager
    // (which we previously saved AS virtio-win.iso → Windows Setup saw
    // no disks because there was no real viostor driver). Plain
    // ``curl`` with its default UA passes the wall; we still verify
    // the bytes so a bad fetch fails LOUDLY instead of silently
    // bundling garbage.
    const auto looks_like_iso = [](const QString& p) {
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly)) return false;
        if (f.size() < 100LL * 1024 * 1024) return false;  // real one is ~750 MiB
        if (!f.seek(0x8001)) return false;
        return f.read(5) == QByteArray("CD001");
    };

    // Row 0 of the PayloadPage table = "Download virtio-win.iso".
    Q_EMIT payload_step_started(0);

    if (!inputs_.virtio_iso_path.isEmpty() && looks_like_iso(inputs_.virtio_iso_path)) {
        LSW_INFO("pipeline: using virtio-win.iso at {}",
                 inputs_.virtio_iso_path.toStdString());
        Q_EMIT payload_step_finished(0, true, QStringLiteral("using supplied ISO"));
        build_payload_iso();
        return;
    }
    const QString virtio_dest =
        QDir(app_.settings().isos_dir()).filePath("virtio-win.iso");
    if (looks_like_iso(virtio_dest)) {
        inputs_.virtio_iso_path = virtio_dest;
        LSW_INFO("pipeline: reusing cached virtio-win.iso at {}",
                 virtio_dest.toStdString());
        Q_EMIT payload_step_finished(0, true, QStringLiteral("cached"));
        build_payload_iso();
        return;
    }
    QFile::remove(virtio_dest);  // drop any stale Anubis-HTML masquerade
    inputs_.virtio_iso_path = virtio_dest;  // tentative — confirmed below

    const QString url = iso::IsoCatalog::virtio_win_url();

    // Get the final Content-Length first (HEAD, follows the 301 chain)
    // so the PayloadPage row can show a real percentage. If it can't
    // be determined we fall back to a MiB counter.
    auto* head = new QProcess(this);
    head->setProgram(QStringLiteral("curl"));
    head->setArguments({QStringLiteral("-sIL"), url});
    connect(head, &QProcess::finished, this,
            [this, head, url, virtio_dest, looks_like_iso](int, QProcess::ExitStatus) {
        qint64 total = 0;
        const auto hdrs = QString::fromLatin1(head->readAllStandardOutput());
        head->deleteLater();
        for (const auto& line : hdrs.split('\n')) {
            const auto t = line.trimmed();
            if (t.startsWith(QStringLiteral("content-length:"), Qt::CaseInsensitive)) {
                total = t.mid(t.indexOf(':') + 1).trimmed().toLongLong();  // last wins
            }
        }

        auto* curl = new QProcess(this);
        // -f: HTTP errors → non-zero exit (don't save error bodies).
        // -S: show errors. -L: follow the fedorapeople 301 chain. NO
        // custom User-Agent: curl's default UA passes Anubis (a
        // browser-looking UA gets the challenge page instead).
        curl->setProgram(QStringLiteral("curl"));
        curl->setArguments({QStringLiteral("-fSL"), QStringLiteral("--retry"),
                            QStringLiteral("3"), QStringLiteral("--retry-delay"),
                            QStringLiteral("3"), QStringLiteral("-o"), virtio_dest, url});

        auto* poll = new QTimer(this);
        poll->setInterval(1000);
        connect(poll, &QTimer::timeout, this, [this, virtio_dest, total] {
            const qint64 got = QFileInfo(virtio_dest).size();
            if (total > 0) {
                const double frac = static_cast<double>(got) / static_cast<double>(total);
                const int pct = static_cast<int>(frac * 100.0);
                const QString d = QStringLiteral("%1%  (%2 / %3 MiB)")
                                      .arg(pct)
                                      .arg(got / (1024 * 1024))
                                      .arg(total / (1024 * 1024));
                Q_EMIT payload_step_progress(0, d);
                Q_EMIT step_progress(Step::BuildingPayloadIso, frac, d);
            } else {
                const QString d = QStringLiteral("%1 MiB").arg(got / (1024 * 1024));
                Q_EMIT payload_step_progress(0, d);
                Q_EMIT step_progress(Step::BuildingPayloadIso, -1.0,
                    QStringLiteral("Downloading virtio-win drivers: %1").arg(d));
            }
        });
        poll->start();

        connect(curl, &QProcess::finished, this,
                [this, curl, poll, virtio_dest, looks_like_iso](int code, QProcess::ExitStatus) {
            poll->stop();
            poll->deleteLater();
            curl->deleteLater();
            if (code != 0) {
                const auto m = QStringLiteral("download failed (curl exit %1)").arg(code);
                Q_EMIT payload_step_finished(0, false, m);
                emit_failed(Step::BuildingPayloadIso,
                            QStringLiteral("virtio-win.iso %1").arg(m));
                return;
            }
            if (!looks_like_iso(virtio_dest)) {
                const auto m = QStringLiteral("not an ISO (anti-bot wall / bad mirror?)");
                Q_EMIT payload_step_finished(0, false, m);
                emit_failed(Step::BuildingPayloadIso,
                            QStringLiteral("virtio-win.iso download produced a non-ISO "
                                           "file — refusing to bundle it"));
                return;
            }
            inputs_.virtio_iso_path = virtio_dest;
            LSW_INFO("pipeline: virtio-win.iso downloaded + verified at {}",
                     virtio_dest.toStdString());
            Q_EMIT payload_step_finished(0, true, QStringLiteral("done"));
            build_payload_iso();
        });
        connect(curl, &QProcess::errorOccurred, this,
                [this, curl, poll](QProcess::ProcessError) {
            poll->stop();
            poll->deleteLater();
            const QString err = curl->errorString();
            curl->deleteLater();
            Q_EMIT payload_step_finished(0, false, err);
            emit_failed(Step::BuildingPayloadIso,
                        QStringLiteral("could not run curl for virtio-win.iso: %1").arg(err));
        });
        curl->start();
    });
    connect(head, &QProcess::errorOccurred, this, [this, head](QProcess::ProcessError) {
        const QString err = head->errorString();
        head->deleteLater();
        Q_EMIT payload_step_finished(0, false, err);
        emit_failed(Step::BuildingPayloadIso,
                    QStringLiteral("could not run curl (HEAD) for virtio-win.iso: %1").arg(err));
    });
    head->start();
}

void Pipeline::build_payload_iso() {
    // Row 1 of the PayloadPage table = "Build LSW payload ISO".
    Q_EMIT payload_step_started(1);
    Q_EMIT step_progress(Step::BuildingPayloadIso, -1.0,
                         QStringLiteral("Building the LSW payload ISO..."));

    // Initialize the VM record. Identifiers + paths come from the
    // single-VM constants in config::kVmName.
    rec_ = config::VmRecord{};
    rec_.uuid = QUuid::createUuid();
    rec_.name = config::kVmName;
    rec_.iso_path = inputs_.iso_local_path;
    rec_.sizing = inputs_.sizing;
    const QString fixed = QString::fromLatin1(config::kVmName);
    rec_.disk_path = QDir(app_.settings().disks_dir()).filePath(QStringLiteral("%1.qcow2").arg(fixed));
    rec_.nvram_path = QDir(app_.settings().nvram_dir()).filePath(QStringLiteral("%1.fd").arg(fixed));

    install::AutounattendInputs au;
    au.computer_name = inputs_.computer_name.isEmpty() ? rec_.name : inputs_.computer_name;
    au.user_name = inputs_.user_name;
    au.user_password = inputs_.user_password;
    au.image_index = inputs_.image_index;

    install::PayloadInputs pi;
    pi.output_iso_path =
        QDir(app_.settings().isos_dir()).filePath(QStringLiteral("%1-payload.iso").arg(fixed));
    pi.autounattend_xml = install::Autounattend::render(au);
    const QString agent_exe = resolve_agent_exe_path();
    if (!agent_exe.isEmpty()) {
        pi.agent_exe_path = agent_exe;
        LSW_INFO("pipeline: bundling guest agent from {}", agent_exe.toStdString());
    } else {
        LSW_WARN("pipeline: no guest agent found; install will hang waiting for vsock. "
                 "Set LSW_AGENT_EXE or run scripts/build-agent.sh");
    }

    const auto gen = generation_;
    const QPointer<Pipeline> self{this};
    (void)QtConcurrent::run([self, pi, gen]() {
        const int rc = install::PayloadIsoBuilder::build(pi);
        QMetaObject::invokeMethod(self.data(), [self, rc, pi, gen]() {
            if (!self || self->generation_ != gen) return;
            if (rc != 0) {
                Q_EMIT self->payload_step_finished(1, false,
                                                   QStringLiteral("build failed"));
                self->emit_failed(Step::BuildingPayloadIso,
                                    QStringLiteral("payload ISO build failed"));
                return;
            }
            self->rec_.payload_iso_path = pi.output_iso_path;
            Q_EMIT self->payload_step_finished(1, true, QStringLiteral("done"));
            Q_EMIT self->step_finished(Step::BuildingPayloadIso);
            self->run_create_disk();
        }, Qt::QueuedConnection);
    });
}

// ----- Create qcow2 disk --------------------------------------------------

void Pipeline::run_create_disk() {
    Q_EMIT step_started(Step::CreatingDisk,
        QStringLiteral("Allocating the qcow2 system disk (%1 GiB)...")
            .arg(rec_.sizing.disk_gib));

    const QString disk_path = rec_.disk_path;
    const int size_gib = rec_.sizing.disk_gib;
    auto* lifecycle = &app_.lifecycle();
    const auto gen = generation_;
    const QPointer<Pipeline> self{this};
    (void)QtConcurrent::run([self, lifecycle, disk_path, size_gib, gen]() {
        const int rc = lifecycle->create_disk(disk_path, size_gib);
        QMetaObject::invokeMethod(self.data(), [self, rc, gen]() {
            if (!self || self->generation_ != gen) return;
            if (rc != 0) {
                self->emit_failed(Step::CreatingDisk,
                                    QStringLiteral("qemu-img create failed"));
                return;
            }
            Q_EMIT self->step_finished(Step::CreatingDisk);
            self->run_start_vm();
        }, Qt::QueuedConnection);
    });
}

// ----- Define + start VM --------------------------------------------------

void Pipeline::run_start_vm() {
    Q_EMIT step_started(Step::StartingVm,
                        QStringLiteral("Defining the libvirt domain and starting the VM..."));

    // Persist the record on the App-level VmState — the manager UI and
    // the post-install watcher both look it up here.
    rec_ = *app_.vms().find(app_.vms().add(rec_));
    vm::DomainXmlInputs xin;
    xin.virtio_iso = inputs_.virtio_iso_path;
    if (app_.lifecycle().define_and_start(rec_, xin) != 0) {
        emit_failed(Step::StartingVm,
                      QStringLiteral("libvirt rejected the domain — see manager logs"));
        return;
    }
    Q_EMIT step_finished(Step::StartingVm);

    // Kick the press-any-key spammer + VNC port resolver. Both run in
    // parallel with the agent-reachability wait.
    press_any_key_ticks_remaining_ = kPressAnyKeyTicks;
    press_any_key_timer_->start();
    vnc_retry_attempts_ = 0;
    vnc_port_ = -1;
    vnc_retry_timer_->start();

    run_wait_for_agent();
}

// ----- Wait for guest agent -----------------------------------------------

void Pipeline::run_wait_for_agent() {
    Q_EMIT step_started(Step::WaitingForAgent,
        QStringLiteral("Waiting for the guest agent to come online over vsock "
                       "(Windows install typically takes 10–30 minutes)..."));

    watcher_ = new install::PostInstallWatcher(rec_.uuid, rec_.vsock_cid, kAgentPort, this);
    connect(watcher_, &install::PostInstallWatcher::ready, this,
            [this](const QUuid&) {
                Q_EMIT step_finished(Step::WaitingForAgent);
                // step_finished() synchronously makes InstallPage
                // schedule wizard()->next() on a 0ms timer. We MUST
                // let that run (so SetupPage is shown and its signal
                // connections are live) before run_setup_phase() emits
                // setup_plan_ready — otherwise the plan signal fires
                // into a SetupPage that doesn't exist yet and the
                // table never populates. Deferring here queues us
                // strictly after InstallPage's already-scheduled
                // next() timer.
                QTimer::singleShot(0, this, [this] { run_setup_phase(); });
            });
    watcher_->start();
}

void Pipeline::on_press_any_key_tick() {
    if (--press_any_key_ticks_remaining_ <= 0) {
        press_any_key_timer_->stop();
        return;
    }
    if (rec_.uuid.isNull() || !app_.libvirt().is_open()) {
        return;
    }
    auto* conn = app_.libvirt().raw();
    const auto uuid_bytes = rec_.uuid.toRfc4122();
    (void)QtConcurrent::run([conn, uuid_bytes]() {
        auto* d = ::virDomainLookupByUUID(
            conn, reinterpret_cast<const unsigned char*>(uuid_bytes.constData()));
        if (d == nullptr) return;
        vm::DomainHandle handle{d};
        constexpr unsigned int kKeySpace = 57;  // KEY_SPACE in linux/input-event-codes.h
        unsigned int keycodes[1] = {kKeySpace};
        (void)::virDomainSendKey(d, VIR_KEYCODE_SET_LINUX, /*holdtime=*/100, keycodes,
                                 /*nkeycodes=*/1, /*flags=*/0);
    });
}

void Pipeline::on_vnc_retry_tick() {
    if (rec_.uuid.isNull() || !app_.libvirt().is_open()) {
        vnc_retry_timer_->stop();
        return;
    }
    if (++vnc_retry_attempts_ > kVncRetryMaxAttempts) {
        vnc_retry_timer_->stop();
        LSW_WARN("pipeline: vnc port never appeared on domain {}",
                 rec_.uuid.toString().toStdString());
        return;
    }
    const int port = find_vnc_port(app_.libvirt().raw(), rec_.uuid);
    if (port <= 0) {
        return;
    }
    vnc_retry_timer_->stop();
    vnc_port_ = port;
    LSW_INFO("pipeline: vnc port resolved: 127.0.0.1:{}", port);
    Q_EMIT vnc_port_available(port);
}

// ----- Setup phase --------------------------------------------------------

void Pipeline::run_setup_phase() {
    Q_EMIT step_started(Step::RunningSetup,
                        QStringLiteral("Running post-install setup steps..."));

    QString err;
    const QByteArray bytes = QByteArray::fromRawData(
        lsw::embedded::kSetupStepsTemplate,
        static_cast<int>(sizeof(lsw::embedded::kSetupStepsTemplate) - 1));
    stages_ = parse_setup_plan(bytes, &err);
    Q_EMIT setup_plan_ready(stages_);
    if (stages_.isEmpty()) {
        LSW_WARN("pipeline: no setup steps configured ({})", err.toStdString());
        Q_EMIT step_finished(Step::RunningSetup);
        run_post_setup_swap();
        return;
    }

    // Bind to the App-level shared rpc — same channel the manager UI
    // uses, so we don't race a second connection.
    rpc_ = &app_.rpc();
    setup_progress_token_ = rpc_->on_event(
        QStringLiteral("setup_progress"), [this](const nlohmann::json& f) {
            const auto job = QString::fromStdString(f.value("job_id", std::string{}));
            const auto msg = QString::fromStdString(f.value("message", std::string{}));
            const auto b = f.value("bytes", std::uint64_t{0});
            const auto tot = f.value("total_bytes", std::uint64_t{0});
            QMetaObject::invokeMethod(this, [this, job, msg, b, tot]() {
                const auto it = job_to_row_.constFind(job);
                if (it == job_to_row_.constEnd()) return;
                if (!msg.isEmpty()) {
                    LSW_INFO("pipeline: setup_progress job={} row={} '{}'",
                             job.toStdString(), it.value(), msg.toStdString());
                }
                QString status;
                if (!msg.isEmpty()) {
                    status = msg;
                } else if (tot > 0) {
                    const int pct = static_cast<int>((b * 100) / tot);
                    status = QStringLiteral("%1% (%2 / %3)")
                                  .arg(pct).arg(format_bytes(b)).arg(format_bytes(tot));
                } else if (b > 0) {
                    status = format_bytes(b);
                } else {
                    status = QStringLiteral("Running...");
                }
                Q_EMIT setup_step_progress(it.value(), status);
            }, Qt::QueuedConnection);
        });
    setup_done_token_ = rpc_->on_event(
        QStringLiteral("setup_done"), [this](const nlohmann::json& f) {
            const auto job = QString::fromStdString(f.value("job_id", std::string{}));
            const auto ok = f.value("ok", false);
            const auto code = f.value("exit_code", 0);
            const auto msg = QString::fromStdString(f.value("message", std::string{}));
            QMetaObject::invokeMethod(this, [this, job, ok, code, msg]() {
                LSW_INFO("pipeline: setup_done job={} ok={} exit={} msg='{}'",
                         job.toStdString(), ok, code, msg.toStdString());
                const auto it = job_to_row_.constFind(job);
                if (it == job_to_row_.constEnd()) {
                    LSW_WARN("pipeline: setup_done for unknown job={} (already "
                             "handled or never dispatched)", job.toStdString());
                    return;
                }
                const int row = it.value();
                const QString detail = ok
                    ? (code == 0 ? QStringLiteral("Done")
                                 : QStringLiteral("Done (exit %1)").arg(code))
                    : (msg.isEmpty() ? QStringLiteral("exit %1").arg(code) : msg);
                if (!ok) any_setup_failure_ = true;
                Q_EMIT setup_step_finished(row, ok, detail);
                job_to_row_.remove(job);
                on_stage_step_finished();
            }, Qt::QueuedConnection);
        });
    rpc_->set_target(rec_.vsock_cid, kAgentPort);

    QTimer::singleShot(0, this, [this] { start_stage(0); });
}

bool Pipeline::dispatch_step(const SetupStep& s, int row) {
    nlohmann::json req = nlohmann::json::object();
    nlohmann::json params = nlohmann::json::object();
    params["name"] = s.name.toStdString();
    switch (s.kind) {
    case StepKind::DownloadInstall:
        req["cmd"] = "download_install";
        params["url"] = s.url.toStdString();
        params["args"] = s.args.toStdString();
        break;
    case StepKind::RunScript:
        req["cmd"] = "run_script";
        params["script"] = s.script.toStdString();
        break;
    case StepKind::DownloadExtract:
        req["cmd"] = "download_extract";
        params["url"] = s.url.toStdString();
        params["extract_to"] = s.extract_to.toStdString();
        params["script"] = s.script.toStdString();
        break;
    }
    const std::string cmd = req["cmd"].get<std::string>();
    req["params"] = std::move(params);

    LSW_INFO("pipeline: dispatch setup step row={} kind={} name='{}'", row, cmd,
             s.name.toStdString());
    Q_EMIT setup_step_started(row);
    // ``rpc_->call`` blocks until the agent acks with a job_id (or the
    // connection drops). The reply is small and the ack arrives almost
    // immediately on a healthy channel, so doing it on the GUI thread
    // is fine — the long-running work happens on the agent side and
    // streams back via setup_progress / setup_done events.
    const auto reply = rpc_->call(req);
    if (reply.isEmpty()) {
        any_setup_failure_ = true;
        Q_EMIT setup_step_finished(row, false, QStringLiteral("RPC failed (no reply)"));
        return false;
    }
    try {
        const auto j = nlohmann::json::parse(reply.constData(),
                                              reply.constData() + reply.size());
        if (!j.value("ok", false)) {
            const auto err = QString::fromStdString(j.value("error", std::string{"agent error"}));
            any_setup_failure_ = true;
            Q_EMIT setup_step_finished(row, false, err);
            return false;
        }
        const auto job_id = QString::fromStdString(j.value("job_id", std::string{}));
        if (job_id.isEmpty()) {
            any_setup_failure_ = true;
            Q_EMIT setup_step_finished(row, false, QStringLiteral("agent returned no job_id"));
            return false;
        }
        job_to_row_.insert(job_id, row);
        LSW_INFO("pipeline: setup step row={} accepted by agent job_id={}", row,
                 job_id.toStdString());
        return true;
    } catch (const nlohmann::json::exception& e) {
        LSW_WARN("pipeline: malformed setup ack: {}", e.what());
        any_setup_failure_ = true;
        Q_EMIT setup_step_finished(row, false, QStringLiteral("malformed ack"));
        return false;
    }
}

void Pipeline::start_stage(int idx) {
    if (idx < 0 || idx >= stages_.size()) {
        return;
    }
    current_stage_ = idx;
    const auto& stage = stages_[idx];

    int row_offset = 0;
    for (int i = 0; i < idx; ++i) {
        row_offset += static_cast<int>(stages_[i].steps.size());
    }
    stage_pending_ = static_cast<int>(stage.steps.size());
    LSW_INFO("pipeline: starting setup stage {}/{} ({} step(s), row_offset={})",
             idx + 1, stages_.size(), stage_pending_, row_offset);
    int sync_failures = 0;
    for (int i = 0; i < stage.steps.size(); ++i) {
        if (!dispatch_step(stage.steps[i], row_offset + i)) {
            ++sync_failures;
        }
    }
    stage_pending_ -= sync_failures;

    if (stage_pending_ == 0) {
        QTimer::singleShot(0, this, [this, idx] {
            const int next = idx + 1;
            if (next >= stages_.size()) {
                Q_EMIT step_finished(Step::RunningSetup);
                if (any_setup_failure_) {
                    emit_failed(Step::RunningSetup,
                        QStringLiteral("one or more setup steps failed — skipping virtio swap"));
                    return;
                }
                run_post_setup_swap();
                return;
            }
            start_stage(next);
        });
    }
}

void Pipeline::on_stage_step_finished() {
    if (stage_pending_ <= 0) {
        return;
    }
    --stage_pending_;
    LSW_INFO("pipeline: setup stage {} step finished, {} still pending",
             current_stage_ + 1, stage_pending_);
    if (stage_pending_ > 0) {
        return;
    }
    const int next = current_stage_ + 1;
    if (next >= stages_.size()) {
        Q_EMIT step_finished(Step::RunningSetup);
        if (any_setup_failure_) {
            emit_failed(Step::RunningSetup,
                QStringLiteral("one or more setup steps failed — skipping virtio swap"));
            return;
        }
        run_post_setup_swap();
        return;
    }
    start_stage(next);
}

// ----- Virtio swap --------------------------------------------------------

void Pipeline::run_post_setup_swap() {
    if (swap_started_) return;
    swap_started_ = true;

    // No controller swap: the VM already installed + booted on the
    // single high-perf virtio domain (virtio-blk from first boot via
    // PnP-injected viostor). Setup is done and the agent is up over
    // vsock. All that's left is to eject the now-useless install
    // media so reboots don't re-prompt the El-Torito CD and the ISOs
    // are freed. Ejecting cdrom *media* leaves the vda virtio-blk
    // system disk untouched — none of the old swap brick risk.
    LSW_INFO("pipeline: setup complete; ejecting install media and finishing");
    (void)app_.lifecycle().eject_install_media(rec_.uuid);
    Q_EMIT step_finished(Step::RunningSetup);
    emit_completed(rec_.uuid);
}

void Pipeline::perform_virtio_swap_worker() {
    const auto rec = rec_;
    auto* lifecycle = &app_.lifecycle();
    auto* rpc = &app_.rpc();
    const auto gen = generation_;
    const QPointer<Pipeline> self{this};

    (void)QtConcurrent::run([self, rec, lifecycle, rpc, gen]() {
        auto post = [&](auto fn) {
            QMetaObject::invokeMethod(self.data(), fn, Qt::QueuedConnection);
        };

        // 1. ACPI shutdown, then wait up to 90s for the qemu process
        //    to fully exit (NTFS flush, virtio driver post-install
        //    config). Fall back to force_off on timeout.
        if (lifecycle->shutdown(rec.uuid) != 0) {
            LSW_WARN("pipeline: virDomainShutdown failed; will force-off");
        }
        const auto off_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        while (lifecycle->is_running(rec.uuid)) {
            if (std::chrono::steady_clock::now() > off_deadline) {
                LSW_WARN("pipeline: guest didn't shut down in 90 s; forcing off");
                (void)lifecycle->force_off(rec.uuid);
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));  // qcow2 lock release

        post([self, gen]() {
            if (!self || self->generation_ != gen) return;
            Q_EMIT self->step_finished(Step::ShuttingDown);
            Q_EMIT self->step_started(Step::StartingVmVirtio,
                QStringLiteral("Defining the virtio-optimised domain and rebooting..."));
        });

        // 2. Redefine with use_virtio = true and start.
        vm::DomainXmlInputs xin;
        xin.use_virtio = true;
        const int swap_rc = lifecycle->define_and_start(rec, xin);
        if (swap_rc != 0) {
            post([self, gen]() {
                if (!self || self->generation_ != gen) return;
                self->emit_failed(Step::StartingVmVirtio,
                    QStringLiteral("libvirt rejected the post-virtio domain"));
            });
            return;
        }
        post([self, gen]() {
            if (!self || self->generation_ != gen) return;
            Q_EMIT self->step_finished(Step::StartingVmVirtio);
            Q_EMIT self->step_started(Step::WaitingForAgentAfterSwap,
                QStringLiteral("Waiting for the agent to answer on the virtio config..."));
        });

        // 3. Probe the agent for up to 3 min — virtio first-boot
        //    enumerates drivers and tail-Windows-Updates can sneak in.
        const auto agent_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
        bool agent_ok = false;
        while (std::chrono::steady_clock::now() < agent_deadline) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            const auto reply = rpc->call(nlohmann::json{{"cmd", "system_info"}});
            if (!reply.isEmpty()) {
                agent_ok = true;
                break;
            }
        }

        post([self, agent_ok, rec, gen]() {
            if (!self || self->generation_ != gen) return;
            if (agent_ok) {
                Q_EMIT self->step_finished(Step::WaitingForAgentAfterSwap);
                self->emit_completed(rec.uuid);
            } else {
                self->emit_failed(Step::WaitingForAgentAfterSwap,
                    QStringLiteral("agent didn't answer in 3 minutes after virtio swap"));
            }
        });
    });
}

}  // namespace lsw::manager::wizard
