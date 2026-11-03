#include "lsw/host/manager/main_window.hpp"

#include "lsw/host/config/display_refresh.hpp"
#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/manager/app_error_dialog.hpp"
#include "lsw/host/manager/apply_bar.hpp"
#include "lsw/host/manager/collect_logs_dialog.hpp"
#include "lsw/host/manager/console_panel.hpp"
#include "lsw/host/manager/display_page.hpp"
#include "lsw/host/manager/files_page.hpp"
#include "lsw/host/manager/graphics_alerts.hpp"
#include "lsw/host/manager/graphics_page.hpp"
#include "lsw/host/manager/log_panel.hpp"
#include "lsw/host/manager/maintenance_page.hpp"
#include "lsw/host/manager/notice_board.hpp"
#include "lsw/host/manager/run_on_vm_dialog.hpp"
#include "lsw/host/manager/screen_pixels.hpp"
#include "lsw/host/manager/settings_page.hpp"
#include "lsw/host/manager/update_dialog.hpp"
#include "lsw/host/manager/usb_page.hpp"
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
#include <QGuiApplication>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>

namespace lsw::host::manager {

namespace {

using ui::Theme;
using ui::Widgets;
using K = config::ManagerSettings::Key;

constexpr int kClearedNoticeMs = 6000;

struct PageInfo {
    const char* title;
    const char* detail;
};

constexpr std::array<PageInfo, static_cast<std::size_t>(NavRail::Page::Count)> kPages{{
    {"Overview", ""},
    {"Display", ""},
    {"Graphics", ""},
    {"Files", ""},
    {"USB", ""},
    {"Console", ""},
    {"Log", ""},
    {"Maintenance", ""},
    {"Settings", ""},
}};

QString tilde(const QString& path) {
    const QString home = QDir::homePath();
    if (path.startsWith(home + QLatin1Char('/'))) {
        return QStringLiteral("~") + path.mid(home.size());
    }
    return path;
}

}

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
    const auto bar_action = [&](const QString& text) {
        auto* button = Widgets::button(text, Theme::Role::kQuiet);
        button->setVisible(false);
        bar_layout->addWidget(button);
        return button;
    };
    desktop_button_ = bar_action(QStringLiteral("Open desktop"));
    run_on_vm_button_ = bar_action(QStringLiteral("Run on VM…"));
    clear_button_ = bar_action(QStringLiteral("Clear"));
    windows_files_button_ = bar_action(QStringLiteral("Open Windows files"));
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
        clear_logs();
    });
    connect(windows_files_button_, &QPushButton::clicked, this, &MainWindow::open_windows_files);
    connect(config_folder_button_, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(services_.settings().config_dir()));
    });
    right_layout->addWidget(bar);

    warn_strip_ = new WarningStrip;
    warn_strip_->setVisible(false);
    right_layout->addWidget(warn_strip_);
    notices_ = new NoticeBoard;
    right_layout->addWidget(notices_);

    stack_ = new QStackedWidget;
    right_layout->addWidget(stack_, 1);

    apply_bar_ = new ApplyBar(services_.pending_changes());
    right_layout->addWidget(apply_bar_);

    auto* strip = new QWidget;
    Theme::surface(strip, Theme::Role::kStatusStrip);
    auto* strip_layout = new QHBoxLayout(strip);
    strip_layout->setContentsMargins(14, 5, 14, 5);
    status_left_ = Widgets::meta(QString{});
    status_left_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    strip_layout->addWidget(status_left_, 1);
    right_layout->addWidget(strip);

    columns->addWidget(right, 1);
    setCentralWidget(central);

    build_pages();

    connect(rail_, &NavRail::page_selected, this, &MainWindow::show_page);
    follow_launches();
    follow_upkeep();
    connect(rail_, &NavRail::start_requested, this, &MainWindow::start_guest);
    connect(usb_page_, &UsbPage::start_requested, this, &MainWindow::start_guest);
    connect(rail_, &NavRail::stop_requested, overview_, &VmDetailPanel::stop_guest);

    connect(overview_, &VmDetailPanel::running_changed, this, [this](bool running) {
        rail_->set_running(running);
        if (running) {
            notices_->withdraw(QStringLiteral("start"));
            push_display_modes();
            push_display_refresh();
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
            if (restart_ == nullptr) {
                notice_.clear();
            }
            push_dpi();
        }
    });
    connect(&services_.status(), &services::StatusHub::changed, this, [this] {
        refresh_status_bar();
        refresh_detail();
        refresh_identity();
    });
    connect(&services_.status(), &services::StatusHub::changed, this,
            [this](services::StatusHub::Area area) {
                if (area != services::StatusHub::Area::Usb) {
                    return;
                }
                const auto& devices = services_.status().usb().devices;
                rail_->set_usb_count(static_cast<int>(std::count_if(
                    devices.begin(), devices.end(), [](const services::UsbStatus::Device& device) {
                        return device.place == services::UsbStatus::Place::Guest;
                    })));
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
    connect(apply_bar_, &ApplyBar::restart_requested, this, [this] { (void)restart_guest(); });
    connect(apply_bar_, &ApplyBar::counts_changed, this, &MainWindow::refresh_pending_counts);
    refresh_pending_counts();
    connect(alerts_, &GraphicsAlerts::changed, this, &MainWindow::on_graphics_changed);
    connect(graphics_page_, &GraphicsPage::summary_changed, this, &MainWindow::on_graphics_changed);
    connect(&services_.manager_settings(), &config::ManagerSettings::changed, this,
            [this](const QString& key) {
                if (key == K::kDisplayModes || key == K::kMatchHostOutput) {
                    push_display_modes();
                } else if (key == K::kDisplayRefresh) {
                    push_display_refresh();
                } else if (key == K::kScalingPercent) {
                    push_dpi();
                }
                if (key == K::kCpuCores || key == K::kMemoryMib || key == K::kDiskGib ||
                    key.startsWith(QLatin1StringView("graphics/"))) {
                    refresh_identity();
                    refresh_status_bar();
                }
            });

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

    const auto watch = [this](QScreen* screen) {
        connect(screen, &QScreen::refreshRateChanged, this, [this] { follow_monitor_refresh(); });
    };
    for (QScreen* screen : QGuiApplication::screens()) {
        watch(screen);
    }
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this, watch](QScreen* screen) {
        watch(screen);
        services_.status().refresh(services::StatusHub::Area::Host);
        follow_monitor_refresh();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] {
        services_.status().refresh(services::StatusHub::Area::Host);
        follow_monitor_refresh();
    });
    follow_monitor_refresh();
}

