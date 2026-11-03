#include "lsw/host/manager/adapter_roles.hpp"

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/services/status_hub.hpp"

#include <QAbstractItemView>
#include <QComboBox>

#include <algorithm>

namespace lsw::host::manager {

namespace {
using K = config::ManagerSettings::Key;
}

AdapterRoles::AdapterRoles(config::PendingChanges& pending, const services::StatusHub& status,
                           QObject* parent)
    : QObject(parent), pending_(pending), status_(status) {}

QString AdapterRoles::label(Role role) {
    switch (role) {
    case Role::Display:
        return QStringLiteral("Display");
    case Role::RenderOnly:
        return QStringLiteral("Render only");
    case Role::NotUsed:
        break;
    }
    return QStringLiteral("Not used");
}

QString AdapterRoles::staged_display_uuid() const {
    const QString setting = pending_.staged_or_current(K::kHostGpu).toString().trimmed();
    const auto display = platform::HostGpus::resolve(status_.graphics().host_gpus, setting);
    return display ? display->uuid : QString{};
}

QVector<platform::HostGpu> AdapterRoles::listed() const {
    const auto& gpus = status_.graphics().host_gpus;
    const QString display_uuid = staged_display_uuid();
    const QStringList render = pending_.staged_or_current(K::kRenderGpus).toStringList();
    const bool all_cpu =
        std::all_of(gpus.begin(), gpus.end(), [](const platform::HostGpu& g) { return g.cpu; });
    QVector<platform::HostGpu> out;
    for (const auto& gpu : gpus) {
        if (!gpu.cpu || all_cpu || gpu.uuid == display_uuid || render.contains(gpu.uuid)) {
            out.push_back(gpu);
        }
    }
    std::stable_partition(out.begin(), out.end(), [this](const platform::HostGpu& gpu) {
        return staged(gpu.uuid) == Role::Display;
    });
    return out;
}

AdapterRoles::Role AdapterRoles::staged(const QString& uuid) const {
    if (uuid == staged_display_uuid()) {
        return Role::Display;
    }
    return pending_.staged_or_current(K::kRenderGpus).toStringList().contains(uuid)
               ? Role::RenderOnly
               : Role::NotUsed;
}

AdapterRoles::Role AdapterRoles::running(const QString& uuid) const {
    for (const auto& adapter : status_.graphics().adapters) {
        if (adapter.gpu.uuid == uuid) {
            return adapter.display ? Role::Display : Role::RenderOnly;
        }
    }
    return Role::NotUsed;
}

bool AdapterRoles::differs(const QString& uuid) const {
    return status_.graphics().probed && staged(uuid) != running(uuid);
}

int AdapterRoles::staged_render_count() const {
    int count = 0;
    for (const auto& gpu : status_.graphics().host_gpus) {
        count += staged(gpu.uuid) == Role::RenderOnly ? 1 : 0;
    }
    return count;
}

QComboBox* AdapterRoles::combo(const platform::HostGpu& gpu) {
    const QString setting = pending_.staged_or_current(K::kHostGpu).toString().trimmed();
    const bool is_auto = setting.isEmpty() || setting == QLatin1StringView("auto");
    auto* role = new QComboBox;
    if (staged(gpu.uuid) == Role::Display) {
        role->addItem(is_auto ? QStringLiteral("Display (auto)") : label(Role::Display),
                      static_cast<int>(Role::Display));
    } else {
        for (const Role option : {Role::Display, Role::RenderOnly, Role::NotUsed}) {
            role->addItem(label(option), static_cast<int>(option));
        }
        role->setCurrentIndex(static_cast<int>(staged(gpu.uuid)));
    }
    role->view()->setMinimumWidth(role->sizeHint().width());
    connect(role, &QComboBox::activated, this, [this, role, uuid = gpu.uuid](int index) {
        const auto chosen = static_cast<Role>(role->itemData(index).toInt());
        QMetaObject::invokeMethod(this, [this, uuid, chosen] { choose(uuid, chosen); },
                                  Qt::QueuedConnection);
    });
    return role;
}

void AdapterRoles::choose(const QString& uuid, Role role) {
    const auto& gpus = status_.graphics().host_gpus;
    const auto display = platform::HostGpus::resolve(
        gpus, pending_.staged_or_current(K::kHostGpu).toString());
    QStringList render = pending_.staged_or_current(K::kRenderGpus).toStringList();
    if (display && display->uuid == uuid) {
        return;
    }
    render.removeAll(uuid);
    if (role == Role::Display) {
        if (display && !render.contains(display->uuid)) {
            render << display->uuid;
        }
        pending_.stage(K::kHostGpu, uuid);
    } else if (role == Role::RenderOnly) {
        render << uuid;
    }
    QStringList ordered;
    for (const auto& gpu : gpus) {
        if (render.contains(gpu.uuid)) {
            ordered << gpu.uuid;
        }
    }
    pending_.stage(K::kRenderGpus, ordered);
}

}
