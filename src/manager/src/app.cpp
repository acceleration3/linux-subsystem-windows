#include "lsw/manager/app.hpp"

#include "lsw/log.hpp"
#include "lsw/manager/install/wim_editions.hpp"
#include "lsw/manager/mcp/mcp_server.hpp"
#include "lsw/manager/ui/manager/main_window.hpp"
#include "lsw/manager/ui/wizard/wizard.hpp"
#include "lsw/manager/wizard/microsoft_resolver.hpp"
#include "lsw/manager/wizard/pipeline.hpp"

#include <QDir>
#include <QMetaEnum>
#include <QTextStream>
#include <libvirt/libvirt.h>

#include <cstdio>

namespace lsw::manager {

App::App(int& argc, char** argv)
    : QApplication(argc, argv), settings_(this), vms_(this), libvirt_(this),
      lifecycle_(libvirt_, this), monitor_(libvirt_, this),
      app_configs_(QDir(settings_.data_dir()).filePath("apps"), this),
      rpc_(this), log_store_(this) {
    // Attach the log push handler now so we collect records the
    // moment rpc_ connects, even before the UI is up.
    log_store_.attach(rpc_);
    // Same for the stats monitor — it needs the rpc to pull in-guest
    // CPU / memory / disk numbers (libvirt alone can't see them
    // without virtio-balloon, which isn't installed yet).
    monitor_.set_rpc(&rpc_);
    setApplicationName("lsw-manager");
    setOrganizationName("lsw");
    setOrganizationDomain("lsw");
    // We turn this OFF only once a real, tray-backed main window is up
    // (see ``show_main_window``). During the wizard phase we keep it ON
    // so closing/cancelling the wizard actually exits the process —
    // otherwise the QApplication event loop has no UI and no tray icon
    // to drive it, and the user can only kill us with Ctrl-C.
    setQuitOnLastWindowClosed(true);

    connect(&single_instance_, &SingleInstance::raise_requested, this, &App::on_raise_request);
}

App::~App() = default;

int App::run() {
    if (!single_instance_.acquire()) {
        LSW_INFO("another lsw-manager is already running; raising it");
        (void)single_instance_.raise_existing();
        return 0;
    }
    if (!settings_.ensure_directories()) {
        LSW_ERROR("could not create LSW data directories");
        return 1;
    }
    if (libvirt_.open() != 0) {
        LSW_WARN("libvirt unavailable — VM controls disabled");
    }
    populate_existing_vm();

    // Stand up the MCP server. Localhost-only, no auth — single user
    // assumption. Failure to bind is non-fatal; we just log and skip.
    mcp_server_ = std::make_unique<mcp::McpServer>(rpc_, log_store_, this);
    (void)mcp_server_->listen();

    if (cli_.unattended()) {
        run_unattended();
    } else if (settings_.is_first_run() || vms_.records().isEmpty()) {
        show_wizard();
    } else {
        show_main_window();
    }
    return exec();
}

void App::populate_existing_vm() {
    if (!libvirt_.is_open()) {
        return;
    }
    auto* d = ::virDomainLookupByName(libvirt_.raw(), config::kVmName);
    if (d == nullptr) {
        return;  // no VM defined yet — wizard will create one
    }
    vm::DomainHandle handle{d};

    config::VmRecord rec;
    rec.name = config::kVmName;
    unsigned char uuid_bytes[VIR_UUID_BUFLEN] = {};
    if (::virDomainGetUUID(d, uuid_bytes) == 0) {
        rec.uuid = QUuid::fromRfc4122(
            QByteArray(reinterpret_cast<const char*>(uuid_bytes), VIR_UUID_BUFLEN));
    } else {
        rec.uuid = QUuid::createUuid();  // fallback — non-fatal
    }
    const QString fixed = QString::fromLatin1(config::kVmName);
    rec.disk_path = QDir(settings_.disks_dir()).filePath(QStringLiteral("%1.qcow2").arg(fixed));
    rec.nvram_path = QDir(settings_.nvram_dir()).filePath(QStringLiteral("%1.fd").arg(fixed));
    rec.payload_iso_path =
        QDir(settings_.isos_dir()).filePath(QStringLiteral("%1-payload.iso").arg(fixed));
    vms_.add(std::move(rec));
    const auto line = std::string{"found existing libvirt domain '"} + config::kVmName
                      + "', registered with manager";
    LSW_INFO("{}", line);
}

void App::show_wizard() {
    if (wizard_ == nullptr) {
        wizard_ = new ui::wizard::Wizard(*this);
        connect(wizard_, &ui::wizard::Wizard::complete, this, &App::on_wizard_complete);
    }
    wizard_->show();
    wizard_->raise();
    wizard_->activateWindow();
}

void App::show_main_window() {
    if (main_window_ == nullptr) {
        main_window_ = new ui::manager::MainWindow(*this);
    }
    main_window_->show();
    main_window_->raise();
    main_window_->activateWindow();
}

void App::on_wizard_complete() {
    settings_.mark_first_run_complete();
    if (wizard_ != nullptr) {
        wizard_->hide();
    }
    show_main_window();
}

void App::on_raise_request() {
    if (main_window_ != nullptr) {
        main_window_->show();
        main_window_->raise();
        main_window_->activateWindow();
    } else if (wizard_ != nullptr) {
        wizard_->show();
        wizard_->raise();
        wizard_->activateWindow();
    }
}

namespace {

// One-line text rendering of a Pipeline::Step for stdout. Mirrors the
// human-facing description the wizard pages would otherwise show.
[[nodiscard]] QString step_label(wizard::Pipeline::Step s) {
    using Step = wizard::Pipeline::Step;
    switch (s) {
    case Step::DownloadingIso:            return QStringLiteral("download-iso");
    case Step::BuildingPayloadIso:        return QStringLiteral("build-payload");
    case Step::CreatingDisk:              return QStringLiteral("create-disk");
    case Step::StartingVm:                return QStringLiteral("start-vm");
    case Step::WaitingForAgent:           return QStringLiteral("wait-for-agent");
    case Step::RunningSetup:              return QStringLiteral("setup");
    case Step::ShuttingDown:              return QStringLiteral("shutdown");
    case Step::StartingVmVirtio:          return QStringLiteral("start-vm-virtio");
    case Step::WaitingForAgentAfterSwap:  return QStringLiteral("wait-for-agent-after-swap");
    }
    return QStringLiteral("?");
}

void println(const QString& line) {
    std::fputs(line.toUtf8().constData(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

}  // namespace

void App::run_unattended() {
    if (!cli_.accept_license) {
        std::fputs("--accept-license is required for unattended mode (LSW does not bundle "
                   "Windows; supplying this flag is your acknowledgement that you have a "
                   "valid Windows license).\n", stderr);
        QMetaObject::invokeMethod(this, [this] { exit(2); }, Qt::QueuedConnection);
        return;
    }
    if (!vms_.records().isEmpty()) {
        std::fputs("a VM is already defined; refusing to run the install pipeline a second "
                   "time. Use the manager UI or remove the existing domain first.\n", stderr);
        QMetaObject::invokeMethod(this, [this] { exit(3); }, Qt::QueuedConnection);
        return;
    }

    auto* pipeline = new wizard::Pipeline(*this, this);

    // Plain stdout logging — the unattended runner is meant for scripts
    // and CI, not interactive use. One line per step transition.
    connect(pipeline, &wizard::Pipeline::step_started, this,
            [](wizard::Pipeline::Step s, const QString& desc) {
                println(QStringLiteral("[%1] %2").arg(step_label(s), desc));
            });
    connect(pipeline, &wizard::Pipeline::step_progress, this,
            [](wizard::Pipeline::Step s, double, const QString& detail) {
                if (!detail.isEmpty()) {
                    println(QStringLiteral("[%1] %2").arg(step_label(s), detail));
                }
            });
    connect(pipeline, &wizard::Pipeline::step_finished, this,
            [](wizard::Pipeline::Step s) {
                println(QStringLiteral("[%1] finished").arg(step_label(s)));
            });
    connect(pipeline, &wizard::Pipeline::setup_step_started, this,
            [pipeline](int row) {
                println(QStringLiteral("[setup] step %1: running").arg(row));
                (void)pipeline;
            });
    connect(pipeline, &wizard::Pipeline::setup_step_finished, this,
            [](int row, bool ok, const QString& detail) {
                println(QStringLiteral("[setup] step %1: %2 (%3)")
                            .arg(row).arg(ok ? "done" : "failed", detail));
            });
    connect(pipeline, &wizard::Pipeline::vnc_port_available, this, [](int port) {
        println(QStringLiteral("[vnc] graphics port: 127.0.0.1:%1 "
                                 "(attach virt-viewer to watch the install)").arg(port));
    });
    connect(pipeline, &wizard::Pipeline::failed, this,
            [this](wizard::Pipeline::Step s, const QString& err) {
                std::fputs(QStringLiteral("[%1] FAILED: %2\n")
                                .arg(step_label(s), err)
                                .toUtf8().constData(), stderr);
                exit(1);
            });
    connect(pipeline, &wizard::Pipeline::completed, this, [this](const QUuid& uuid) {
        println(QStringLiteral("[done] VM %1 ready").arg(uuid.toString()));
        settings_.mark_first_run_complete();
        exit(0);
    });

    wizard::Pipeline::Inputs in;
    in.iso_local_path = cli_.iso_path;
    in.user_name = cli_.user_name.isEmpty() ? QStringLiteral("lsw") : cli_.user_name;
    in.user_password = cli_.user_password.isEmpty() ? QStringLiteral("lsw") : cli_.user_password;
    in.computer_name = cli_.computer_name;  // empty → pipeline falls back to VM name
    if (cli_.cpu_cores > 0)  in.sizing.cpu_cores  = cli_.cpu_cores;
    if (cli_.memory_mib > 0) in.sizing.memory_mib = cli_.memory_mib;
    if (cli_.disk_gib > 0)   in.sizing.disk_gib   = cli_.disk_gib;

    if (!cli_.iso_path.isEmpty()) {
        // Local ISO: --edition selects the WIM image. Accept either a
        // 1-based index ("--edition 6") or a case-insensitive substring
        // of the edition name ("--edition pro"). Empty → image 1.
        println(QStringLiteral("[editions] reading editions from %1 ...").arg(cli_.iso_path));
        QString err;
        const auto editions =
            install::list_iso_editions(cli_.iso_path, settings_.cache_dir(), &err);
        if (editions.isEmpty()) {
            std::fputs(QStringLiteral("[editions] FAILED: %1\n").arg(err)
                            .toUtf8().constData(), stderr);
            QMetaObject::invokeMethod(this, [this] { exit(4); }, Qt::QueuedConnection);
            return;
        }
        for (const auto& e : editions) {
            println(QStringLiteral("[editions]   %1: %2").arg(e.index).arg(e.name));
        }
        int chosen = editions.first().index;
        if (!cli_.edition_label.isEmpty()) {
            bool is_num = false;
            const int as_idx = cli_.edition_label.toInt(&is_num);
            int matched = -1;
            if (is_num) {
                for (const auto& e : editions) {
                    if (e.index == as_idx) { matched = e.index; break; }
                }
            } else {
                for (const auto& e : editions) {
                    if (e.name.contains(cli_.edition_label, Qt::CaseInsensitive)) {
                        matched = e.index;
                        break;
                    }
                }
            }
            if (matched < 0) {
                std::fputs(QStringLiteral("[editions] FAILED: --edition '%1' matched no "
                                           "edition by index or name\n")
                                .arg(cli_.edition_label).toUtf8().constData(), stderr);
                QMetaObject::invokeMethod(this, [this] { exit(4); }, Qt::QueuedConnection);
                return;
            }
            chosen = matched;
        }
        in.image_index = chosen;
        const auto it = std::find_if(editions.begin(), editions.end(),
            [chosen](const install::WimEdition& e) { return e.index == chosen; });
        println(QStringLiteral("[editions] installing #%1: %2")
                    .arg(chosen).arg(it != editions.end() ? it->name : QString{}));
        pipeline->set_inputs(in);
        pipeline->start();
        return;
    }

    // Microsoft download path. Resolve the URL through the same
    // pipeline IsoChoicePage uses, then hand off to the install
    // pipeline once we have a real URL.
    println(QStringLiteral("[resolve] querying microsoft.com for edition='%1' language='%2'")
                .arg(cli_.edition_label.isEmpty() ? QStringLiteral("Windows 11") : cli_.edition_label,
                     cli_.language_label.isEmpty() ? QStringLiteral("English") : cli_.language_label));
    auto* resolver = new wizard::MicrosoftResolver(this);
    connect(resolver, &wizard::MicrosoftResolver::resolved, this,
            [this, pipeline, in, resolver](const QString& url, const QString& display) mutable {
                println(QStringLiteral("[resolve] %1 -> %2").arg(display, url));
                in.iso_download_url = url;
                in.iso_display_label = display;
                pipeline->set_inputs(in);
                pipeline->start();
                resolver->deleteLater();
            });
    connect(resolver, &wizard::MicrosoftResolver::failed, this,
            [this, resolver](const QString& msg, bool /*banned*/) {
                std::fputs(QStringLiteral("[resolve] FAILED: %1\n").arg(msg)
                                .toUtf8().constData(), stderr);
                resolver->deleteLater();
                exit(1);
            });
    resolver->resolve(
        cli_.edition_label.isEmpty() ? QStringLiteral("Windows 11") : cli_.edition_label,
        cli_.language_label);
}

}  // namespace lsw::manager
