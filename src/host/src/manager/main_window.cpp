#include "lsw/host/manager/main_window.hpp"

#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/manager/console_panel.hpp"
#include "lsw/host/manager/graphics_alerts.hpp"
#include "lsw/host/manager/graphics_page.hpp"
#include "lsw/host/manager/log_panel.hpp"
#include "lsw/host/manager/notice_board.hpp"
#include "lsw/host/manager/run_on_vm_dialog.hpp"
#include "lsw/host/manager/settings_page.hpp"
#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/apps/guest_app_launcher.hpp"
#include "lsw/host/manager/vm_detail_panel.hpp"
#include "lsw/host/manager/warning_strip.hpp"
#include "lsw/host/mirror/window_mirror.hpp"
#include "lsw/host/platform/guest_smb_share.hpp"
#include "lsw/host/platform/host_scale.hpp"
#include "lsw/host/services/domain_inputs.hpp"
#include "lsw/host/services/host_services.hpp"
#include "lsw/host/tools/download_image_dialog.hpp"
#include "lsw/host/tools/trim_image_dialog.hpp"
#include "lsw/host/ui/format.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"
#include "lsw/host/vm/domain_xml_inputs.hpp"
#include "lsw/host/vm/graceful_shutdown.hpp"
#include "lsw/linkproto/commands.hpp"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QApplication>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>
#include <nlohmann/json.hpp>

