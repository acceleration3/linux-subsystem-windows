#include "lsw/host/manager/adapter_roles.hpp"

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/platform/host_graphics_probe.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/ui/format.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace lsw::host::manager {

namespace {

using ui::Theme;
using ui::Widgets;
using K = config::ManagerSettings::Key;

enum Role { kDisplay = 0, kRenderOnly = 1, kNotUsed = 2 };

constexpr int kColName = 0;
constexpr int kColIds = 1;
constexpr int kColMemory = 2;
constexpr int kColDriver = 3;
constexpr int kColRole = 4;

}  // namespace

AdapterRoles::AdapterRoles(config::PendingChanges& pending, const services::StatusHub& status,
                           QWidget* parent)
    : QWidget(parent), pending_(pending), status_(status), gpus_(status.graphics().host_gpus),
      probed_(status.graphics().probed) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* frame = new QFrame;
    Theme::surface(frame, Theme::Role::kPanel);
    outer->addWidget(frame);
    auto* column = new QVBoxLayout(frame);
    column->setContentsMargins(1, 1, 1, 1);
    column->setSpacing(0);

    table_ = new QTableWidget(0, 5);
    Theme::rows_table(table_, {QStringLiteral("HOST GPU"), QStringLiteral("IDS"),
                               QStringLiteral("MEMORY"), QStringLiteral("HOST DRIVER"),
                               QStringLiteral("ROLE")});
    table_->verticalHeader()->setDefaultSectionSize(42);
    table_->setProperty("controls", true);
    // The IDs, the memory and the driver are short and must be read whole,
    // so their columns are as wide as what is in them; the GPU's name
    // takes what is left and is the one that elides. The role column is
    // sized to its combos in rebuild.
    auto* head = table_->horizontalHeader();
    head->setSectionResizeMode(kColName, QHeaderView::Stretch);
    for (const int col : {kColIds, kColMemory, kColDriver}) {
        head->setSectionResizeMode(col, QHeaderView::ResizeToContents);
    }
    head->setSectionResizeMode(kColRole, QHeaderView::Fixed);
    column->addWidget(table_);

    auto* foot = new QWidget;
    Theme::surface(foot, Theme::Role::kListFoot);
    auto* foot_row = new QVBoxLayout(foot);
    foot_row->setContentsMargins(10, 8, 10, 8);
    foot_row->addWidget(Widgets::meta(
        QStringLiteral("A GPU added after Windows was installed has no driver in the guest "
                       "yet; the Graphics page installs it."),
        true));
    column->addWidget(foot);

    // Revert and Apply change what is staged; the rows follow.
    connect(&pending_, &config::PendingChanges::changed, this, &AdapterRoles::rebuild,
            Qt::QueuedConnection);
    connect(&status_, &services::StatusHub::changed, this, [this](services::StatusHub::Area area) {
        if (area != services::StatusHub::Area::Graphics) {
            return;
        }
        // The list, and whether the host has answered at all: an answer
        // of no GPUs is the same list as before it came, and the row
        // that said "Asking the host" must still change.
        const auto& now = status_.graphics().host_gpus;
        const bool same = now.size() == gpus_.size()
                          && std::equal(now.begin(), now.end(), gpus_.begin(),
                                        [](const auto& a, const auto& b) {
                                            return a.uuid == b.uuid && a.driver == b.driver
                                                   && a.driver_info == b.driver_info;
                                        });
        if (!same || probed_ != status_.graphics().probed) {
            gpus_ = now;
            probed_ = status_.graphics().probed;
            rebuild();
        }
    });
    rebuild();
}

QVector<platform::HostGpu> AdapterRoles::listed(const QVector<platform::HostGpu>& gpus,
                                                const QString& display_uuid,
                                                const QStringList& render) {
    const bool all_cpu =
        std::all_of(gpus.begin(), gpus.end(), [](const platform::HostGpu& g) { return g.cpu; });
    QVector<platform::HostGpu> out;
    for (const auto& gpu : gpus) {
        if (!gpu.cpu || all_cpu || gpu.uuid == display_uuid || render.contains(gpu.uuid)) {
            out.push_back(gpu);
        }
    }
    return out;
}

