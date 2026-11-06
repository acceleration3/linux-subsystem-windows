#include "lsw/host/manager/graphics_page.hpp"

#include "lsw/host/config/display_modes.hpp"
#include "lsw/host/config/display_refresh.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/manager/adapter_roles.hpp"
#include "lsw/host/manager/display_facts.hpp"
#include "lsw/host/manager/graphics_alerts.hpp"
#include "lsw/host/manager/setting_controls.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/manager/warning_strip.hpp"
#include "lsw/host/platform/accel_virt_paths.hpp"
#include "lsw/host/ui/flow_layout.hpp"
#include "lsw/host/ui/format.hpp"
#include "lsw/host/ui/key_values.hpp"
#include "lsw/host/ui/panel.hpp"
#include "lsw/host/ui/status_pill.hpp"
#include "lsw/host/ui/sort_item.hpp"
#include "lsw/host/ui/table_header.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QComboBox>
#include <algorithm>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

using ui::Format;
using ui::StatusPill;
using ui::Theme;
using ui::Widgets;
using K = config::ManagerSettings::Key;
using Adapter = services::GraphicsAdapter;
using Warning = GraphicsAlerts::Warning;
using Area = services::StatusHub::Area;

constexpr int kKpiCell = 170;

QString short_uuid(const QString& uuid) {
    if (uuid.size() < 16) {
        return uuid;
    }
    const QString ends = uuid.left(8) + uuid.right(4);
    if (ends.count(QLatin1Char('0')) == ends.size()) {
        return QStringLiteral("…%1…").arg(uuid.mid(9, 9));
    }
    return uuid.left(8) + QStringLiteral("…") + uuid.right(4);
}

QString ids(const platform::HostGpu& gpu) {
    return QStringLiteral("%1:%2")
        .arg(gpu.vendor_id, 4, 16, QLatin1Char('0'))
        .arg(gpu.device_id, 4, 16, QLatin1Char('0'));
}

QString memory(const platform::HostGpu& gpu) {
    if (gpu.vram_bytes == 0) {
        return QStringLiteral("shared");
    }
    return Format::gib(static_cast<qint64>(gpu.vram_bytes), 0);
}

ui::Panel card(const QString& name, const QString& caption, QWidget* right = nullptr) {
    auto panel = ui::Panel::area(name, caption);
    if (right != nullptr) {
        panel.header->addWidget(right);
    }
    return panel;
}

QWidget* kpi_grid(const QVector<QPair<QString, QString>>& figures) {
    auto* grid = new QFrame;
    Theme::surface(grid, Theme::Role::kKpiGrid);
    auto* row = new ui::FlowLayout(grid, ui::FlowLayout::Mode::Even, 1, 1);
    row->set_cell_width(kKpiCell);
    row->setContentsMargins(1, 1, 1, 1);
    for (const auto& [key, value] : figures) {
        auto* tile = new QWidget;
        Theme::surface(tile, Theme::Role::kTile);
        auto* column = new QVBoxLayout(tile);
        column->setContentsMargins(11, 9, 11, 9);
        column->setSpacing(4);
        column->addWidget(Widgets::caption(key));
        auto* figure = new QLabel(value);
        figure->setObjectName(QString::fromLatin1(Theme::Role::kKpiValue));
        figure->setToolTip(value);
        figure->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        figure->setMinimumWidth(0);
        column->addWidget(figure);
        row->addWidget(tile);
    }
    return grid;
}

using ui::KeyValues;

QString tilde(const QString& path) {
    const QString home = QDir::homePath();
    if (path.startsWith(home + QLatin1Char('/'))) {
        return QStringLiteral("~") + path.mid(home.size());
    }
    return path;
}

}