namespace lsw::host::manager {

namespace {

using ui::Theme;
using ui::Widgets;
using K = config::ManagerSettings::Key;

struct PageInfo {
    const char* title;
    const char* detail;
};

// The details that are not status are descriptions of the page; the
// rest come from the status hub (see MainWindow::page_detail).
const PageInfo kPages[] = {
    {"Overview", ""}, {"Console", ""}, {"Log", ""}, {"Graphics", ""}, {"Settings", ""},
};

// A path under the home directory as people write it: "~/.config/...".
QString tilde(const QString& path) {
    const QString home = QDir::homePath();
    if (path.startsWith(home + QLatin1Char('/'))) {
        return QStringLiteral("~") + path.mid(home.size());
    }
    return path;
}

}  // namespace

MainWindow::MainWindow(services::HostServices& services, QWidget* parent)
    : QMainWindow(parent), services_(services) {
    setWindowTitle(QStringLiteral("LSW"));
    resize(1000, 620);

    auto* central = new QWidget;
    auto* columns = new QHBoxLayout(central);
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(0);

    rail_ = new NavRail;
    columns->addWidget(rail_);

    auto* right = new QWidget;
    auto* right_layout = new QVBoxLayout(right);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(0);

    // ---- content bar ----------------------------------------------
    auto* bar = new QWidget;
    Theme::surface(bar, Theme::Role::kContentBar);
    auto* bar_layout = new QHBoxLayout(bar);
    bar_layout->setContentsMargins(14, 9, 14, 9);
    bar_layout->setSpacing(10);
    content_title_ = new QLabel;
    QFont title_font = content_title_->font();
    title_font.setPixelSize(14);
    title_font.setWeight(QFont::DemiBold);
    content_title_->setFont(title_font);
    content_detail_ = Widgets::meta(QString{});
    bar_layout->addWidget(content_title_);
    bar_layout->addWidget(content_detail_);
    bar_layout->addStretch(1);
    // Console and Log are pages now, which is right for the common
    // case and wrong for the one where you want to watch the log while
    // doing something else. So they can still be a window.
    const auto bar_action = [&](const QString& text) {
        auto* button = Widgets::button(text, Theme::Role::kQuiet);
        button->setVisible(false);
        bar_layout->addWidget(button);
        return button;
    };
    desktop_button_ = bar_action(QStringLiteral("Open desktop"));
    run_on_vm_button_ = bar_action(QStringLiteral("Run on VM…"));
    clear_button_ = bar_action(QStringLiteral("Clear"));
    change_settings_button_ = bar_action(QStringLiteral("Change in Settings"));
    config_folder_button_ = bar_action(QStringLiteral("Open config folder"));
    pop_out_button_ = bar_action(QStringLiteral("Pop out"));

    connect(pop_out_button_, &QPushButton::clicked, this, [this] { pop_out(rail_->current()); });
    connect(desktop_button_, &QPushButton::clicked, this, &MainWindow::open_desktop);
    connect(run_on_vm_button_, &QPushButton::clicked, this, [this] {
        if (!run_on_vm_dialog_) {
            run_on_vm_dialog_ = new RunOnVmDialog(services_.rpc(), services_.app_launcher(), this);
            run_on_vm_dialog_->set_agent_online(overview_->is_agent_online());
        }
        run_on_vm_dialog_->show();
        run_on_vm_dialog_->raise();
        run_on_vm_dialog_->activateWindow();
    });
    connect(clear_button_, &QPushButton::clicked, this, [this] {
        if (rail_->current() == NavRail::Page::Console) {
            if (console_ != nullptr) {
                console_->clear_transcript();
            }
            return;
        }
        services_.log_store().clear();
    });
    connect(change_settings_button_, &QPushButton::clicked, this,
            [this] { show_page(NavRail::Page::Settings); });
    connect(config_folder_button_, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(services_.settings().config_dir()));
    });
    right_layout->addWidget(bar);

    // ---- warning strip --------------------------------------------
    // The overview's copy of the two Graphics warnings that break the
    // guest. Everything else the Graphics page says stays there.
    warn_strip_ = new WarningStrip;
    warn_strip_->setVisible(false);
    right_layout->addWidget(warn_strip_);
    notices_ = new NoticeBoard;
    right_layout->addWidget(notices_);

    stack_ = new QStackedWidget;
    right_layout->addWidget(stack_, 1);

    // ---- status strip ---------------------------------------------
    // Under the content rather than across the whole window: the rail
    // has its own foot, and a bar that runs beneath it reads as
    // belonging to neither.
    auto* strip = new QWidget;
    Theme::surface(strip, Theme::Role::kStatusStrip);
    auto* strip_layout = new QHBoxLayout(strip);
    strip_layout->setContentsMargins(14, 5, 14, 5);
    status_left_ = Widgets::meta(QString{});
    strip_layout->addWidget(status_left_, 1);
    right_layout->addWidget(strip);

    columns->addWidget(right, 1);
    setCentralWidget(central);

    build_pages();

    connect(rail_, &NavRail::page_selected, this, &MainWindow::show_page);
    follow_launches();
    connect(rail_, &NavRail::start_requested, this, [this] {
        if (!overview_->start_guest()) {
            notice_start_refused(QStringLiteral("libvirt would not start the guest."));
        }
    });
    connect(rail_, &NavRail::stop_requested, overview_, &VmDetailPanel::stop_guest);

    connect(overview_, &VmDetailPanel::running_changed, this, [this](bool running) {
        rail_->set_running(running);
        // The device takes the resolutions the moment QEMU is up; the
        // definition may carry an older list than the settings now hold.
        if (running) {
            notices_->withdraw(QStringLiteral("start"));
            push_display_modes();
        } else if (restart_ == nullptr) {
            notice_.clear();
        }
    });
    follow_agent_for_desktop(overview_->is_agent_online());
    connect(overview_, &VmDetailPanel::launch_requested, this, &MainWindow::launch_requested);
    connect(overview_, &VmDetailPanel::agent_online_changed, this, [this](bool online) {
        rail_->set_agent_online(online);
        follow_agent_for_desktop(online);
        if (online) {
            // A restart is over once Windows answers again.
            if (restart_ == nullptr) {
                notice_.clear();
            }
            push_dpi();
        }
    });
    // Every reading of the machine or its graphics: the status bar, the
    // page's detail and the rail's identity are all drawn from it.
    connect(&services_.status(), &services::StatusHub::changed, this, [this] {
        refresh_status_bar();
        content_detail_->setText(page_detail(rail_->current()));
        refresh_identity();
    });
    connect(console_, &ConsolePanel::open_failed, this, [this](const QString& reason) {
        notices_->post(QStringLiteral("console"), false, QStringLiteral("Could not open a console."),
                       reason);
    });
    connect(console_, &ConsolePanel::opened, this,
            [this] { notices_->withdraw(QStringLiteral("console")); });
    connect(console_, &ConsolePanel::summary_changed, this, [this] {
        if (rail_->current() == NavRail::Page::Console) {
            content_detail_->setText(page_detail(NavRail::Page::Console));
        }
    });
    connect(log_, &LogPanel::record_count_changed, rail_, &NavRail::set_log_count);
    rail_->set_log_count(log_->shown());
    connect(&services_.pending_changes(), &config::PendingChanges::changed, this,
            [this] { rail_->set_pending_changes(services_.pending_changes().count()); });
    rail_->set_pending_changes(services_.pending_changes().count());
    connect(alerts_, &GraphicsAlerts::changed, this, &MainWindow::on_graphics_changed);
    connect(graphics_page_, &GraphicsPage::summary_changed, this, &MainWindow::on_graphics_changed);
    // The display settings are live: each change goes to the running
    // device at once, and the DPI to the agent.
    connect(&services_.manager_settings(), &config::ManagerSettings::changed, this,
            [this](const QString& key) {
                if (key == K::kDisplayModes || key == K::kMatchHostOutput) {
                    push_display_modes();
                } else if (key == K::kScalingPercent) {
                    push_dpi();
                }
                if (key == K::kCpuCores || key == K::kMemoryMib || key == K::kDiskGib ||
                    key.startsWith(QLatin1StringView("graphics/"))) {
                    refresh_identity();
                    refresh_status_bar();
                }
            });

    // Single-VM model: pin the pages to whatever record App populated
    // for the lone ``lsw`` domain on startup.
    const config::VmRecord* record = services_.vms().primary();
    if (record != nullptr) {
        overview_->set_target(record->uuid);
    }
    const bool provisioned = record != nullptr;
    overview_stack_->setCurrentIndex(provisioned ? 0 : 1);
    rail_->set_guest_provisioned(provisioned);
    refresh_identity();
    refresh_status_bar();
    show_page(NavRail::Page::Overview);
    on_graphics_changed();
    setup_tray();
}

