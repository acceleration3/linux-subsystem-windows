#include "lsw/host/manager/document_opener.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/manager/guest_app_launch_controller.hpp"
#include "lsw/host/manager/open_document_dialog.hpp"
#include "lsw/host/services/guest_document_path.hpp"
#include "lsw/host/services/host_services.hpp"
#include "lsw/host/services/host_share_layout.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QDir>
#include <QFileInfo>
#include <QtConcurrent>

#include <algorithm>

namespace lsw::host::manager {

namespace {

using Share = vm::DomainDescription::Share;
using services::GuestDocumentPath;

QVector<Share> everything() {
    return {Share{QStringLiteral("Host"), QStringLiteral("/"), false, QStringLiteral("z")}};
}

}

DocumentOpener::DocumentOpener(services::HostServices& services, GuestAppLaunchController& controller,
                               QObject* parent)
    : QObject(parent), services_(services), controller_(controller) {}

void DocumentOpener::open(const QString& app_id, const QStringList& files,
                          const QString& activation_token) {
    const config::VmRecord* vm = services_.vms().primary();
    GuestAppLaunchController::Request request;
    request.app_id = app_id;
    request.activation_token = activation_token;
    if (vm == nullptr) {
        LSW_WARN("open documents: no VM defined");
        request.uploads = files;
        controller_.fail(request, QStringLiteral("There is no Windows machine set up yet, so there is "
                                                 "nothing to open these in. Open the manager to set one up."));
        return;
    }
    auto& lifecycle = services_.lifecycle();
    const QUuid uuid = vm->uuid;
    QtConcurrent::run([&lifecycle, uuid, files] {
        Plan plan;
        const QString xml = lifecycle.domain_xml(uuid, !lifecycle.is_running(uuid));
        const auto description = xml.isEmpty() ? std::nullopt : vm::DomainDescription::parse(xml);
        plan.shares = description ? description->shares : QVector<Share>{};
        for (Share& share : plan.shares) {
            if (const QString real = GuestDocumentPath::canonical(share.host_path); !real.isEmpty()) {
                share.host_path = real;
            }
        }
        for (const QString& file : files) {
            const QString path = GuestDocumentPath::canonical(file);
            if (path.isEmpty()) {
                LSW_WARN("open documents: {} is gone", file.toStdString());
                continue;
            }
            if (const auto guest = GuestDocumentPath::map(path, plan.shares)) {
                plan.mapped << *guest;
            } else {
                plan.unmapped << path;
            }
        }
        return plan;
    }).then(this, [this, app_id, activation_token](const Plan& plan) {
        proceed(app_id, plan, activation_token);
    });
}

void DocumentOpener::proceed(const QString& app_id, const Plan& plan, const QString& activation_token) {
    if (plan.mapped.isEmpty() && plan.unmapped.isEmpty()) {
        return;
    }
    LSW_INFO("open documents with {}: {} in the shares, {} outside", app_id.toStdString(),
             plan.mapped.size(), plan.unmapped.size());
    if (!plan.unmapped.isEmpty()) {
        ask(app_id, plan, activation_token);
        return;
    }
    GuestAppLaunchController::Request request;
    request.app_id = app_id;
    request.documents = plan.mapped;
    request.activation_token = activation_token;
    controller_.open(std::move(request));
}

QStringList DocumentOpener::share_and_map(const QStringList& shareable, const Plan& plan, QStringList* uploads) {
    auto& pending = services_.pending_changes();
    auto& settings = services_.manager_settings();

    QStringList folders;
    for (const QString& file : shareable) {
        const QString folder = QFileInfo(file).absolutePath();
        if (!folders.contains(folder)) {
            folders << folder;
        }
    }
    std::sort(folders.begin(), folders.end(),
              [](const QString& a, const QString& b) { return a.size() < b.size(); });
    for (const QString& folder : std::as_const(folders)) {
        const auto stored = settings.host_shares();
        const bool covered = std::any_of(stored.begin(), stored.end(), [&folder](const config::HostShare& s) {
            const QString root = GuestDocumentPath::canonical(s.host_path);
            return !root.isEmpty() && GuestDocumentPath::within(folder, root);
        });
        if (covered) {
            continue;
        }
        if (!pending.add_share(folder)) {
            LSW_WARN("no free drive letter to share {} with the guest", folder.toStdString());
        } else {
            LSW_INFO("sharing {} with the guest to open documents from it", folder.toStdString());
        }
    }

    QVector<Share> shares = plan.shares;
    for (Share& share : services::HostShareLayout::assign(settings.host_shares())) {
        share.host_path = GuestDocumentPath::canonical(share.host_path);
        if (!share.host_path.isEmpty()) {
            shares.push_back(std::move(share));
        }
    }
    QStringList documents;
    for (const QString& path : shareable) {
        if (const auto guest = GuestDocumentPath::map(path, shares)) {
            documents << *guest;
        } else {
            *uploads << path;
        }
    }
    return documents;
}

void DocumentOpener::ask(const QString& app_id, const Plan& plan, const QString& activation_token) {
    GuestAppLaunchController::Request request;
    request.app_id = app_id;
    request.documents = plan.mapped;
    request.activation_token = activation_token;

    QStringList folders;
    int unshareable = 0;
    for (const QString& path : plan.unmapped) {
        const QString folder = QFileInfo(path).absolutePath();
        if (!folders.contains(folder)) {
            folders << folder;
        }
        if (!GuestDocumentPath::map(path, everything())) {
            ++unshareable;
        }
    }
    QString application;
    if (!app_id.isEmpty()) {
        if (const auto* cfg = services_.app_configs().find(app_id)) {
            application = cfg->name;
        }
    }
    const config::VmRecord* vm = services_.vms().primary();
    const bool running = vm != nullptr && services_.lifecycle().is_running(vm->uuid);

    auto* dialog = new OpenDocumentDialog(plan.unmapped, folders, application, running, unshareable);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const QStringList unmapped = plan.unmapped;
    connect(dialog, &QDialog::finished, this, [this, dialog, request, plan, unmapped, running](int) mutable {
        switch (dialog->choice()) {
        case OpenDocumentDialog::Choice::Cancel:
            return;
        case OpenDocumentDialog::Choice::Copy:
            request.uploads = unmapped;
            controller_.open(std::move(request));
            return;
        case OpenDocumentDialog::Choice::Share:
            break;
        }
        QStringList shareable;
        for (const QString& path : std::as_const(unmapped)) {
            (GuestDocumentPath::map(path, everything()) ? shareable : request.uploads) << path;
        }
        const QStringList shared = shareable.isEmpty() ? QStringList{} : share_and_map(shareable, plan, &request.uploads);
        request.documents << shared;
        if (shared.isEmpty()) {
            controller_.open(std::move(request));
            return;
        }
        const config::VmRecord* vm = services_.vms().primary();
        request.restart_from_id = running && vm != nullptr ? services_.lifecycle().domain_id(vm->uuid) : -1;
        QString why;
        const RestartResult result = restarter_ ? restarter_(&why) : RestartResult::Refused;
        if (result == RestartResult::Refused) {
            LSW_WARN("sharing for documents: the guest was not restarted: {}", why.toStdString());
            request.restart_from_id = -1;
            controller_.fail(request, QStringLiteral("Windows could not be restarted to see the shared folder: %1")
                                          .arg(why.isEmpty() ? QStringLiteral("see the log") : why));
            return;
        }
        controller_.open(std::move(request));
    });
    dialog->show();
    ui::Widgets::bring_to_front(dialog);
}

}