void MainWindow::build_pages() {
    overview_ = new VmDetailPanel(services_, this);
    console_ = new ConsolePanel(services_.shells(), services_.status(), this);
    log_ = new LogPanel(services_.log_store(), services_.settings(), services_.log_collector(), this);
    connect(log_, &LogPanel::collect_requested, this, &MainWindow::collect_logs);
    display_page_ = new DisplayPage(services_.status(), services_.manager_settings(),
                                    services_.pending_changes(), this);
    connect(display_page_, &DisplayPage::summary_changed, this, &MainWindow::refresh_detail);
    files_page_ = new FilesPage(services_.status(), services_.manager_settings(),
                                services_.pending_changes(), this);
    connect(files_page_, &FilesPage::summary_changed, this, &MainWindow::refresh_detail);
    maintenance_page_ = new MaintenancePage(services_.status(), this);
    connect(maintenance_page_, &MaintenancePage::summary_changed, this, &MainWindow::refresh_detail);
    usb_page_ = new UsbPage(services_.status(), services_.usb(), this);
    connect(usb_page_, &UsbPage::summary_changed, this, &MainWindow::refresh_detail);
    alerts_ = new GraphicsAlerts(services_.status(), this);
    graphics_page_ =
        new GraphicsPage(services_.status(), *alerts_, services_.manager_settings(),
                         services_.pending_changes(), this);
    settings_page_ = new SettingsPage(services_.manager_settings(), services_.pending_changes(),
                                      services_.status(), services_.log_verbosity(), this);

    overview_stack_ = new QStackedWidget;
    overview_stack_->addWidget(overview_);
    overview_stack_->addWidget(build_empty_state());

    stack_->addWidget(overview_stack_);
    stack_->addWidget(display_page_);
    stack_->addWidget(graphics_page_);
    stack_->addWidget(files_page_);
    stack_->addWidget(usb_page_);
    stack_->addWidget(console_);
    stack_->addWidget(log_);
    stack_->addWidget(maintenance_page_);
    stack_->addWidget(settings_page_);

    connect(maintenance_page_, &MaintenancePage::download_requested, this, [this] {
        tools::DownloadImageDialog dialog{services_.settings(), this};
        dialog.exec();
    });
    connect(maintenance_page_, &MaintenancePage::trim_requested, this, [this] {
        const config::VmRecord* record = services_.vms().primary();
        const QString source = record != nullptr ? record->iso_path : QString{};
        tools::TrimImageDialog dialog{services_.settings(), source, this};
        dialog.exec();
    });
    auto& collector = services_.log_collector();
    auto& cleaner = services_.log_cleaner();
    connect(&collector, &services::LogCollector::step_started, this, &MainWindow::refresh_log_buttons);
    connect(&collector, &services::LogCollector::finished, this, &MainWindow::refresh_log_buttons);
    connect(&collector, &services::LogCollector::failed, this, &MainWindow::refresh_log_buttons);
    connect(&cleaner, &services::LogCleaner::started, this, &MainWindow::refresh_log_buttons);
    connect(&cleaner, &services::LogCleaner::finished, this, &MainWindow::refresh_log_buttons);
    connect(&cleaner, &services::LogCleaner::finished, this, &MainWindow::show_cleared);
    connect(maintenance_page_, &MaintenancePage::update_requested, this,
            [this] { services_.guest_update().run(true); });
    update_dialog_ = new UpdateDialog(services_.status(), this);
    connect(update_dialog_, &UpdateDialog::retry_requested, this,
            [this] { services_.guest_update().retry(); });
    connect(maintenance_page_, &MaintenancePage::reinstall_requested, this,
            &MainWindow::reinstall_requested);
    connect(settings_page_, &SettingsPage::rewrite_entries_requested, this, [this] {
        services_.apply_integrations();
    });
}