void MainWindow::build_pages() {
    overview_ = new VmDetailPanel(services_, this);
    console_ = new ConsolePanel(services_.shells(), services_.status(), this);
    log_ = new LogPanel(services_.log_store(), this);
    alerts_ = new GraphicsAlerts(services_.status(), services_.rpc(), this);
    graphics_page_ =
        new GraphicsPage(services_.status(), *alerts_, services_.manager_settings(), this);
    settings_page_ = new SettingsPage(services_.manager_settings(), services_.pending_changes(),
                                      services_.app_configs(), services_.status(), this);

    overview_stack_ = new QStackedWidget;
    overview_stack_->addWidget(overview_);
    overview_stack_->addWidget(build_empty_state());

    // Order must match NavRail::Page.
    stack_->addWidget(overview_stack_);
    stack_->addWidget(console_);
    stack_->addWidget(log_);
    stack_->addWidget(graphics_page_);
    stack_->addWidget(settings_page_);

    connect(graphics_page_, &GraphicsPage::settings_requested, this,
            [this] { show_page(NavRail::Page::Settings); });
    connect(graphics_page_, &GraphicsPage::restart_requested, this, &MainWindow::restart_guest);
    connect(settings_page_, &SettingsPage::restart_requested, this, &MainWindow::restart_guest);
    // The image tools are the same dialogs the wizard opens. Nothing
    // about them needs a guest, so they work with the machine off.
    connect(settings_page_, &SettingsPage::download_tool_requested, this, [this] {
        tools::DownloadImageDialog dialog{services_.settings(), this};
        dialog.exec();
    });
    connect(settings_page_, &SettingsPage::trim_tool_requested, this, [this] {
        const config::VmRecord* record = services_.vms().primary();
        const QString source = record != nullptr ? record->iso_path : QString{};
        tools::TrimImageDialog dialog{services_.settings(), source, this};
        dialog.exec();
    });
    connect(settings_page_, &SettingsPage::reinstall_requested, this,
            &MainWindow::reinstall_requested);
    connect(settings_page_, &SettingsPage::rewrite_entries_requested, this, [this] {
        // Idempotent, and the point of the button is the case where the
        // files on disk have drifted from what the store says.
        services_.apply_integrations();
    });
    connect(settings_page_, &SettingsPage::open_share_requested, this, [this] {
        // The share is served in userspace by the file manager, so this
        // is a URL rather than a mount. The whole disk, whatever the
        // bookmarks' scope: the row says it opens C:. Signed in as the
        // account the bookmarks use, so the file manager does not ask.
        const auto& settings = services_.manager_settings();
        QDesktopServices::openUrl(QUrl(platform::GuestSmbShare::url(
            settings.string_value(K::kGuestUser), settings.string_value(K::kGuestPassword),
            QStringLiteral("C$"))));
    });
}

