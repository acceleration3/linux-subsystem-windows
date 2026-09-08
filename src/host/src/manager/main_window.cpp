#include "lsw/host/manager/main_window.hpp"

#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/manager/console_panel.hpp"
#include "lsw/host/manager/log_panel.hpp"
#include "lsw/host/manager/render_panel.hpp"
#include "lsw/host/manager/settings_page.hpp"
#include "lsw/host/manager/vm_detail_panel.hpp"
#include "lsw/host/services/host_services.hpp"
#include "lsw/host/ui/theme.hpp"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

using ui::Theme;
using K = config::ManagerSettings::Key;

struct PageInfo {
    const char* title;
    const char* detail;
};

const PageInfo kPages[] = {
    {"Overview", ""},
    {"Console", "cmd.exe in the guest"},
    {"Log", "live"},
    {"Render channel", "shared memory between the guest drivers and the server"},
    {"Settings", "~/.config/lsw/manager.conf"},
};

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
    bar->setObjectName(QString::fromLatin1(Theme::Role::kContentBar));
    auto* bar_layout = new QHBoxLayout(bar);
    bar_layout->setContentsMargins(14, 9, 14, 9);
    bar_layout->setSpacing(10);
    content_title_ = new QLabel;
    QFont title_font = content_title_->font();
    title_font.setPixelSize(14);
    title_font.setWeight(QFont::DemiBold);
    content_title_->setFont(title_font);
    content_detail_ = new QLabel;
    content_detail_->setObjectName(QString::fromLatin1(Theme::Role::kMeta));
    bar_layout->addWidget(content_title_);
    bar_layout->addWidget(content_detail_);
    bar_layout->addStretch(1);
    right_layout->addWidget(bar);

    // ---- warning strip --------------------------------------------
    // Only ever visible when an area of the shared region is actually
    // running out. Dismissing it hides it until the count doubles,
    // which is the same rule the render server uses for its own
    // warnings.
    warn_strip_ = new QWidget;
    warn_strip_->setObjectName(QString::fromLatin1(Theme::Role::kWarnStrip));
    auto* warn_layout = new QHBoxLayout(warn_strip_);
    warn_layout->setContentsMargins(14, 9, 14, 9);
    warn_layout->setSpacing(12);
    warn_text_ = new QLabel;
    warn_text_->setObjectName(QString::fromLatin1(Theme::Role::kWarnText));
    warn_text_->setWordWrap(true);
    auto* dismiss = new QPushButton(QStringLiteral("Dismiss"));
    dismiss->setObjectName(QString::fromLatin1(Theme::Role::kQuiet));
    auto* show_render = new QPushButton(QStringLiteral("Show the render channel"));
    warn_layout->addWidget(warn_text_, 1);
    warn_layout->addWidget(dismiss);
    warn_layout->addWidget(show_render);
    warn_strip_->setVisible(false);
    right_layout->addWidget(warn_strip_);

    connect(dismiss, &QPushButton::clicked, this, [this] {
        warning_dismissed_ = true;
        warn_strip_->setVisible(false);
    });
    connect(show_render, &QPushButton::clicked, this,
            [this] { show_page(NavRail::Page::Render); });

    stack_ = new QStackedWidget;
    right_layout->addWidget(stack_, 1);
    columns->addWidget(right, 1);
    setCentralWidget(central);

    build_pages();

    // ---- status bar -----------------------------------------------
    status_left_ = new QLabel;
    status_left_->setObjectName(QString::fromLatin1(Theme::Role::kMeta));
    statusBar()->addWidget(status_left_);
    statusBar()->setSizeGripEnabled(false);

    connect(rail_, &NavRail::page_selected, this, &MainWindow::show_page);
    connect(rail_, &NavRail::start_requested, overview_, &VmDetailPanel::start_guest);
    connect(rail_, &NavRail::stop_requested, overview_, &VmDetailPanel::stop_guest);

    connect(overview_, &VmDetailPanel::running_changed, this, [this](bool running) {
        rail_->set_running(running);
        refresh_status_bar();
    });
    connect(overview_, &VmDetailPanel::agent_online_changed, this, [this](bool online) {
        rail_->set_agent_online(online);
        refresh_status_bar();
    });
    connect(log_, &LogPanel::record_count_changed, rail_, &NavRail::set_log_count);
    connect(&services_.pending_changes(), &config::PendingChanges::changed, this,
            [this] { rail_->set_pending_changes(services_.pending_changes().count()); });
    connect(&services_.render_stats(), &vm::RenderStats::updated, this,
            &MainWindow::refresh_warning);

    // Single-VM model: pin the pages to whatever record App populated
    // for the lone ``lsw`` domain on startup.
    if (!services_.vms().records().isEmpty()) {
        overview_->set_target(services_.vms().records().first().uuid);
    }
    refresh_identity();
    refresh_status_bar();
    show_page(NavRail::Page::Overview);
    setup_tray();
}