void MainWindow::clear_logs() {
    if (services_.log_cleaner().running() || services_.log_collector().running()) {
        return;
    }
    QMessageBox ask{this};
    ask.setIcon(QMessageBox::Question);
    ask.setWindowTitle(tr("Clear all logs?"));
    ask.setText(tr("Clear all logs?"));
    ask.setInformativeText(tr("This deletes the logs of the manager, the window mirrors, the GPU "
                              "driver and Windows. Collected archives stay."));
    QPushButton* clear = ask.addButton(tr("Clear"), QMessageBox::AcceptRole);
    ask.addButton(tr("Cancel"), QMessageBox::RejectRole);
    ask.setDefaultButton(static_cast<QPushButton*>(ask.button(QMessageBox::Cancel)));
    ask.exec();
    if (ask.clickedButton() == clear) {
        services_.log_cleaner().start();
    }
}

void MainWindow::refresh_log_buttons() {
    const bool busy = services_.log_cleaner().running() || services_.log_collector().running();
    clear_button_->setEnabled(!busy || rail_->current() != NavRail::Page::Log);
}

void MainWindow::show_cleared() {
    const auto& result = services_.log_cleaner().result();
    const QString key = QStringLiteral("logs-cleared");
    notices_->post(key, false, result.headline(), result.detail());
    if (result.complete()) {
        QTimer::singleShot(kClearedNoticeMs, this, [this, key] { notices_->withdraw(key); });
    }
}

void MainWindow::collect_logs() {
    if (services_.log_cleaner().running()) {
        return;
    }
    auto* dialog = new CollectLogsDialog(services_.log_collector(), this);
    services_.log_collector().start();
    dialog->show();
    Widgets::bring_to_front(dialog);
}