void MainWindow::pop_out(NavRail::Page page) {
    // A second instance rather than reparenting the embedded one: the
    // page keeps working where it is. Two consoles are two views: on the
    // same Windows session they show the same shell, so the pop-out can
    // sit on SYSTEM's while the page stays on the user's.
    QWidget* content = nullptr;
    QString title;
    if (page == NavRail::Page::Console) {
        content = new ConsolePanel(services_.shells(), services_.status());
        title = QStringLiteral("Console");
    } else if (page == NavRail::Page::Log) {
        content = new LogPanel(services_.log_store());
        title = QStringLiteral("Log");
    } else {
        return;
    }

    auto* window = new QWidget(nullptr, Qt::Window);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(QStringLiteral("LSW %1").arg(title));
    window->resize(900, 560);
    auto* layout = new QVBoxLayout(window);
    layout->setContentsMargins(0, 0, 0, 0);
    content->setParent(window);
    layout->addWidget(content);
    window->show();
}

void MainWindow::follow_agent_for_desktop(bool online) {
    run_on_vm_button_->setEnabled(online);
    run_on_vm_button_->setToolTip(
        online ? QStringLiteral("Copy a local Windows program or installer to the VM and run it")
               : QStringLiteral("Needs Windows running: the agent is offline"));
    if (run_on_vm_dialog_) run_on_vm_dialog_->set_agent_online(online);
    desktop_button_->setEnabled(online);
    desktop_button_->setToolTip(
        online ? QStringLiteral("The guest's whole screen in a window of its own")
               : QStringLiteral("Needs Windows running: the agent is offline"));
}

void MainWindow::open_desktop() {
    if (desktop_ != nullptr) {
        desktop_->raise();
        return;
    }
    const config::VmRecord* record = services_.vms().primary();
    const QString name =
        record == nullptr || record->name.isEmpty() ? QStringLiteral("lsw") : record->name;
    desktop_ = mirror::WindowMirror::of_desktop(services_.rpc(),
                                                QStringLiteral("%1 desktop").arg(name), this);
    connect(desktop_, &mirror::WindowMirror::finished, desktop_, &QObject::deleteLater);
    connect(desktop_, &mirror::WindowMirror::failed, this, [this](const QString& reason) {
        notices_->post(QStringLiteral("desktop"), false,
                       QStringLiteral("The desktop could not be opened."), reason);
    });
    connect(desktop_, &mirror::WindowMirror::ready, this,
            [this] { notices_->withdraw(QStringLiteral("desktop")); });
    if (!desktop_->start()) {
        desktop_->deleteLater();
        desktop_ = nullptr;
        return;
    }
}