void MainWindow::build_pages() {
    overview_ = new VmDetailPanel(services_.vms(), services_.lifecycle(),
                                  services_.monitor(), services_.app_configs(),
                                  services_.rpc(), services_.log_store(), this);
    console_ = new ConsolePanel(services_.rpc(), this);
    log_ = new LogPanel(services_.log_store(), this);
    render_ = new RenderPanel(services_.render_stats(), services_.manager_settings(),
                              services_.pending_changes(), this);
    settings_page_ = new SettingsPage(services_.manager_settings(),
                                      services_.pending_changes(),
                                      services_.app_configs(), this);

    // Order must match NavRail::Page.
    stack_->addWidget(overview_);
    stack_->addWidget(console_);
    stack_->addWidget(log_);
    stack_->addWidget(render_);
    stack_->addWidget(settings_page_);

    connect(render_, &RenderPanel::settings_requested, this,
            [this] { show_page(NavRail::Page::Settings); });
    connect(settings_page_, &SettingsPage::restart_requested, this,
            &MainWindow::restart_guest);
    connect(settings_page_, &SettingsPage::open_share_requested, this, [this] {
        // The share is served in userspace by the file manager, so this
        // is a URL rather than a mount.
        const auto* record = services_.vms().records().isEmpty()
                                 ? nullptr
                                 : &services_.vms().records().first();
        Q_UNUSED(record);
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("smb://127.0.0.1:5445/C$")));
    });
}

void MainWindow::show_page(NavRail::Page page) {
    const int index = static_cast<int>(page);
    if (index < 0 || index >= stack_->count()) {
        return;
    }
    stack_->setCurrentIndex(index);
    rail_->set_current(page);
    content_title_->setText(QString::fromLatin1(kPages[index].title));
    content_detail_->setText(QString::fromLatin1(kPages[index].detail));

    // A console nobody is looking at should not be holding a shell open
    // in the guest.
    if (page != NavRail::Page::Console && console_ != nullptr) {
        console_->release_session();
    }
}

void MainWindow::refresh_identity() {
    if (services_.vms().records().isEmpty()) {
        rail_->set_identity(QStringLiteral("lsw"), QStringLiteral("not provisioned"));
        return;
    }
    const auto& record = services_.vms().records().first();
    auto& settings = services_.manager_settings();
    const QString detail =
        QStringLiteral("%1\n%2 cores · %3 GiB · %4 GiB")
            .arg(record.edition.isEmpty() ? QStringLiteral("Windows") : record.edition)
            .arg(settings.int_value(QLatin1String(K::kCpuCores)))
            .arg(settings.int_value(QLatin1String(K::kMemoryMib)) / 1024)
            .arg(settings.int_value(QLatin1String(K::kDiskGib)));
    rail_->set_identity(record.name.isEmpty() ? QStringLiteral("lsw") : record.name, detail);
}

void MainWindow::refresh_status_bar() {
    auto& settings = services_.manager_settings();
    const int channels = settings.int_value(QLatin1String(K::kChannels));
    const QString text =
        QStringLiteral("agent 127.0.0.1:%1   ·   transport ivshmem, doorbell vector 0   "
                       "·   disk virtio-blk, iothread   ·   render %2 channel%3   ·   %4")
            .arg(settings.int_value(QLatin1String(K::kAgentPort)))
            .arg(channels)
            .arg(channels == 1 ? QString{} : QStringLiteral("s"))
            .arg(overview_ != nullptr && overview_->is_agent_online()
                     ? QStringLiteral("agent answering")
                     : QStringLiteral("agent not reachable"));
    status_left_->setText(text);
}

void MainWindow::refresh_warning() {
    rail_->set_render_warning(services_.render_stats().worst()
                              != vm::AreaPressure::State::Healthy);
    if (!services_.manager_settings().bool_value(QLatin1String(K::kWarnCongestion))) {
        warn_strip_->setVisible(false);
        return;
    }
    const QString headline = services_.render_stats().headline();
    if (headline.isEmpty()) {
        warning_dismissed_ = false;
        warn_strip_->setVisible(false);
        return;
    }
    const bool critical =
        services_.render_stats().worst() == vm::AreaPressure::State::Congested;
    warn_strip_->setObjectName(QString::fromLatin1(
        critical ? Theme::Role::kWarnStripCritical : Theme::Role::kWarnStrip));
    warn_text_->setObjectName(QString::fromLatin1(
        critical ? Theme::Role::kWarnTextCritical : Theme::Role::kWarnText));
    warn_strip_->style()->unpolish(warn_strip_);
    warn_strip_->style()->polish(warn_strip_);
    warn_text_->style()->unpolish(warn_text_);
    warn_text_->style()->polish(warn_text_);
    warn_text_->setText(headline);
    warn_strip_->setVisible(!warning_dismissed_);
}

void MainWindow::restart_guest() {
    // The values are already current; what is left is to make the guest
    // pick them up. Stop, then start: the render server's sizes are
    // command-line arguments and the domain XML is rebuilt on start.
    if (services_.vms().records().isEmpty()) {
        return;
    }
    services_.apply_render_sizing();
    overview_->stop_guest();
    overview_->start_guest();
    refresh_identity();
    refresh_status_bar();
}

void MainWindow::setup_tray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    tray_ = new QSystemTrayIcon(this);
    tray_->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    tray_->setToolTip(QStringLiteral("LSW"));
    auto* menu = new QMenu(this);
    menu->addAction(QStringLiteral("Show"), this, [this]() {
        show();
        raise();
        activateWindow();
    });
    menu->addAction(QStringLiteral("Settings"), this,
                    [this] { show_page(NavRail::Page::Settings); show(); raise(); });
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
            show();
            raise();
            activateWindow();
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    const bool to_tray =
        services_.manager_settings().bool_value(QLatin1String(K::kCloseToTray));
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