GraphicsPage::GraphicsPage(services::StatusHub& status, GraphicsAlerts& alerts,
                           config::ManagerSettings& settings, config::PendingChanges& pending,
                           QWidget* parent)
    : QWidget(parent), status_(status), alerts_(alerts), settings_(settings), pending_(pending),
      roles_(new AdapterRoles(pending, status, this)),
      controls_(new SettingControls(settings, pending, this)) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* holder = new QWidget;
    content_ = new QVBoxLayout(holder);
    content_->setContentsMargins(14, 14, 14, 14);
    content_->setSpacing(12);
    scroll->setWidget(holder);
    root->addWidget(scroll, 1);

    connect(&status_, &services::StatusHub::changed, this, [this](Area area) {
        if (area == Area::Graphics) {
            refresh();
        }
    });
    connect(&alerts_, &GraphicsAlerts::changed, this, &GraphicsPage::refresh);
    connect(&pending_, &config::PendingChanges::changed, this, &GraphicsPage::refresh);
    rebuild();
}

void GraphicsPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    status_.set_detail(Area::Graphics, true);
}

void GraphicsPage::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    status_.set_detail(Area::Graphics, false);
}

QString GraphicsPage::summary() const {
    const int running = static_cast<int>(status_.graphics().adapters.size());
    int staged = 0;
    for (const auto& gpu : status_.graphics().host_gpus) {
        staged += roles_->staged(gpu.uuid) != AdapterRoles::Role::NotUsed ? 1 : 0;
    }
    QString text = QStringLiteral("accel-virt · %1")
                       .arg(running == 1 ? QStringLiteral("1 adapter")
                                         : QStringLiteral("%1 adapters").arg(running));
    if (status_.graphics().probed && status_.graphics().pending.adapters && staged != running) {
        const int more = staged - running;
        text += more > 0 ? QStringLiteral(", %1 more after a restart").arg(more)
                         : QStringLiteral(", %1 fewer after a restart").arg(-more);
    }
    return text;
}

QStringList GraphicsPage::status_facts() const {
    const auto& g = status_.graphics();
    const vm::DomainDescription* definition = status_.vm().definition();
    const vm::DomainDescription::GpuDevice* device =
        definition != nullptr ? definition->display() : nullptr;
    QStringList facts;
    for (const auto& adapter : g.adapters) {
        facts << QStringLiteral("%1 %2")
                     .arg(adapter.display ? QStringLiteral("host-gpu") : QStringLiteral("render"),
                          short_uuid(adapter.gpu.uuid));
    }
    if (facts.isEmpty()) {
        facts << QStringLiteral("host-gpu %1")
                     .arg(device != nullptr ? short_uuid(device->host_gpu) : QStringLiteral("auto"));
    }
    if (device != nullptr && device->hostmem_mib > 0) {
        facts << QStringLiteral("hostmem %1")
                     .arg(platform::HostGpus::format_mib(static_cast<quint64>(device->hostmem_mib)));
    }
    const QString preferred = g.live_modes.value_or(QString{}).section(QLatin1Char(','), 0, 0);
    if (const auto mode = config::DisplayModes::parse(preferred)) {
        facts << QStringLiteral("display %1, %2")
                     .arg(mode->label(), config::DisplayRefresh::label(g.live_refresh));
    } else if (status_.vm().running()) {
        facts << QStringLiteral("display host monitors' modes");
    }
    if (g.pending.any()) {
        QStringList what;
        if (g.pending.host_window_mib) {
            what << QStringLiteral("hostmem %1")
                        .arg(platform::HostGpus::format_mib(static_cast<quint64>(*g.pending.host_window_mib)));
        }
        if (g.pending.adapters) {
            what << QStringLiteral("adapters");
        }
        facts << QStringLiteral("pending restart: %1").arg(what.join(QStringLiteral(", ")));
    }
    return facts;
}

void GraphicsPage::refresh() {
    if (signature() != built_for_) {
        rebuild();
        Q_EMIT summary_changed();
    }
}

