#include "lsw/host/manager/graphics_page.hpp"

#include "lsw/host/config/display_modes.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/manager/graphics_alerts.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/manager/warning_strip.hpp"
#include "lsw/host/platform/accel_virt_paths.hpp"
#include "lsw/host/ui/format.hpp"
#include "lsw/host/ui/panel.hpp"
#include "lsw/host/ui/status_pill.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTableWidget>
#include <QUrl>
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

// How many of the driver's last lines the panel shows.
constexpr int kLogLines = 6;

// "5f2c91d0…e83a": enough of a UUID to tell two apart and to find it
// in the settings or in vulkaninfo.
QString short_uuid(const QString& uuid) {
    if (uuid.size() < 16) {
        return uuid;
    }
    const QString ends = uuid.left(8) + uuid.right(4);
    // RADV builds its UUIDs from the PCI address, zeros either side of
    // the bus; the ends then say nothing, and the middle says it all.
    if (ends.count(QLatin1Char('0')) == ends.size()) {
        return QStringLiteral("…%1…").arg(uuid.mid(9, 9));
    }
    return uuid.left(8) + QStringLiteral("…") + uuid.right(4);
}

// "1002:7550".
QString ids(const platform::HostGpu& gpu) {
    return QStringLiteral("%1:%2")
        .arg(gpu.vendor_id, 4, 16, QLatin1Char('0'))
        .arg(gpu.device_id, 4, 16, QLatin1Char('0'));
}

// An integrated GPU's device-local heap is system memory; there is no
// figure of its own to give.
QString memory(const platform::HostGpu& gpu) {
    if (gpu.vram_bytes == 0) {
        return QStringLiteral("shared");
    }
    return Format::gib(static_cast<qint64>(gpu.vram_bytes), 0);
}

// The card's model without the vendor's words, for a row label:
// "AMD Radeon RX 9070 XT" is "RX 9070 XT".
QString short_name(const QString& name) {
    static const QStringList vendor_words = {
        QStringLiteral("AMD"),    QStringLiteral("Radeon"),   QStringLiteral("Intel"),
        QStringLiteral("Intel(R)"), QStringLiteral("NVIDIA"), QStringLiteral("GeForce"),
        QStringLiteral("Graphics"), QStringLiteral("(TM)"),   QStringLiteral("Mesa")};
    QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    words.removeIf([](const QString& w) {
        return vendor_words.contains(w) || w == QLatin1StringView("Processor")
               || w.endsWith(QLatin1StringView("-Core"));
    });
    // A name in brackets after the model, "(RADV NAVI48)", is the driver's.
    const qsizetype bracket = words.indexOf(QRegularExpression(QStringLiteral("^\\(.*")));
    if (bracket > 0) {
        words = words.mid(0, bracket);
    }
    QString out = words.isEmpty() ? name : words.join(QLatin1Char(' '));
    // A row label, not a title: past a dozen characters it pushes the
    // values off the card.
    constexpr qsizetype kMost = 12;
    if (out.size() > kMost) {
        out = out.left(kMost - 1) + QStringLiteral("…");
    }
    return out;
}

// A labelled row in a card's header: the name, a mono caption, and
// whatever sits at the right.
ui::Panel card(const QString& name, const QString& caption, QWidget* right = nullptr) {
    auto panel = ui::Panel::make();
    panel.header->setContentsMargins(12, 8, 12, 8);
    panel.header->setSpacing(10);
    auto* title = new QLabel(name);
    title->setObjectName(QString::fromLatin1(Theme::Role::kCardName));
    panel.header->addWidget(title);
    panel.header->addWidget(Widgets::meta(caption));
    panel.header->addStretch(1);
    if (right != nullptr) {
        panel.header->addWidget(right);
    }
    return panel;
}