QWidget* MainWindow::build_empty_state() {
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(20, 40, 20, 40);
    outer->addStretch(1);

    auto* column = new QVBoxLayout;
    column->setSpacing(14);
    column->setAlignment(Qt::AlignHCenter);

    auto* eyebrow = Widgets::caption(QStringLiteral("NOTHING INSTALLED"));
    eyebrow->setAlignment(Qt::AlignCenter);

    auto* heading = new QLabel(QStringLiteral("There is no Windows guest on this machine yet"));
    heading->setObjectName(QString::fromLatin1(Theme::Role::kHeading));
    heading->setAlignment(Qt::AlignCenter);

    auto* body =
        new QLabel(QStringLiteral("The first run builds one: pick or download an image, size the "
                                  "machine, and let the unattended installer do the rest. Around "
                                  "25 minutes, mostly unattended."));
    body->setObjectName(QString::fromLatin1(Theme::Role::kDim));
    body->setWordWrap(true);
    body->setAlignment(Qt::AlignCenter);
    // Fixed in both directions: a layout that centres a widget gives it
    // its plain size hint, which for a wrapping label is one line, and the
    // rest of the paragraph was cut off.
    body->setFixedWidth(420);
    body->ensurePolished();
    body->setFixedHeight(body->heightForWidth(420));

    auto* run = Widgets::button(QStringLiteral("Run the first-run wizard"), Theme::Role::kPrimary);
    connect(run, &QPushButton::clicked, this, &MainWindow::wizard_requested);

    column->addWidget(eyebrow);
    column->addWidget(heading);
    column->addWidget(body, 0, Qt::AlignHCenter);
    column->addWidget(run, 0, Qt::AlignHCenter);
    outer->addLayout(column);
    outer->addStretch(1);
    return page;
}

void MainWindow::show_page(NavRail::Page page) {
    const int index = static_cast<int>(page);
    if (index < 0 || index >= stack_->count()) {
        return;
    }
    stack_->setCurrentIndex(index);
    rail_->set_current(page);
    content_title_->setText(QString::fromLatin1(kPages[index].title));
    content_detail_->setText(page_detail(page));
    pop_out_button_->setVisible(page == NavRail::Page::Console || page == NavRail::Page::Log);
    desktop_button_->setVisible(page == NavRail::Page::Overview);
    run_on_vm_button_->setVisible(page == NavRail::Page::Overview);
    clear_button_->setVisible(page == NavRail::Page::Log || page == NavRail::Page::Console);
    change_settings_button_->setVisible(page == NavRail::Page::Graphics);
    config_folder_button_->setVisible(page == NavRail::Page::Settings);

    refresh_warning();
    refresh_status_bar();
}

void MainWindow::refresh_identity() {
    const config::VmRecord* primary = services_.vms().primary();
    if (primary == nullptr) {
        rail_->set_identity(QStringLiteral("lsw"), QStringLiteral("not provisioned"));
        return;
    }
    const auto& record = *primary;
    const auto& vm = services_.status().vm();
    // The machine's shape as libvirt holds it — the running one, or what
    // the next start boots — and the disk image's own size. The settings
    // only stand in when there is no definition to read (no libvirt).
    auto& settings = services_.manager_settings();
    const vm::DomainDescription* definition = vm.definition();
    const int cores = definition != nullptr ? definition->vcpus : settings.int_value(K::kCpuCores);
    const qint64 memory_mib =
        definition != nullptr ? definition->memory_mib : settings.int_value(K::kMemoryMib);
    const QString disk = vm.disk_bytes
                             ? ui::Format::gib(static_cast<qint64>(*vm.disk_bytes), 0)
                             : QStringLiteral("%1 GiB").arg(settings.int_value(K::kDiskGib));
    QString detail = QStringLiteral("%1\n%2 cores · %3 GiB · %4")
                         .arg(record.edition.isEmpty() ? QStringLiteral("Windows") : record.edition)
                         .arg(cores)
                         .arg(memory_mib / 1024)
                         .arg(disk);
    // The GPU Windows sees, because with accel-virt that is the adapter's
    // own name; on every page, since the rail is.
    const auto& adapters = services_.status().graphics().adapters;
    if (!adapters.isEmpty()) {
        detail += QLatin1Char('\n') + platform::HostGpus::adapter_name(adapters.first().gpu);
        if (adapters.size() > 1) {
            detail += QStringLiteral(" +%1 render").arg(adapters.size() - 1);
        }
    }
    rail_->set_identity(record.name.isEmpty() ? QStringLiteral("lsw") : record.name, detail);
}