QString GraphicsPage::signature() const {
    QStringList parts;
    for (const auto& adapter : status_.graphics().adapters) {
        parts << adapter.gpu.uuid << QString::number(adapter.display);
    }
    for (const auto& api : status_.graphics().apis) {
        parts << api.runs_on << QString::number(api.served);
    }
    for (const auto& warning : alerts_.warnings()) {
        parts << warning.headline << warning.detail;
    }
    const auto& g = status_.graphics();
    const auto& facts = g.log_facts;
    parts << QString::number(status_.vm().running())
          << g.live_modes.value_or(QStringLiteral("-")) << QString::number(g.probed)
          << QString::number(facts.host_window_taken.value_or(false))
          << QString::number(facts.host_window_taken.has_value())
          << QString::number(facts.modes_taken) << facts.preferred_mode
          << settings_.string_list_value(K::kDisplayModes).join(QLatin1Char(','));
    const auto& vm = status_.vm();
    parts << QString::number(vm.running_device.has_value())
          << QString::number(vm.next_boot.has_value());
    if (const vm::DomainDescription* definition = vm.definition()) {
        for (const auto& device : definition->gpus) {
            parts << device.host_gpu << QString::number(device.display)
                  << QString::number(device.hostmem_mib);
        }
    }
    parts << QString::number(g.pending.host_window_mib.value_or(0))
          << QString::number(g.pending.adapters)
          << pending_.staged_or_current(K::kHostGpu).toString()
          << pending_.staged_or_current(K::kRenderGpus).toStringList().join(QLatin1Char(','))
          << pending_.staged_or_current(K::kHostWindowMib).toString()
          << QString::number(g.scale_percent.value_or(0)) << QString::number(g.live_refresh.value_or(0));
    for (const auto& gpu : g.host_gpus) {
        parts << gpu.uuid;
    }
    return parts.join(QLatin1Char('\x1f'));
}