void AdapterRoles::rebuild() {
    const QString setting = pending_.staged_or_current(K::kHostGpu).toString().trimmed();
    const bool is_auto = setting.isEmpty() || setting == QLatin1StringView("auto");
    const auto display = platform::HostGpus::resolve(gpus_, setting);
    const QString display_uuid = display ? display->uuid : QString{};
    const QStringList render = pending_.staged_or_current(K::kRenderGpus).toStringList();
    const auto rows = listed(gpus_, display_uuid, render);

    table_->setRowCount(rows.isEmpty() ? 1 : static_cast<int>(rows.size()));
    if (rows.isEmpty()) {
        table_->setItem(0, kColName,
                        new QTableWidgetItem(status_.graphics().probed
                                                 ? QStringLiteral("No Vulkan device found here")
                                                 : QStringLiteral("Asking the host…")));
        table_->setItem(0, kColDriver, Theme::kind_cell(QStringLiteral("QEMU picks")));
        auto* only = new QComboBox;
        only->addItem(QStringLiteral("Display (auto)"));
        set_role_cell(0, only);
        Theme::fit_rows(table_);
        return;
    }
    for (int row = 0; row < rows.size(); ++row) {
        const auto& gpu = rows.at(row);
        auto* name = new QTableWidgetItem(platform::HostGpus::adapter_name(gpu));
        name->setToolTip(gpu.uuid);
        table_->setItem(row, kColName, name);
        table_->setItem(row, kColIds, Theme::kind_cell(QStringLiteral("%1:%2")
                                                           .arg(gpu.vendor_id, 4, 16, QLatin1Char('0'))
                                                           .arg(gpu.device_id, 4, 16, QLatin1Char('0'))));
        table_->setItem(row, kColMemory,
                        Theme::kind_cell(gpu.vram_bytes == 0
                                             ? QStringLiteral("shared")
                                             : ui::Format::gib(static_cast<qint64>(gpu.vram_bytes), 0)));
        table_->setItem(row, kColDriver,
                        Theme::kind_cell(platform::HostGraphicsProbe::driver_label(gpu)));

        auto* role = new QComboBox;
        if (gpu.uuid == display_uuid) {
            role->addItem(is_auto ? QStringLiteral("Display (auto)") : QStringLiteral("Display"),
                          kDisplay);
        } else {
            role->addItem(QStringLiteral("Display"), kDisplay);
            role->addItem(QStringLiteral("Render only"), kRenderOnly);
            role->addItem(QStringLiteral("Not used"), kNotUsed);
            role->setCurrentIndex(render.contains(gpu.uuid) ? 1 : 2);
        }
        // Queued: a choice rebuilds the table the combo sits in.
        connect(role, &QComboBox::activated, this, [this, role, uuid = gpu.uuid](int index) {
            const int chosen = role->itemData(index).toInt();
            QMetaObject::invokeMethod(this, [this, uuid, chosen] { choose(uuid, chosen); },
                                      Qt::QueuedConnection);
        });
        set_role_cell(row, role);
    }
    Theme::fit_rows(table_);
}

void AdapterRoles::set_role_cell(int row, QComboBox* combo) {
    // In a holder, centred at the combo's own height: set straight into
    // the cell it would be stretched to the row's.
    auto* holder = new QWidget;
    auto* line = new QHBoxLayout(holder);
    line->setContentsMargins(0, 0, 0, 0);
    line->addWidget(combo, 1, Qt::AlignVCenter);
    table_->setCellWidget(row, kColRole, holder);
    combo->ensurePolished();
    const int wanted = combo->sizeHint().width() + 2 * Theme::cell_inset();
    if (wanted > table_->columnWidth(kColRole)) {
        table_->setColumnWidth(kColRole, wanted);
    }
}

void AdapterRoles::choose(const QString& uuid, int role) {
    const QString setting = pending_.staged_or_current(K::kHostGpu).toString();
    const auto display = platform::HostGpus::resolve(gpus_, setting);
    QStringList render = pending_.staged_or_current(K::kRenderGpus).toStringList();
    if (display && display->uuid == uuid) {
        return;  // the display row has no other role
    }
    render.removeAll(uuid);
    if (role == kDisplay) {
        // The display moves; the one it leaves keeps working, as a
        // render-only adapter, rather than vanishing from the guest.
        if (display && !render.contains(display->uuid)) {
            render << display->uuid;
        }
        pending_.stage(K::kHostGpu, uuid);
    } else if (role == kRenderOnly) {
        render << uuid;
    }
    // In host order, so the same set is always the same list.
    QStringList ordered;
    for (const auto& gpu : gpus_) {
        if (render.contains(gpu.uuid)) {
            ordered << gpu.uuid;
        }
    }
    pending_.stage(K::kRenderGpus, ordered);
    rebuild();
}

}  // namespace lsw::host::manager