QString MainWindow::page_detail(NavRail::Page page) const {
    switch (page) {
    case NavRail::Page::Overview: {
        const auto& vm = services_.status().vm();
        if (!vm.running()) {
            return QStringLiteral("stopped");
        }
        return vm.agent_online() ? QStringLiteral("running")
                                 : QStringLiteral("running, Windows not answering yet");
    }
    case NavRail::Page::Graphics:
        return graphics_page_ != nullptr ? graphics_page_->summary() : QString{};
    case NavRail::Page::Log:
        // The records arrive from the agent as it writes them, so the
        // page is live exactly when the agent answers.
        return services_.status().vm().agent_online() ? QStringLiteral("live")
                                                      : QStringLiteral("agent not answering");
    case NavRail::Page::Settings:
        return tilde(services_.manager_settings().file_path());
    case NavRail::Page::Console:
        return console_ != nullptr ? console_->summary() : QString{};
    case NavRail::Page::Count:
        break;
    }
    return QString::fromLatin1(kPages[static_cast<int>(page)].detail);
}

void MainWindow::refresh_status_bar() {
    QStringList facts;
    if (!notice_.isEmpty()) {
        facts << notice_;
    }
    if (rail_->current() == NavRail::Page::Graphics && graphics_page_ != nullptr) {
        facts << graphics_page_->status_facts();
    } else {
        // The facts, with a rule between each, which is what makes a row
        // of them read as a row rather than as a sentence. Each is read:
        // the channel's own target and the disk as its definition
        // declares it.
        const auto& vm = services_.status().vm();
        const quint32 port = services_.rpc().port();
        facts << (port != 0 ? QStringLiteral("agent 127.0.0.1:%1").arg(port)
                            : QStringLiteral("agent not targeted"));
        if (const vm::DomainDescription* definition = vm.definition()) {
            QString disk = definition->disk_bus == QLatin1StringView("nvme")
                               ? QStringLiteral("NVMe")
                               : definition->disk_bus;
            if (vm.disk_bytes) {
                disk +=
                    QStringLiteral(", ") + ui::Format::gib(static_cast<qint64>(*vm.disk_bytes), 0);
            }
            facts << QStringLiteral("disk %1").arg(disk);
        }
        facts << (!vm.running()       ? QStringLiteral("guest off")
                  : vm.agent_online() ? QStringLiteral("agent answering")
                                      : QStringLiteral("agent not answering yet"));
    }
    status_left_->setText(facts.join(QStringLiteral("   │   ")));
}

void MainWindow::on_graphics_changed() {
    refresh_warning();
    refresh_identity();
    content_detail_->setText(page_detail(rail_->current()));
    refresh_status_bar();
}

void MainWindow::refresh_warning() {
    using Severity = GraphicsAlerts::Severity;
    const Severity worst = alerts_->worst();
    rail_->set_graphics_warning(worst == Severity::Red     ? Theme::Tone::kBad
                                : worst == Severity::Amber ? Theme::Tone::kAmber
                                                           : nullptr);
    // Only the ones that break the guest, and only on the overview: the
    // rest are the Graphics page's to say.
    std::optional<GraphicsAlerts::Warning> shown;
    for (const auto& warning : alerts_->warnings()) {
        if (warning.kind == GraphicsAlerts::Warning::Kind::NoDirect3d ||
            warning.kind == GraphicsAlerts::Warning::Kind::DisplayNotRunning ||
            warning.kind == GraphicsAlerts::Warning::Kind::DisplayNoDriver) {
            shown = warning;
            break;
        }
    }
    if (!shown || rail_->current() != NavRail::Page::Overview) {
        warn_strip_->setVisible(false);
        return;
    }
    warn_strip_->set_message(shown->critical, shown->headline, shown->detail);
    if (warn_strip_->findChild<QPushButton*>() == nullptr) {
        connect(warn_strip_->add_button(QStringLiteral("Open Graphics")), &QPushButton::clicked,
                this, [this] { show_page(NavRail::Page::Graphics); });
    }
    warn_strip_->setVisible(true);
}