// Figures side by side, a hairline between each.
QWidget* kpi_grid(const QVector<QPair<QString, QString>>& figures) {
    auto* grid = new QFrame;
    Theme::surface(grid, Theme::Role::kKpiGrid);
    auto* row = new QHBoxLayout(grid);
    row->setContentsMargins(1, 1, 1, 1);
    row->setSpacing(1);
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
        // Clipped rather than widening the page: the tooltip has it all.
        figure->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        figure->setMinimumWidth(0);
        column->addWidget(figure);
        row->addWidget(tile, 1);
    }
    return grid;
}

// A key and its value down a card, the way a definition list reads:
// mono caption, the value in its colour, and a faint note after it.
class KeyValues {
public:
    explicit KeyValues(QVBoxLayout* body) : grid_(new QGridLayout) {
        grid_->setHorizontalSpacing(16);
        grid_->setVerticalSpacing(9);
        grid_->setColumnStretch(1, 1);
        body->addLayout(grid_);
    }

    QHBoxLayout* add(const QString& key, const QString& value, const char* tone = nullptr,
                     const QString& note = {}) {
        auto* caption = Widgets::caption(key);
        auto* line = new QHBoxLayout;
        line->setSpacing(6);
        // Nothing known but a reason: the reason is the row. A lone "-"
        // in front of it reads as a bullet point, not as "unknown".
        if (value == QLatin1String("-") && !note.isEmpty()) {
            line->addWidget(Widgets::meta(note, true), 1);
            grid_->addWidget(caption, row_, 0, Qt::AlignLeft | Qt::AlignVCenter);
            grid_->addLayout(line, row_, 1);
            ++row_;
            return line;
        }
        auto* shown = new QLabel(value);
        if (tone != nullptr) {
            shown->setProperty(Theme::Tone::kProperty, QString::fromLatin1(tone));
        }
        line->addWidget(shown);
        if (!note.isEmpty()) {
            // Wrapping, so two cards side by side fit the page however
            // long a note runs.
            line->addWidget(Widgets::meta(note, true), 1);
        } else {
            line->addStretch(1);
        }
        grid_->addWidget(caption, row_, 0, Qt::AlignLeft | Qt::AlignVCenter);
        grid_->addLayout(line, row_, 1);
        ++row_;
        return line;
    }

private:
    QGridLayout* grid_;
    int row_ = 0;
};

// Windows' side of one adapter, as the pill on its card says it.
QLabel* adapter_pill(const services::StatusHub& status, const Adapter& adapter) {
    if (!status.vm().running()) {
        return StatusPill::make(StatusPill::State::Pending, QStringLiteral("Guest off"));
    }
    if (!status.graphics().guest.valid) {
        return StatusPill::make(StatusPill::State::Pending, QStringLiteral("Waiting for the agent"));
    }
    if (!adapter.guest) {
        return StatusPill::make(StatusPill::State::Failed, QStringLiteral("Not in Windows"));
    }
    if (adapter.guest->running()) {
        return StatusPill::make(StatusPill::State::Done, QStringLiteral("Started"));
    }
    if (adapter.guest->no_driver()) {
        return StatusPill::make(StatusPill::State::Warning, QStringLiteral("No driver"));
    }
    return StatusPill::make(StatusPill::State::Failed,
                            QStringLiteral("Problem code %1").arg(adapter.guest->problem_code));
}

// The host window, "8 GiB".
QString window_size(int mib) {
    return mib % 1024 == 0 ? QStringLiteral("%1 GiB").arg(mib / 1024)
                           : QStringLiteral("%1 MiB").arg(mib);
}

// A path under the home directory as people write it: "~/.local/...".
QString tilde(const QString& path) {
    const QString home = QDir::homePath();
    if (path.startsWith(home + QLatin1Char('/'))) {
        return QStringLiteral("~") + path.mid(home.size());
    }
    return path;
}

}  // namespace