void GraphicsPage::rebuild() {
    built_for_ = signature();
    while (auto* item = content_->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    if (auto* warnings = build_warnings()) {
        content_->addWidget(warnings);
    }
    const auto gpus = roles_->listed();
    if (gpus.isEmpty()) {
        const vm::DomainDescription* definition = status_.vm().definition();
        const auto* device = definition != nullptr ? definition->display() : nullptr;
        auto panel = card(QStringLiteral("No host GPU found"),
                          QStringLiteral("host-gpu=%1")
                              .arg(device != nullptr ? device->host_gpu : QStringLiteral("auto")));
        panel.body->addWidget(Widgets::prose(QStringLiteral(
            "The manager could not list this machine's Vulkan devices, so it cannot say which "
            "GPU the guest's adapter runs on. QEMU makes its own choice: the first Vulkan "
            "device it finds.")));
        content_->addWidget(panel.frame);
    }
    for (const auto& gpu : gpus) {
        content_->addWidget(build_adapter_card(gpu));
    }
    for (const auto& adapter : status_.graphics().adapters) {
        const bool known = std::any_of(status_.graphics().host_gpus.begin(),
                                       status_.graphics().host_gpus.end(),
                                       [&adapter](const platform::HostGpu& gpu) {
                                           return gpu.uuid == adapter.gpu.uuid;
                                       });
        if (status_.graphics().probed && !known) {
            auto panel = card(adapter.gpu.name, adapter.display ? QStringLiteral("display adapter")
                                                                : QStringLiteral("render only"));
            panel.header->addWidget(StatusPill::make(StatusPill::State::Warning,
                                                     QStringLiteral("Not on this host")));
            content_->addWidget(panel.frame);
        }
    }
    content_->addWidget(build_apis());
    content_->addWidget(build_device());
    content_->addStretch(1);
}

QWidget* GraphicsPage::build_warnings() {
    const auto warnings = alerts_.warnings();
    if (warnings.isEmpty()) {
        return nullptr;
    }
    auto* box = new QFrame;
    Theme::surface(box, Theme::Role::kPanel);
    auto* column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    for (const auto& warning : warnings) {
        auto* strip = new WarningStrip;
        strip->set_message(warning.critical, warning.headline, warning.detail);
        switch (warning.kind) {
        case Warning::Kind::NoDirect3d:
            connect(strip->add_button(QStringLiteral("Check again")), &QPushButton::clicked,
                    this, [this] { status_.refresh(Area::Host); });
            break;
        case Warning::Kind::ResizableBarOff:
            break;
        }
        column->addWidget(strip);
    }
    return box;
}

QWidget* GraphicsPage::build_adapter_card(const platform::HostGpu& gpu) {
    const auto adapters = status_.graphics().adapters;
    const auto found = std::find_if(adapters.begin(), adapters.end(), [&gpu](const Adapter& a) {
        return a.gpu.uuid == gpu.uuid;
    });
    const bool differs = roles_->differs(gpu.uuid);

    QLabel* pill = nullptr;
    if (differs) {
        pill = StatusPill::make(StatusPill::State::Pending, QStringLiteral("After restart"));
    } else if (found == adapters.end()) {
        pill = StatusPill::make(StatusPill::State::Pending, QStringLiteral("Not used"));
    }

    auto panel = card(platform::HostGpus::adapter_name(gpu), ids(gpu));
    if (differs) {
        panel.header->addWidget(Widgets::tag(true));
    }
    panel.header->addWidget(roles_->combo(gpu));
    if (pill != nullptr) {
        panel.header->addWidget(pill);
    }
    panel.body->addWidget(kpi_grid({
        {QStringLiteral("Dedicated memory"), memory(gpu)},
        {QStringLiteral("Vendor : device"), ids(gpu)},
        {QStringLiteral("Vulkan UUID"), short_uuid(gpu.uuid)},
        {QStringLiteral("Host driver"), platform::HostGraphicsProbe::driver_label(gpu)},
    }));
    return panel.frame;
}

QWidget* GraphicsPage::build_apis() const {
    auto panel = card(QStringLiteral("Host APIs"),
                      status_.graphics().adapters.size() > 1 ? QStringLiteral("served on every adapter")
                                                   : QStringLiteral("what the host serves"));
    const auto& apis = status_.graphics().apis;
    if (!status_.graphics().probed) {
        panel.body->addWidget(Widgets::meta(QStringLiteral("Asking the host…")));
        return panel.frame;
    }
    auto* table = new QTableWidget(static_cast<int>(apis.size()), 3);
    Theme::rows_table(table, {QStringLiteral("API"), QStringLiteral("THE HOST RUNS IT ON"),
                              QStringLiteral("STATE")},
                      QStringLiteral("graphics-apis"), {112, 420, 124});
    table->verticalHeader()->setDefaultSectionSize(34);
    for (int row = 0; row < apis.size(); ++row) {
        const auto& api = apis.at(row);
        table->setItem(row, 0, new ui::SortItem(api.name, std::nullopt, row));
        table->setItem(row, 1, Theme::kind_cell(api.runs_on, row));
        auto* holder = new QWidget;
        auto* line = new QHBoxLayout(holder);
        line->setContentsMargins(8, 0, 8, 0);
        QLabel* pill = api.served     ? StatusPill::make(StatusPill::State::Done,
                                                         QStringLiteral("Served"))
                       : api.required ? StatusPill::make(StatusPill::State::Failed,
                                                         QStringLiteral("Missing"))
                                      : StatusPill::make(StatusPill::State::Pending,
                                                         QStringLiteral("Not served"));
        pill->ensurePolished();
        pill->setMinimumWidth(pill->sizeHint().width());
        line->addWidget(pill, 0, Qt::AlignLeft | Qt::AlignVCenter);
        line->addStretch(1);
        table->setItem(row, 2, new ui::SortItem(QString{}, std::nullopt, row, pill->text()));
        table->setCellWidget(row, 2, holder);
    }
    Theme::fit_rows(table);
    ui::TableHeader::refresh(table->horizontalHeader());
    panel.body->addWidget(table);
    const QString d3d = platform::AccelVirtPaths::d3d_lib_dir();
    auto* note = Widgets::meta(
        (d3d.isEmpty() ? QStringLiteral("Direct3D from nowhere: set LSW_AV_D3D_PATH, or build "
                                        "accel_virt_d3d.")
                       : QStringLiteral("Direct3D from <b style=\"color:%1\">%2</b>.")
                             .arg(QString::fromLatin1(Theme::Color::kFgDim),
                                  tilde(d3d).toHtmlEscaped())),
        true);
    note->setTextFormat(Qt::RichText);
    panel.body->addWidget(note);
    return panel.frame;
}

QWidget* GraphicsPage::build_device() {
    const auto adapters = status_.graphics().adapters;
    const auto& g = status_.graphics();
    const auto& vm = status_.vm();
    const auto& facts = g.log_facts;
    const vm::DomainDescription* definition = vm.definition();
    const vm::DomainDescription::GpuDevice* device =
        definition != nullptr ? definition->display() : nullptr;
    const bool live = vm.running_device.has_value();

    const int devices = definition != nullptr ? static_cast<int>(definition->gpus.size())
                                              : static_cast<int>(adapters.size());
    auto panel = card(QStringLiteral("Device"),
                      devices > 1 ? QStringLiteral("virtio-gpu-av-pci × %1").arg(devices)
                                  : QStringLiteral("virtio-gpu-av-pci"));
    KeyValues rows(panel.body);

    if (device == nullptr) {
        rows.add(QStringLiteral("Host window"), QStringLiteral("-"), Theme::Tone::kFaint,
                 QStringLiteral("no machine is defined"));
    } else {
        QString window_note = QStringLiteral("write-back, used on demand");
        if (live && facts.host_window_taken.has_value()) {
            window_note = *facts.host_window_taken
                              ? QStringLiteral("write-back, taken by the driver")
                              : QStringLiteral("refused by the driver; chunks fall back to guest RAM");
        }
        auto* holder = new QWidget;
        auto* line = new QHBoxLayout(holder);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(8);
        auto* window = controls_->choice(K::kHostWindowMib,
                                         {{QStringLiteral("2 GiB"), 2048},
                                          {QStringLiteral("4 GiB"), 4096},
                                          {QStringLiteral("8 GiB"), 8192},
                                          {QStringLiteral("16 GiB"), 16384}});
        line->addWidget(window);
        line->addWidget(Widgets::tag(true));
        line->addWidget(Widgets::meta(window_note, true), 1);
        rows.add_widget(QStringLiteral("Host window"), holder);
    }

    const DisplayFacts display = DisplayFacts::read(status_);
    rows.add(QStringLiteral("Display"),
             display.mode ? QStringLiteral("%1 at %2").arg(display.mode_label(), display.refresh_label())
                          : display.mode_label(),
             nullptr, QStringLiteral("set on the Display page"));

    int render = 0;
    for (const auto& adapter : adapters) {
        render += adapter.display ? 0 : 1;
    }
    QString adapters_note = render == 0 ? QStringLiteral("the display only")
                                        : QStringLiteral("1 display, %1 render only").arg(render);
    if (g.pending.adapters) {
        const int staged_render = roles_->staged_render_count();
        adapters_note += staged_render == 0
                             ? QStringLiteral("; the display only after a restart")
                             : QStringLiteral("; 1 display, %1 render only after a restart")
                                   .arg(staged_render);
    }
    rows.add(QStringLiteral("Adapters"), QString::number(adapters.size()),
             g.pending.adapters ? Theme::Tone::kAmber : nullptr, adapters_note);
    panel.body->addStretch(1);
    return panel.frame;
}

}