void MainWindow::pop_out(NavRail::Page page) {
    QWidget* content = nullptr;
    QString title;
    if (page == NavRail::Page::Console) {
        content = new ConsolePanel(services_.shells(), services_.status());
        title = QStringLiteral("Console");
    } else if (page == NavRail::Page::Log) {
        auto* panel = new LogPanel(services_.log_store(), services_.settings(),
                                   services_.log_collector());
        connect(panel, &LogPanel::collect_requested, this, &MainWindow::collect_logs);
        content = panel;
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

void MainWindow::start_guest() {
    if (!overview_->start_guest()) {
        notice_start_refused(QStringLiteral("libvirt would not start the guest."));
    }
}

void MainWindow::open_windows_files() {
    const auto& settings = services_.manager_settings();
    QDesktopServices::openUrl(QUrl(platform::GuestSmbShare::url(
        settings.string_value(K::kGuestUser), settings.string_value(K::kGuestPassword),
        platform::GuestSmbShare::share_for_scope(settings.string_value(K::kShareScope)))));
}

void MainWindow::open_desktop() {
    if (desktop_ != nullptr) {
        desktop_->raise(QString());
        return;
    }
    const config::VmRecord* record = services_.vms().primary();
    const QString name =
        record == nullptr || record->name.isEmpty() ? QStringLiteral("lsw") : record->name;
    desktop_ = mirror::WindowMirror::of_desktop(services_.rpc(), QStringLiteral("%1 desktop").arg(name),
                                                screen() != nullptr ? screen()->name() : QString(), this);
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
    content_title_->setText(QString::fromLatin1(kPages[static_cast<std::size_t>(index)].title));
    content_detail_->setText(page_detail(page));
    pop_out_button_->setVisible(page == NavRail::Page::Console || page == NavRail::Page::Log);
    desktop_button_->setVisible(page == NavRail::Page::Overview || page == NavRail::Page::Display);
    run_on_vm_button_->setVisible(page == NavRail::Page::Overview);
    clear_button_->setVisible(page == NavRail::Page::Log || page == NavRail::Page::Console);
    clear_button_->setText(page == NavRail::Page::Log ? QStringLiteral("Clear all logs")
                                                      : QStringLiteral("Clear"));
    refresh_log_buttons();
    windows_files_button_->setVisible(page == NavRail::Page::Files);
    config_folder_button_->setVisible(page == NavRail::Page::Settings);

    refresh_warning();
    refresh_status_bar();
}

void MainWindow::refresh_detail() {
    content_detail_->setText(page_detail(rail_->current()));
}

void MainWindow::refresh_pending_counts() {
    const auto counts = apply_bar_->counts();
    for (int i = 0; i < static_cast<int>(NavRail::Page::Count); ++i) {
        rail_->set_pending(static_cast<NavRail::Page>(i), counts[static_cast<std::size_t>(i)]);
    }
}

void MainWindow::refresh_identity() {
    const config::VmRecord* primary = services_.vms().primary();
    if (primary == nullptr) {
        rail_->set_identity(QStringLiteral("lsw"), QStringLiteral("not provisioned"));
        return;
    }
    const auto& record = *primary;
    const auto& vm = services_.status().vm();
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
        return services_.status().vm().agent_online() ? QStringLiteral("live")
                                                      : QStringLiteral("agent not answering");
    case NavRail::Page::Settings:
        return tilde(services_.manager_settings().file_path());
    case NavRail::Page::Console:
        return console_ != nullptr ? console_->summary() : QString{};
    case NavRail::Page::Display:
        return display_page_ != nullptr ? display_page_->summary() : QString{};
    case NavRail::Page::Files:
        return files_page_ != nullptr ? files_page_->summary() : QString{};
    case NavRail::Page::Usb:
        return usb_page_ != nullptr ? usb_page_->summary() : QString{};
    case NavRail::Page::Maintenance:
        return maintenance_page_ != nullptr ? maintenance_page_->summary() : QString{};
    case NavRail::Page::Count:
        break;
    }
    return QString::fromLatin1(kPages[static_cast<std::size_t>(page)].detail);
}

void MainWindow::refresh_status_bar() {
    QStringList facts;
    if (!notice_.isEmpty()) {
        facts << notice_;
    }
    if (services_.status().update().running()) {
        facts << QStringLiteral("Updating LSW in Windows");
    }
    if (rail_->current() == NavRail::Page::Graphics && graphics_page_ != nullptr) {
        facts << graphics_page_->status_facts();
    } else {
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

std::optional<quint32> MainWindow::current_refresh_millihz() const {
    const QScreen* screen = this->screen();
    if (screen == nullptr) {
        return std::nullopt;
    }
    return config::DisplayRefresh::from_hertz(screen->refreshRate());
}

void MainWindow::follow_monitor_refresh() {
    services_.status().set_monitor_refresh(current_refresh_millihz());
    push_display_refresh();
}

void MainWindow::push_display_refresh() {
    const config::VmRecord* record = services_.vms().primary();
    if (record == nullptr || !services_.status().vm().running()) {
        return;
    }
    const auto rate = services::display_refresh_from_settings(services_.manager_settings(),
                                                              current_refresh_millihz());
    if (!rate) {
        return;
    }
    if (services_.status().graphics().live_refresh == rate) {
        notices_->withdraw(QStringLiteral("refresh"));
        return;
    }
    QString error;
    if (!services_.lifecycle().set_display_refresh(record->uuid, *rate, &error)) {
        notices_->post(QStringLiteral("refresh"), false,
                       QStringLiteral("The refresh rate did not reach the guest."), error);
        return;
    }
    notices_->withdraw(QStringLiteral("refresh"));
    services_.status().refresh(services::StatusHub::Area::Graphics);
}

std::optional<config::DisplayMode> MainWindow::current_output() const {
    const auto size = ScreenPixels(services_.status()).of(this->screen(), windowHandle());
    if (!size) {
        return std::nullopt;
    }
    return config::DisplayMode{size->width(), size->height()};
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
    if (!services_.status().vm().agent_online()) {
        return;
    }
    const int percent = platform::HostScale::resolve(
        services_.manager_settings().int_value(K::kScalingPercent), this);
    services_.rpc().request_async(
        linkproto::cmd::kSetDisplay, {{"scaling_percent", percent}}, this,
        [this](const link::AgentReply& reply) {
            const int applied = reply.ok ? reply.body.value("scale_percent", 0) : 0;
            services_.status().set_scale_percent(applied > 0 ? std::optional<int>{applied}
                                                             : std::nullopt);
        });
}

bool MainWindow::redefine_now(const config::VmRecord& primary) {
    if (!services_.redefine_vm(primary)) {
        notices_->post(QStringLiteral("definition"), true,
                       QStringLiteral("libvirt refused the new machine definition."),
                       QStringLiteral("Nothing changed: the guest keeps the definition it had. "
                                      "The manager's own log says why."),
                       {{QStringLiteral("Open the log"), [this] { show_page(NavRail::Page::Log); }}});
        return false;
    }
    notices_->withdraw(QStringLiteral("definition"));
    return true;
}

RestartResult MainWindow::restart_guest(QString* reason) {
    const config::VmRecord* primary = services_.vms().primary();
    if (primary == nullptr) {
        if (reason != nullptr) {
            *reason = QStringLiteral("there is no Windows machine to restart");
        }
        return RestartResult::Refused;
    }
    if (restart_ != nullptr) {
        redefine_again_ = true;
        return RestartResult::Pending;
    }
    if (!redefine_now(*primary)) {
        if (reason != nullptr) {
            *reason = services_.lifecycle().last_error().isEmpty()
                          ? QStringLiteral("libvirt refused the new machine definition")
                          : services_.lifecycle().last_error();
        }
        return RestartResult::Refused;
    }
    const config::VmRecord record = *primary;
    const auto grow_disk = [this, record] {
        QString error;
        const auto sizing = services::vm_sizing_from_settings(services_.manager_settings());
        if (vm::Lifecycle::grow_disk(record.disk_path, sizing.disk_gib, &error)) {
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
            return RestartResult::Started;
        }
        notice_start_refused(QStringLiteral("libvirt would not start the guest."));
        if (reason != nullptr) {
            *reason = services_.lifecycle().last_error().isEmpty()
                          ? QStringLiteral("libvirt would not start the guest")
                          : services_.lifecycle().last_error();
        }
        return RestartResult::Refused;
    }
    notice_ = QStringLiteral("Restarting the guest: waiting for Windows to shut down");
    refresh_status_bar();
    restart_ = new vm::GracefulShutdown(services_.lifecycle(), record.uuid,
                                        vm::GracefulShutdown::kDefaultDeadlineMs, this);
    connect(restart_, &vm::GracefulShutdown::finished, this, [this, grow_disk, record](bool forced) {
        restart_->deleteLater();
        restart_ = nullptr;
        if (std::exchange(redefine_again_, false)) {
            if (const config::VmRecord* now = services_.vms().primary(); now != nullptr) {
                (void)redefine_now(*now);
            }
        }
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
    return RestartResult::Started;
}

void MainWindow::notice_start_refused(const QString& headline) {
    notices_->post(QStringLiteral("start"), true, headline,
                   QStringLiteral("The manager's own log says why."),
                   {{QStringLiteral("Open the log"), [this] { show_page(NavRail::Page::Log); }}});
}

void MainWindow::follow_upkeep() {
    auto& upkeep = services_.upkeep();
    const auto refresh = [this, &upkeep] {
        refresh_status_bar();
        const auto& update = services_.status().update();
        QString why;
        if (update.state == services::GuestUpdateStatus::State::Failed) {
            why = update.message;
        } else if (upkeep.state() == services::GuestUpkeep::State::Failed) {
            why = upkeep.error();
        } else {
            notices_->withdraw(QStringLiteral("upkeep"));
            return;
        }
        notices_->post(QStringLiteral("upkeep"), false,
                       QStringLiteral("Could not update LSW in Windows."), why,
                       {{QStringLiteral("Try again"), [&upkeep] { upkeep.retry(); }}});
    };
    connect(&upkeep, &services::GuestUpkeep::changed, this, refresh);
    connect(&services_.status(), &services::StatusHub::changed, this,
            [refresh](services::StatusHub::Area area) {
                if (area == services::StatusHub::Area::Update) {
                    refresh();
                }
            });
}

void MainWindow::follow_launches() {
    auto& launcher = services_.app_launcher();
    launcher.set_output_source([this] { return screen() != nullptr ? screen()->name() : QString(); });
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
    connect(&services_.app_errors(), &services::AppErrors::reported, this,
            &MainWindow::show_app_error);
    connect(&launcher, &apps::GuestAppLauncher::launched, this,
            [this](const QString& target, quint32) {
        notices_->withdraw(QStringLiteral("launch:") + target);
    });
}

void MainWindow::show_app_error(const services::AppErrorReport& report) {
    const QString key = report.identity();
    if (const auto open = app_error_dialogs_.value(key); open) {
        return;
    }
    const bool anchored = isVisible() && !isMinimized();
    auto* dialog = new AppErrorDialog(report, anchored ? this : nullptr);
    app_error_dialogs_.insert(key, dialog);
    if (anchored) {
        dialog->open();
    } else {
        dialog->setModal(true);
        Widgets::bring_to_front(dialog);
    }
}

void MainWindow::setup_tray() {
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
    if (e->type() == QEvent::DevicePixelRatioChange &&
        services_.manager_settings().int_value(K::kScalingPercent) == 0) {
        push_dpi();
    }
    if (e->type() == QEvent::DevicePixelRatioChange && display_page_ != nullptr) {
        display_page_->refresh();
    }
    if (e->type() == QEvent::ScreenChangeInternal) {
        follow_monitor_refresh();
        if (display_page_ != nullptr) {
            display_page_->refresh();
        }
    }
    return QMainWindow::event(e);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    const bool to_tray = services_.manager_settings().bool_value(K::kCloseToTray);
    if (tray_ != nullptr && to_tray && QSystemTrayIcon::isSystemTrayAvailable()) {
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

}