GraphicsPage::GraphicsPage(services::StatusHub& status, GraphicsAlerts& alerts,
                           config::ManagerSettings& settings, QWidget* parent)
    : QWidget(parent), status_(status), alerts_(alerts), settings_(settings) {
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
    const auto adapters = status_.graphics().adapters;
    QStringList parts{QStringLiteral("accel-virt")};
    parts << (adapters.size() == 1 ? QStringLiteral("1 adapter")
                                   : QStringLiteral("%1 adapters").arg(adapters.size()));
    if (status_.vm().running() && status_.graphics().guest.valid) {
        int started = 0;
        for (const auto& adapter : adapters) {
            started += adapter.guest && adapter.guest->running() ? 1 : 0;
        }
        if (started == adapters.size() && started > 0) {
            parts << QStringLiteral("avgpu.sys started");
        } else if (started == 0) {
            parts << QStringLiteral("avgpu.sys not running");
        } else {
            parts << QStringLiteral("avgpu.sys started on %1").arg(started);
        }
    }
    return parts.join(QStringLiteral(" · "));
}

QStringList GraphicsPage::status_facts() const {
    // The running machine's devices, from its live definition; with the
    // machine off, the stored one the next start boots. The settings
    // appear only as what a restart would change.
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
        facts << QStringLiteral("hostmem %1").arg(window_size(device->hostmem_mib));
    }
    const QString preferred = g.live_modes.value_or(QString{}).section(QLatin1Char(','), 0, 0);
    if (const auto mode = config::DisplayModes::parse(preferred)) {
        facts << QStringLiteral("display %1, %2 Hz")
                     .arg(mode->label())
                     .arg(config::DisplayModes::kRefreshHz);
    } else if (status_.vm().running()) {
        facts << QStringLiteral("display host monitors' modes");
    }
    if (g.pending.any()) {
        QStringList what;
        if (g.pending.host_window_mib) {
            what << QStringLiteral("hostmem %1").arg(window_size(*g.pending.host_window_mib));
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
    update_log();
}

QString GraphicsPage::signature() const {
    QStringList parts;
    for (const auto& adapter : status_.graphics().adapters) {
        parts << adapter.gpu.uuid << QString::number(adapter.display);
        if (adapter.guest) {
            parts << adapter.guest->name << QString::number(adapter.guest->problem_code)
                  << QString::number(adapter.guest->started) << adapter.guest->driver_version;
        }
    }
    for (const auto& api : status_.graphics().apis) {
        parts << api.runs_on << QString::number(api.served);
    }
    for (const auto& warning : alerts_.warnings()) {
        parts << warning.headline << warning.detail;
    }
    const auto& g = status_.graphics();
    const auto& guest = g.guest;
    const auto& facts = g.log_facts;
    parts << QString::number(status_.vm().running()) << QString::number(guest.valid)
          << QString::number(guest.test_signing) << QString::number(guest.driver_search_off)
          << g.live_modes.value_or(QStringLiteral("-")) << QString::number(g.probed)
          << QString::number(facts.host_window_taken.value_or(false))
          << QString::number(facts.host_window_taken.has_value())
          << QString::number(facts.user_mode_submission.value_or(false))
          << QString::number(facts.native_fences.value_or(false))
          << QString::number(facts.modes_taken) << facts.preferred_mode
          << QString::number(guest.user_mode_submission.value_or(false))
          << QString::number(guest.user_mode_submission.has_value())
          << QString::number(guest.native_fences.value_or(false))
          << QString::number(guest.native_fences.has_value())
          << settings_.string_list_value(K::kDisplayModes).join(QLatin1Char(','));
    // The definition the Device card reads, and what a restart would
    // change of it.
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
          << QString::number(g.pending.adapters);
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
    log_meta_ = nullptr;
    log_text_ = nullptr;

    if (auto* warnings = build_warnings()) {
        content_->addWidget(warnings);
    }
    const auto adapters = status_.graphics().adapters;
    if (adapters.isEmpty()) {
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
    for (int i = 0; i < adapters.size(); ++i) {
        content_->addWidget(build_adapter_card(i));
    }
    content_->addWidget(build_apis());

    auto* pair = new QWidget;
    auto* pair_row = new QHBoxLayout(pair);
    pair_row->setContentsMargins(0, 0, 0, 0);
    pair_row->setSpacing(12);
    pair_row->addWidget(build_device(), 1);
    pair_row->addWidget(build_guest_driver(), 1);
    content_->addWidget(pair);

    content_->addWidget(build_log());
    content_->addStretch(1);
    update_log();
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
        case Warning::Kind::DisplayNotRunning:
            connect(strip->add_button(QStringLiteral("Open the driver log")),
                    &QPushButton::clicked, this, &GraphicsPage::open_log);
            break;
        case Warning::Kind::DisplayNoDriver:
            connect(strip->add_button(QStringLiteral("Open the driver log")),
                    &QPushButton::clicked, this, &GraphicsPage::open_log);
            connect(strip->add_button(QStringLiteral("Install the driver for new adapters")),
                    &QPushButton::clicked, &alerts_, &GraphicsAlerts::install_driver);
            break;
        case Warning::Kind::NoDriver:
        case Warning::Kind::InstallFailed:
            connect(strip->add_button(QStringLiteral("Install the driver for new adapters")),
                    &QPushButton::clicked, &alerts_, &GraphicsAlerts::install_driver);
            break;
        case Warning::Kind::DriverUpdate:
        case Warning::Kind::UpdateFailed:
            connect(strip->add_button(QStringLiteral("Update the driver")),
                    &QPushButton::clicked, &alerts_, &GraphicsAlerts::update_driver);
            break;
        case Warning::Kind::InstallFinished:
            connect(strip->add_button(QStringLiteral("Restart the guest")),
                    &QPushButton::clicked, this, &GraphicsPage::restart_requested);
            break;
        case Warning::Kind::Installing:
            break;
        }
        column->addWidget(strip);
    }
    return box;
}

