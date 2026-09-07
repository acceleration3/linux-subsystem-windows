#include "lsw/host/app/unattended_runner.hpp"

#include "lsw/host/config/settings.hpp"
#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/provision/install/wim_edition_lister.hpp"
#include "lsw/host/provision/microsoft_resolver.hpp"
#include "lsw/host/services/host_services.hpp"

#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <cstdio>

namespace lsw::host::app {

using Step = provision::Pipeline::Step;

UnattendedRunner::UnattendedRunner(services::HostServices& services, CliOptions cli,
                                   QObject* parent)
    : QObject(parent), services_(services), cli_(std::move(cli)) {}

QString UnattendedRunner::step_label(Step s) {
    switch (s) {
    case Step::DownloadingIso:           return QStringLiteral("download-iso");
    case Step::TrimmingImage:            return QStringLiteral("trim-image");
    case Step::BuildingPayloadIso:       return QStringLiteral("build-payload");
    case Step::CreatingDisk:             return QStringLiteral("create-disk");
    case Step::StartingVm:               return QStringLiteral("start-vm");
    case Step::WaitingForAgent:          return QStringLiteral("wait-for-agent");
    case Step::RunningSetup:             return QStringLiteral("setup");
    case Step::ShuttingDown:             return QStringLiteral("shutdown");
    case Step::StartingVmVirtio:         return QStringLiteral("start-vm-virtio");
    case Step::WaitingForAgentAfterSwap: return QStringLiteral("wait-for-agent-after-swap");
    }
    return QStringLiteral("?");
}

void UnattendedRunner::println(const QString& line) {
    std::fputs(line.toUtf8().constData(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void UnattendedRunner::fail(const QString& message, int code) {
    std::fputs(message.toUtf8().constData(), stderr);
    std::fputc('\n', stderr);
    QMetaObject::invokeMethod(this, [this, code] { Q_EMIT finished(code); },
                              Qt::QueuedConnection);
}

void UnattendedRunner::start() {
    if (!cli_.accept_license) {
        fail(QStringLiteral(
                 "--accept-license is required for unattended mode (LSW does not bundle "
                 "Windows; supplying this flag is your acknowledgement that you have a "
                 "valid Windows license)."), 2);
        return;
    }
    if (!services_.vms().records().isEmpty()) {
        fail(QStringLiteral(
                 "a VM is already defined; refusing to run the install pipeline a second "
                 "time. Use the manager UI or remove the existing domain first."), 3);
        return;
    }

    pipeline_ = new provision::Pipeline(services_, this);
    connect_progress_output();

    provision::PipelineInputs in;
    in.iso_local_path = cli_.iso_path;
    in.user_name = cli_.user_name.isEmpty() ? QStringLiteral("lsw") : cli_.user_name;
    in.user_password = cli_.user_password.isEmpty() ? QStringLiteral("lsw") : cli_.user_password;
    in.computer_name = cli_.computer_name;  // empty → pipeline falls back to the VM name
    if (cli_.cpu_cores > 0)  in.sizing.cpu_cores  = cli_.cpu_cores;
    if (cli_.memory_mib > 0) in.sizing.memory_mib = cli_.memory_mib;
    if (cli_.disk_gib > 0)   in.sizing.disk_gib   = cli_.disk_gib;

    if (!cli_.iso_path.isEmpty()) {
        if (!resolve_local_iso(in)) {
            return;
        }
        pipeline_->set_inputs(in);
        pipeline_->start();
        return;
    }
    resolve_microsoft_iso(std::move(in));
}

void UnattendedRunner::connect_progress_output() {
    connect(pipeline_, &provision::Pipeline::step_started, this,
            [](Step s, const QString& desc) {
                println(QStringLiteral("[%1] %2").arg(step_label(s), desc));
            });
    connect(pipeline_, &provision::Pipeline::step_progress, this,
            [](Step s, double, const QString& detail) {
                if (!detail.isEmpty()) {
                    println(QStringLiteral("[%1] %2").arg(step_label(s), detail));
                }
            });
    connect(pipeline_, &provision::Pipeline::step_finished, this, [](Step s) {
        println(QStringLiteral("[%1] finished").arg(step_label(s)));
    });
    connect(pipeline_, &provision::Pipeline::setup_step_started, this, [](int row) {
        println(QStringLiteral("[setup] step %1: running").arg(row));
    });
    connect(pipeline_, &provision::Pipeline::setup_step_finished, this,
            [](int row, bool ok, const QString& detail) {
                println(QStringLiteral("[setup] step %1: %2 (%3)")
                            .arg(row).arg(ok ? "done" : "failed", detail));
            });
    connect(pipeline_, &provision::Pipeline::vnc_port_available, this, [](int port) {
        println(QStringLiteral("[vnc] graphics port: 127.0.0.1:%1 "
                               "(attach virt-viewer to watch the install)").arg(port));
    });
    connect(pipeline_, &provision::Pipeline::failed, this,
            [this](Step s, const QString& err) {
                std::fputs(QStringLiteral("[%1] FAILED: %2\n").arg(step_label(s), err)
                               .toUtf8().constData(), stderr);
                Q_EMIT finished(1);
            });
    connect(pipeline_, &provision::Pipeline::completed, this, [this](const QUuid& uuid) {
        println(QStringLiteral("[done] VM %1 ready").arg(uuid.toString()));
        services_.settings().mark_first_run_complete();
        Q_EMIT finished(0);
    });
}

bool UnattendedRunner::resolve_local_iso(provision::PipelineInputs& in) {
    println(QStringLiteral("[editions] reading editions from %1 ...").arg(cli_.iso_path));
    QString err;
    const auto editions = provision::install::WimEditionLister::list(
        cli_.iso_path, services_.settings().cache_dir(), &err);
    if (editions.isEmpty()) {
        fail(QStringLiteral("[editions] FAILED: %1").arg(err), 4);
        return false;
    }
    for (const auto& e : editions) {
        println(QStringLiteral("[editions]   %1: %2").arg(e.index).arg(e.name));
    }

    int chosen = editions.first().index;
    if (!cli_.edition_label.isEmpty()) {
        bool is_num = false;
        const int as_idx = cli_.edition_label.toInt(&is_num);
        int matched = -1;
        for (const auto& e : editions) {
            const bool hit = is_num ? (e.index == as_idx)
                                    : e.name.contains(cli_.edition_label, Qt::CaseInsensitive);
            if (hit) { matched = e.index; break; }
        }
        if (matched < 0) {
            fail(QStringLiteral("[editions] FAILED: --edition '%1' matched no edition "
                                "by index or name").arg(cli_.edition_label), 4);
            return false;
        }
        chosen = matched;
    }
    in.image_index = chosen;
    const auto it = std::find_if(editions.begin(), editions.end(),
        [chosen](const provision::install::WimEdition& e) { return e.index == chosen; });
    println(QStringLiteral("[editions] installing #%1: %2")
                .arg(chosen).arg(it != editions.end() ? it->name : QString{}));
    return true;
}

void UnattendedRunner::resolve_microsoft_iso(provision::PipelineInputs in) {
    const QString edition = cli_.edition_label.isEmpty() ? QStringLiteral("Windows 11")
                                                         : cli_.edition_label;
    const QString language = cli_.language_label.isEmpty() ? QStringLiteral("English")
                                                           : cli_.language_label;
    println(QStringLiteral("[resolve] querying microsoft.com for edition='%1' language='%2'")
                .arg(edition, language));

    auto* resolver = new provision::MicrosoftResolver(this);
    connect(resolver, &provision::MicrosoftResolver::resolved, this,
            [this, in, resolver](const QString& url, const QString& display) mutable {
                println(QStringLiteral("[resolve] %1 -> %2").arg(display, url));
                in.iso_download_url = url;
                in.iso_display_label = display;
                pipeline_->set_inputs(in);
                pipeline_->start();
                resolver->deleteLater();
            });
    connect(resolver, &provision::MicrosoftResolver::failed, this,
            [this, resolver](const QString& msg, bool /*banned*/) {
                std::fputs(QStringLiteral("[resolve] FAILED: %1\n").arg(msg)
                               .toUtf8().constData(), stderr);
                resolver->deleteLater();
                Q_EMIT finished(1);
            });
    resolver->resolve(edition, cli_.language_label);
}

}  // namespace lsw::host::app