std::optional<config::DisplayMode> MainWindow::current_output() const {
    const QScreen* screen = this->screen();
    if (screen == nullptr) {
        return std::nullopt;
    }
    // The monitor's own pixels, not the scaled desktop's.
    const QSize size = screen->geometry().size() * screen->devicePixelRatio();
    return config::DisplayMode{size.width(), size.height()};
}

void MainWindow::push_display_modes() {
    const config::VmRecord* record = services_.vms().primary();
    if (record == nullptr || !services_.status().vm().running()) {
        return;
    }
    QString error;
    const QString modes =
        services::display_modes_from_settings(services_.manager_settings(), current_output());
    if (!services_.lifecycle().set_display_modes(record->uuid, modes, &error)) {
        notices_->post(QStringLiteral("resolution"), false,
                       QStringLiteral("The resolution did not reach the guest."), error);
        return;
    }
    notices_->withdraw(QStringLiteral("resolution"));
    services_.status().refresh(services::StatusHub::Area::Graphics);
}

void MainWindow::push_dpi() {
    // The scale is Windows' own, set by the agent in the user's session
    // while it runs; the resolution is the device's and is not sent here.
    if (!services_.status().vm().agent_online()) {
        return;
    }
    services_.rpc().request_detached(
        linkproto::cmd::kSetDisplay,
        {{"scaling_percent",
          platform::HostScale::resolve(services_.manager_settings().int_value(K::kScalingPercent),
                                       this)}});
}

void MainWindow::restart_guest() {
    // The values are current by the time this runs; what is left is to
    // make the machine take them. Cores, memory, the shared folders and
    // the GPU devices are the domain definition rather than properties
    // of a running guest, so the definition is rewritten first — libvirt
    // keeps it for the next start — and the guest is stopped and started
    // on it.
    const config::VmRecord* primary = services_.vms().primary();
    if (primary == nullptr || restart_ != nullptr) {
        return;
    }
    config::VmRecord record = *primary;
    record.sizing = services::vm_sizing_from_settings(services_.manager_settings());
    const vm::DomainXmlInputs inputs =
        services::domain_inputs_from_settings(services_.settings(), services_.manager_settings());
    if (services_.lifecycle().redefine(record, inputs) != 0) {
        // A definition libvirt refused is one the guest must not be
        // started against, and the old one is still on disk, so the
        // honest thing is to say nothing was applied.
        notices_->post(QStringLiteral("definition"), true,
                       QStringLiteral("libvirt refused the new machine definition."),
                       QStringLiteral("Nothing changed: the guest keeps the definition it had. "
                                      "The manager's own log says why."),
                       {{QStringLiteral("Open the log"), [this] { show_page(NavRail::Page::Log); }}});
        return;
    }
    // The stored definition is new: what is pending, and what the next
    // start boots, follow from it.
    services_.status().refresh(services::StatusHub::Area::Vm);
    // The disk is the one part of the machine that is a file rather than
    // the definition, and it can only grow while QEMU does not hold it —
    // so it grows between the stop and the start, never on a running
    // guest. A refusal is reported and the guest still starts: on its old
    // size, which is a working machine, rather than not at all.
    notices_->withdraw(QStringLiteral("definition"));
    const auto grow_disk = [this, record] {
        QString error;
        if (vm::Lifecycle::grow_disk(record.disk_path, record.sizing.disk_gib, &error)) {
            notices_->withdraw(QStringLiteral("disk"));
            return;
        }
        notices_->post(QStringLiteral("disk"), false,
                       QStringLiteral("The disk was not grown."),
                       QStringLiteral("The guest starts on its old size: %1").arg(error));
    };
    if (!services_.lifecycle().is_running(record.uuid)) {
        grow_disk();
        if (overview_->start_guest()) {
            notice_ = QStringLiteral("Starting the guest on the new definition");
            refresh_status_bar();
        } else {
            notice_start_refused(QStringLiteral("libvirt would not start the guest."));
        }
        return;
    }
    // A start while the old QEMU still runs does nothing, so the start
    // waits for the stop: asked politely, asked again, and at the
    // deadline forced.
    notice_ = QStringLiteral("Restarting the guest: waiting for Windows to shut down");
    refresh_status_bar();
    restart_ = new vm::GracefulShutdown(services_.lifecycle(), record.uuid,
                                        vm::GracefulShutdown::kDefaultDeadlineMs, this);
    connect(restart_, &vm::GracefulShutdown::finished, this, [this, grow_disk](bool forced) {
        restart_->deleteLater();
        restart_ = nullptr;
        grow_disk();
        notice_ = forced ? QStringLiteral("Windows did not shut down in time and was stopped; "
                                          "starting it again")
                         : QStringLiteral("Starting the guest again");
        if (!overview_->start_guest()) {
            notice_.clear();
            notice_start_refused(QStringLiteral("libvirt would not start the guest again."));
        }
        refresh_status_bar();
    });
    restart_->start();
}