QWidget* GraphicsPage::build_adapter_card(int index) const {
    const auto adapters = status_.graphics().adapters;
    const Adapter& adapter = adapters.at(index);
    auto panel = card(platform::HostGpus::adapter_name(adapter.gpu),
                      adapter.display ? QStringLiteral("display adapter")
                                      : QStringLiteral("render only, no outputs"),
                      adapter_pill(status_, adapter));
    panel.body->addWidget(kpi_grid({
        {QStringLiteral("Dedicated memory"), memory(adapter.gpu)},
        {QStringLiteral("Vendor : device"), ids(adapter.gpu)},
        {QStringLiteral("Vulkan UUID"), short_uuid(adapter.gpu.uuid)},
        {QStringLiteral("Host driver"), platform::HostGraphicsProbe::driver_label(adapter.gpu)},
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
                              QStringLiteral("STATE")});
    auto* head = table->horizontalHeader();
    head->setSectionResizeMode(0, QHeaderView::Fixed);
    head->setSectionResizeMode(1, QHeaderView::Stretch);
    head->setSectionResizeMode(2, QHeaderView::Fixed);
    table->setColumnWidth(0, 112);
    table->setColumnWidth(2, 124);
    table->verticalHeader()->setDefaultSectionSize(34);
    for (int row = 0; row < apis.size(); ++row) {
        const auto& api = apis.at(row);
        table->setItem(row, 0, new QTableWidgetItem(api.name));
        table->setItem(row, 1, Theme::kind_cell(api.runs_on));
        // Missing in red only where Windows cannot do without it.
        auto* holder = new QWidget;
        auto* line = new QHBoxLayout(holder);
        line->setContentsMargins(8, 0, 8, 0);
        QLabel* pill = api.served     ? StatusPill::make(StatusPill::State::Done,
                                                         QStringLiteral("Served"))
                       : api.required ? StatusPill::make(StatusPill::State::Failed,
                                                         QStringLiteral("Missing"))
                                      : StatusPill::make(StatusPill::State::Pending,
                                                         QStringLiteral("Not served"));
        // Measured in the stylesheet's face, not the default one it has
        // before it is shown, or the word is cut short.
        pill->ensurePolished();
        pill->setMinimumWidth(pill->sizeHint().width());
        line->addWidget(pill, 0, Qt::AlignLeft | Qt::AlignVCenter);
        line->addStretch(1);
        table->setCellWidget(row, 2, holder);
    }
    Theme::fit_rows(table);
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
    // What the machine has: the running VM's live definition, or the
    // stored one the next start boots. Settings that differ from it are
    // pending, and said so beside the value they will replace.
    const vm::DomainDescription* definition = vm.definition();
    const vm::DomainDescription::GpuDevice* device =
        definition != nullptr ? definition->display() : nullptr;
    const bool live = vm.running_device.has_value();

    // What the device offers now, if QEMU said; what the settings will
    // offer at the next boot (the window pushes them to the device the
    // moment it is up), if not.
    QStringList offered;
    if (g.live_modes.has_value()) {
        offered = config::DisplayModes::normalized(
            g.live_modes->split(QLatin1Char(','), Qt::SkipEmptyParts));
    } else {
        offered = config::DisplayModes::normalized(settings_.string_list_value(K::kDisplayModes));
    }

    // A quick switch among the sizes offered, in the card's header where
    // it has room: picking one makes it the preferred one, in the
    // settings too, and the window pushes that to the running device.
    QComboBox* pick = nullptr;
    if (offered.size() > 1) {
        pick = new QComboBox;
        pick->addItem(QStringLiteral("Switch resolution"));
        for (const QString& entry : offered.mid(1)) {
            if (const auto m = config::DisplayModes::parse(entry)) {
                pick->addItem(m->label(), entry);
            }
        }
        pick->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        connect(pick, &QComboBox::activated, this, [this, pick](int index) {
            const QString chosen = pick->itemData(index).toString();
            if (chosen.isEmpty()) {
                return;
            }
            QStringList list = config::DisplayModes::normalized(
                settings_.string_list_value(K::kDisplayModes));
            list.removeAll(chosen);
            list.prepend(chosen);
            settings_.set_value(K::kDisplayModes, list);
        });
    }
    const int devices = definition != nullptr ? static_cast<int>(definition->gpus.size())
                                              : static_cast<int>(adapters.size());
    auto panel = card(QStringLiteral("Device"),
                      devices > 1 ? QStringLiteral("virtio-gpu-av-pci × %1").arg(devices)
                                  : QStringLiteral("virtio-gpu-av-pci"),
                      pick);
    KeyValues rows(panel.body);

    if (device == nullptr) {
        rows.add(QStringLiteral("Host window"), QStringLiteral("-"), Theme::Tone::kFaint,
                 QStringLiteral("no machine is defined"));
    } else {
        QString window_note = live ? QStringLiteral("write-back")
                                   : QStringLiteral("write-back, from the next start");
        const char* window_tone = nullptr;
        if (live && facts.host_window_taken.has_value()) {
            window_note = *facts.host_window_taken
                              ? QStringLiteral("write-back, taken by the driver")
                              : QStringLiteral("refused by the driver; chunks fall back to guest RAM");
            window_tone = *facts.host_window_taken ? nullptr : Theme::Tone::kAmber;
        }
        if (g.pending.host_window_mib) {
            window_note += QStringLiteral("; %1 after a restart")
                               .arg(window_size(*g.pending.host_window_mib));
            window_tone = Theme::Tone::kAmber;
        }
        rows.add(QStringLiteral("Host window"),
                 device->hostmem_mib > 0 ? window_size(device->hostmem_mib) : QStringLiteral("-"),
                 window_tone, window_note);
    }

    QString preferred = offered.value(0);
    if (preferred.isEmpty()) {
        preferred = facts.preferred_mode;
    }
    const int count = !offered.isEmpty() ? static_cast<int>(offered.size()) : facts.modes_taken;
    const auto mode = config::DisplayModes::parse(preferred);
    rows.add(
        QStringLiteral("Display"),
        mode ? QStringLiteral("%1 at %2 Hz").arg(mode->label()).arg(config::DisplayModes::kRefreshHz)
             : QStringLiteral("Host monitors' modes"),
        nullptr,
        count <= 0 ? QString{}
        // Live from the device while it runs; otherwise what the settings
        // will offer at the next boot, and it says so.
        : g.live_modes.has_value() || facts.modes_taken > 0
            ? QStringLiteral("%1 mode%2 offered").arg(count).arg(count == 1 ? "" : "s")
            : QStringLiteral("%1 mode%2 at boot").arg(count).arg(count == 1 ? "" : "s"));

    int render = 0;
    for (const auto& adapter : adapters) {
        render += adapter.display ? 0 : 1;
    }
    QString adapters_note = render == 0 ? QStringLiteral("the display only")
                                        : QStringLiteral("1 display, %1 render only").arg(render);
    if (g.pending.adapters) {
        adapters_note += QStringLiteral("; the settings pass a different set after a restart");
    }
    rows.add(QStringLiteral("Adapters"), QString::number(adapters.size()),
             g.pending.adapters ? Theme::Tone::kAmber : nullptr, adapters_note);
    panel.body->addStretch(1);
    return panel.frame;
}

QWidget* GraphicsPage::build_guest_driver() const {
    const auto adapters = status_.graphics().adapters;
    const auto& guest = status_.graphics().guest;
    QString version;
    if (!adapters.isEmpty() && adapters.first().guest) {
        version = adapters.first().guest->driver_version;
    }
    int started = 0;
    for (const auto& adapter : adapters) {
        started += adapter.guest && adapter.guest->running() ? 1 : 0;
    }
    QLabel* pill = nullptr;
    if (!status_.vm().running()) {
        pill = StatusPill::make(StatusPill::State::Pending, QStringLiteral("Guest off"));
    } else if (!guest.valid) {
        pill = StatusPill::make(StatusPill::State::Pending, QStringLiteral("Unknown"));
    } else if (started == adapters.size() && started > 0) {
        pill = StatusPill::make(StatusPill::State::Done, QStringLiteral("Started"));
    } else if (started == 0) {
        pill = StatusPill::make(StatusPill::State::Failed, QStringLiteral("Not running"));
    } else {
        pill = StatusPill::make(StatusPill::State::Warning,
                                QStringLiteral("%1 of %2").arg(started).arg(adapters.size()));
    }
    auto panel = card(QStringLiteral("Guest driver"),
                      version.isEmpty() ? QStringLiteral("avgpu.sys")
                                        : QStringLiteral("avgpu.sys %1").arg(version),
                      pill);
    KeyValues rows(panel.body);

    const bool known = status_.vm().running() && guest.valid;
    const QString off_note = status_.vm().running() ? QStringLiteral("the agent has not answered")
                                              : QStringLiteral("the guest is off");
    // One line per adapter; with only one, it is simply "the device".
    for (const auto& adapter : adapters) {
        const QString key = adapters.size() == 1
                                ? QStringLiteral("Device")
                                : short_name(platform::HostGpus::adapter_name(adapter.gpu));
        if (!known) {
            rows.add(key, QStringLiteral("-"), Theme::Tone::kFaint, off_note);
        } else if (!adapter.guest) {
            rows.add(key, QStringLiteral("Absent"), Theme::Tone::kBad,
                     QStringLiteral("Windows lists no adapter for it"));
        } else if (adapter.guest->running()) {
            rows.add(key, QStringLiteral("Running"), Theme::Tone::kOk,
                     QStringLiteral("no problem code"));
        } else if (adapter.guest->no_driver()) {
            rows.add(key, QStringLiteral("No driver"), Theme::Tone::kAmber,
                     QStringLiteral("problem code %1").arg(adapter.guest->problem_code));
        } else {
            rows.add(key, QStringLiteral("Problem"), Theme::Tone::kBad,
                     QStringLiteral("problem code %1").arg(adapter.guest->problem_code));
        }
    }
    if (!known) {
        rows.add(QStringLiteral("Test signing"), QStringLiteral("-"), Theme::Tone::kFaint,
                 QStringLiteral("the driver needs it"));
        rows.add(QStringLiteral("Driver search"), QStringLiteral("-"), Theme::Tone::kFaint,
                 QStringLiteral("must be off"));
    } else {
        rows.add(QStringLiteral("Test signing"),
                 guest.test_signing ? QStringLiteral("On") : QStringLiteral("Off"),
                 guest.test_signing ? Theme::Tone::kOk : Theme::Tone::kBad,
                 QStringLiteral("the driver needs it"));
        rows.add(QStringLiteral("Driver search"),
                 guest.driver_search_off ? QStringLiteral("Off") : QStringLiteral("On"),
                 guest.driver_search_off ? Theme::Tone::kOk : Theme::Tone::kBad,
                 guest.driver_search_off ? QStringLiteral("Update cannot replace it")
                                         : QStringLiteral("Update may replace it"));
    }
    // From the driver's own lines when it has said; the policy
    // provisioning wrote otherwise.
    const auto& facts = status_.graphics().log_facts;
    const std::optional<bool> ums =
        facts.user_mode_submission.has_value() ? facts.user_mode_submission
                                               : guest.user_mode_submission;
    const std::optional<bool> fences =
        facts.native_fences.has_value() ? facts.native_fences : guest.native_fences;
    if (!ums.has_value()) {
        rows.add(QStringLiteral("Submission"), QStringLiteral("-"), Theme::Tone::kFaint,
                 QStringLiteral("not reported yet"));
    } else {
        rows.add(QStringLiteral("Submission"),
                 *ums ? QStringLiteral("User mode") : QStringLiteral("Kernel"), nullptr,
                 fences.has_value()
                     ? (*fences ? QStringLiteral("native fences on")
                                : QStringLiteral("native fences off"))
                     : QString{});
    }
    panel.body->addStretch(1);
    return panel.frame;
}

QWidget* GraphicsPage::build_log() {
    auto panel = ui::Panel::make(QStringLiteral("Driver log"));
    panel.header->setContentsMargins(12, 6, 8, 6);
    panel.header->setSpacing(10);
    log_meta_ = Widgets::meta(QString{});
    panel.header->addWidget(log_meta_);
    panel.header->addStretch(1);
    auto* open = Widgets::button(QStringLiteral("Open the full log"), Theme::Role::kQuiet);
    connect(open, &QPushButton::clicked, this, &GraphicsPage::open_log);
    panel.header->addWidget(open);
    panel.body->setContentsMargins(0, 0, 0, 0);
    log_text_ = new QLabel;
    log_text_->setObjectName(QString::fromLatin1(Theme::Role::kTerm));
    log_text_->setTextFormat(Qt::RichText);
    log_text_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    log_text_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    log_text_->setMinimumHeight(kLogLines * 19 + 18);
    panel.body->addWidget(log_text_);
    return panel.frame;
}

void GraphicsPage::update_log() {
    if (log_text_ == nullptr) {
        return;
    }
    const auto& g = status_.graphics();
    log_meta_->setText(QStringLiteral("%1 · %2 lines%3")
                           .arg(QFileInfo(g.log_path).fileName())
                           .arg(QLocale().toString(g.log_line_count))
                           .arg(status_.vm().running() ? QStringLiteral(" · live") : QString{}));
    const QStringList& lines = g.log_lines;
    if (lines.isEmpty()) {
        log_text_->setText(
            status_.vm().running()
                ? QStringLiteral("Nothing from the driver yet.")
                : QStringLiteral("Nothing yet. The driver writes here from its first line on "
                                 "the next boot."));
        return;
    }
    QStringList shown;
    const qsizetype from = qMax<qsizetype>(0, lines.size() - kLogLines);
    for (qsizetype i = from; i < lines.size(); ++i) {
        const QString text = lines.at(i).toHtmlEscaped();
        // The newest line in the foreground: it is the one that changes.
        shown << (i + 1 == lines.size()
                      ? QStringLiteral("<span style=\"color:%1\">%2</span>")
                            .arg(QString::fromLatin1(Theme::Color::kFg), text)
                      : text);
    }
    log_text_->setText(shown.join(QStringLiteral("<br>")));
}

void GraphicsPage::open_log() const {
    QDesktopServices::openUrl(QUrl::fromLocalFile(status_.graphics().log_path));
}

}  // namespace lsw::host::manager