void MainWindow::notice_start_refused(const QString& headline) {
    notices_->post(QStringLiteral("start"), true, headline,
                   QStringLiteral("The manager's own log says why."),
                   {{QStringLiteral("Open the log"), [this] { show_page(NavRail::Page::Log); }}});
}

void MainWindow::follow_launches() {
    auto& launcher = services_.app_launcher();
    connect(&launcher, &apps::GuestAppLauncher::failed, this,
            [this](const QString& target, const QString& error) {
        QString name = target.section(QLatin1Char('\\'), -1);
        for (const auto& app : services_.app_configs().configs()) {
            if (app.target() == target && !app.name.isEmpty()) {
                name = app.name;
                break;
            }
        }
        notices_->post(QStringLiteral("launch:") + target, false,
                       QStringLiteral("Could not open %1.").arg(name), error,
                       {{QStringLiteral("Open desktop"), [this] { open_desktop(); }}});
    });
    connect(&launcher, &apps::GuestAppLauncher::launched, this,
            [this](const QString& target, quint32) {
        notices_->withdraw(QStringLiteral("launch:") + target);
    });
}

void MainWindow::setup_tray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    tray_ = new QSystemTrayIcon(this);
    tray_->setIcon(QApplication::windowIcon());
    tray_->setToolTip(QStringLiteral("LSW"));
    auto* menu = new QMenu(this);
    menu->addAction(QStringLiteral("Show"), this, [this] { Widgets::bring_to_front(this); });
    menu->addAction(QStringLiteral("Settings"), this, [this] {
        show_page(NavRail::Page::Settings);
        show();
        raise();
    });
    menu->addSeparator();
    menu->addAction(QStringLiteral("Quit"), this, &MainWindow::on_quit);
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, &MainWindow::on_tray_activated);
    tray_->show();
}

void MainWindow::on_tray_activated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) {
        if (isVisible()) {
            hide();
        } else {
            Widgets::bring_to_front(this);
        }
    }
}

bool MainWindow::event(QEvent* e) {
    // "Match this desktop" is this window's scale, which is not known
    // until the compositor has mapped it (a Wayland screen says 2 for
    // 1.2 until then) and changes when it moves to another monitor. So
    // the guest is told again whenever it changes.
    if (e->type() == QEvent::DevicePixelRatioChange &&
        services_.manager_settings().int_value(K::kScalingPercent) == 0) {
        push_dpi();
    }
    return QMainWindow::event(e);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    const bool to_tray = services_.manager_settings().bool_value(K::kCloseToTray);
    if (tray_ != nullptr && to_tray) {
        hide();
        e->ignore();
        return;
    }
    QMainWindow::closeEvent(e);
}

void MainWindow::on_quit() {
    if (tray_ != nullptr) {
        tray_->hide();
    }
    qApp->quit();
}

}  // namespace lsw::host::manager
