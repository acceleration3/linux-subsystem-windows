#include "lsw/host/manager/main_window.hpp"

#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/vm/domain_xml_inputs.hpp"
#include "lsw/host/vm/render_server.hpp"
#include "lsw/host/manager/console_panel.hpp"
#include "lsw/host/manager/log_panel.hpp"
#include "lsw/host/manager/render_panel.hpp"
#include "lsw/host/manager/settings_page.hpp"
#include "lsw/host/manager/vm_detail_panel.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/services/host_services.hpp"
#include "lsw/host/tools/download_image_dialog.hpp"
#include "lsw/host/tools/trim_image_dialog.hpp"
#include "lsw/host/ui/theme.hpp"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QUrl>
#include <QScreen>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

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
    {"Render channel", ""},
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
    // Console and Log are pages now, which is right for the common
    // case and wrong for the one where you want to watch the log while
    // doing something else. So they can still be a window.
    const auto bar_action = [&](const QString& text) {
        auto* button = new QPushButton(text);
        button->setObjectName(QString::fromLatin1(Theme::Role::kQuiet));
        button->setVisible(false);
        bar_layout->addWidget(button);
        return button;
    };
    preview_button_ = bar_action(QStringLiteral("Pop out preview"));
    clear_button_ = bar_action(QStringLiteral("Clear"));
    reset_counts_button_ = bar_action(QStringLiteral("Reset counts"));
    config_folder_button_ = bar_action(QStringLiteral("Open config folder"));
    pop_out_button_ = bar_action(QStringLiteral("Pop out"));

    connect(pop_out_button_, &QPushButton::clicked, this,
            [this] { pop_out(rail_->current()); });
    connect(preview_button_, &QPushButton::clicked, this, [this] {
        // The preview is one VNC connection and the panel owns it, so
        // this hands the widget itself to a window and takes it back
        // when that window closes rather than opening a second stream.
        if (overview_ != nullptr) {
            overview_->pop_out_preview();
        }
    });
    connect(clear_button_, &QPushButton::clicked, this,
            [this] { services_.log_store().clear(); });
    connect(reset_counts_button_, &QPushButton::clicked, this,
            [this] { services_.render_stats().reset(); });
    connect(config_folder_button_, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(services_.settings().config_dir()));
    });
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

    // ---- status strip ---------------------------------------------
    // Under the content rather than across the whole window: the rail
    // has its own foot, and a bar that runs beneath it reads as
    // belonging to neither.
    auto* strip = new QWidget;
    strip->setObjectName(QString::fromLatin1(Theme::Role::kStatusStrip));
    auto* strip_layout = new QHBoxLayout(strip);
    strip_layout->setContentsMargins(14, 5, 14, 5);
    status_left_ = new QLabel;
    status_left_->setObjectName(QString::fromLatin1(Theme::Role::kMeta));
    strip_layout->addWidget(status_left_, 1);
    right_layout->addWidget(strip);

    columns->addWidget(right, 1);
    setCentralWidget(central);

    build_pages();

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
        if (online) {
            push_display_settings();
        }
    });
    connect(log_, &LogPanel::record_count_changed, rail_, &NavRail::set_log_count);
    connect(&services_.pending_changes(), &config::PendingChanges::changed, this,
            [this] { rail_->set_pending_changes(services_.pending_changes().count()); });
    connect(&services_.render_stats(), &vm::RenderStats::updated, this,
            &MainWindow::refresh_warning);

    // Single-VM model: pin the pages to whatever record App populated
    // for the lone ``lsw`` domain on startup.
    if (!services_.vms().records().isEmpty()) {
        const QUuid uuid = services_.vms().records().first().uuid;
        overview_->set_target(uuid);
        // The render regions are named after the VM, so the page has
        // nothing to read until there is one.
        render_->set_region_prefix(
            vm::RenderServer::region_name(uuid, 0).chopped(2));
    }
    const bool provisioned = !services_.vms().records().isEmpty();
    overview_stack_->setCurrentIndex(provisioned ? 0 : 1);
    rail_->set_guest_provisioned(provisioned);
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

    overview_stack_ = new QStackedWidget;
    overview_stack_->addWidget(overview_);
    overview_stack_->addWidget(build_empty_state());

    // Order must match NavRail::Page.
    stack_->addWidget(overview_stack_);
    stack_->addWidget(console_);
    stack_->addWidget(log_);
    stack_->addWidget(render_);
    stack_->addWidget(settings_page_);

    connect(render_, &RenderPanel::settings_requested, this,
            [this] { show_page(NavRail::Page::Settings); });
    connect(settings_page_, &SettingsPage::restart_requested, this,
            &MainWindow::restart_guest);
    // The image tools are the same dialogs the wizard opens. Nothing
    // about them needs a guest, so they work with the machine off.
    connect(settings_page_, &SettingsPage::download_tool_requested, this, [this] {
        tools::DownloadImageDialog dialog{services_.settings(), this};
        dialog.exec();
    });
    connect(settings_page_, &SettingsPage::trim_tool_requested, this, [this] {
        const QString source = services_.vms().records().isEmpty()
                                   ? QString{}
                                   : services_.vms().records().first().iso_path;
        tools::TrimImageDialog dialog{services_.settings(), source, 1, this};
        dialog.exec();
    });
    connect(settings_page_, &SettingsPage::rewrite_entries_requested, this, [this] {
        // Idempotent, and the point of the button is the case where the
        // files on disk have drifted from what the store says.
        apps::DesktopLauncher::sync(services_.app_configs().configs());
    });
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

void MainWindow::pop_out(NavRail::Page page) {
    // A second instance rather than reparenting the embedded one: the
    // page keeps working where it is, and each console gets its own
    // session id from the agent anyway, which is what makes two of them
    // a sensible thing to have.
    QWidget* content = nullptr;
    QString title;
    if (page == NavRail::Page::Console) {
        content = new ConsolePanel(services_.rpc());
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

QWidget* MainWindow::build_empty_state() {
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(20, 40, 20, 40);
    outer->addStretch(1);

    auto* column = new QVBoxLayout;
    column->setSpacing(14);
    column->setAlignment(Qt::AlignHCenter);

    auto* eyebrow = new QLabel(QStringLiteral("NOTHING INSTALLED"));
    eyebrow->setObjectName(QString::fromLatin1(Theme::Role::kCaption));
    eyebrow->setAlignment(Qt::AlignCenter);

    auto* heading =
        new QLabel(QStringLiteral("There is no Windows guest on this machine yet"));
    heading->setObjectName(QString::fromLatin1(Theme::Role::kHeading));
    heading->setAlignment(Qt::AlignCenter);

    auto* body = new QLabel(
        QStringLiteral("The first run builds one: pick or download an image, size the "
                       "machine, and let the unattended installer do the rest. Around "
                       "25 minutes, mostly unattended."));
    body->setObjectName(QString::fromLatin1(Theme::Role::kDim));
    body->setWordWrap(true);
    body->setAlignment(Qt::AlignCenter);
    body->setMaximumWidth(420);

    auto* run = new QPushButton(QStringLiteral("Run the first-run wizard"));
    run->setObjectName(QString::fromLatin1(Theme::Role::kPrimary));
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
    pop_out_button_->setVisible(page == NavRail::Page::Console
                                || page == NavRail::Page::Log);
    preview_button_->setVisible(page == NavRail::Page::Overview);
    clear_button_->setVisible(page == NavRail::Page::Log);
    reset_counts_button_->setVisible(page == NavRail::Page::Render);
    config_folder_button_->setVisible(page == NavRail::Page::Settings);

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

QString MainWindow::page_detail(NavRail::Page page) const {
    auto& settings = services_.manager_settings();
    switch (page) {
    case NavRail::Page::Overview:
        return overview_ != nullptr && overview_->is_running()
                   ? QStringLiteral("running")
                   : QStringLiteral("stopped");
    case NavRail::Page::Render: {
        const int channels = settings.int_value(QLatin1String(K::kChannels));
        int claimed = 0;
        for (const auto& channel : services_.render_stats().channels()) {
            claimed += channel.claimed ? 1 : 0;
        }
        return QStringLiteral("%1 channels · %2 in use · %3 GiB reserved")
            .arg(channels)
            .arg(claimed)
            .arg(static_cast<double>(channels) * vm::RenderServer::region_mib() / 1024.0,
                 0, 'f', 1);
    }
    case NavRail::Page::Log:
    case NavRail::Page::Console:
    case NavRail::Page::Settings:
    case NavRail::Page::Count:
        break;
    }
    return QString::fromLatin1(kPages[static_cast<int>(page)].detail);
}

void MainWindow::refresh_status_bar() {
    auto& settings = services_.manager_settings();
    const int channels = settings.int_value(QLatin1String(K::kChannels));
    // The facts, with a rule between each, which is what makes a row of
    // them read as a row rather than as a sentence.
    const QStringList facts = {
        QStringLiteral("agent 127.0.0.1:%1")
            .arg(settings.int_value(QLatin1String(K::kAgentPort))),
        QStringLiteral("transport ivshmem %1 GiB, vector 0")
            .arg(vm::RenderServer::region_mib() / 1024.0, 0, 'f', 1),
        QStringLiteral("disk virtio-blk, iothread"),
        QStringLiteral("render %1 channel%2")
            .arg(channels)
            .arg(channels == 1 ? QString{} : QStringLiteral("s")),
        overview_ != nullptr && overview_->is_agent_online()
            ? QStringLiteral("agent answering")
            : QStringLiteral("agent not reachable"),
    };
    status_left_->setText(facts.join(QStringLiteral("   \u2502   ")));
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

void MainWindow::push_display_settings() {
    // The virtual monitor's size, the guest's DPI and the video memory
    // the adapter reports are settings on this side that live on that
    // side, so they go across the moment there is something to send
    // them to. The resolution takes effect at once; the other two are
    // registry state Windows reads at logon and at boot, which is why
    // they are tagged as restart settings in the page that sets them.
    auto& settings = services_.manager_settings();
    int width = settings.int_value(QLatin1String(K::kGuestWidth));
    int height = settings.int_value(QLatin1String(K::kGuestHeight));
    if (settings.bool_value(QLatin1String(K::kMatchHostOutput))) {
        if (const QScreen* screen = this->screen(); screen != nullptr) {
            width = screen->geometry().width();
            height = screen->geometry().height();
        }
    }

    const nlohmann::json request = {
        {"cmd", linkproto::cmd::kSetDisplay},
        {"params",
         {{"width", width},
          {"height", height},
          {"scaling_percent", settings.int_value(QLatin1String(K::kScalingPercent))},
          {"vram_mib", settings.int_value(QLatin1String(K::kVramMib))}}},
    };
    auto* rpc = &services_.rpc();
    (void)QtConcurrent::run([rpc, request] { (void)rpc->call(request); });
}

void MainWindow::restart_guest() {
    // The values are current by the time this runs; what is left is to
    // make the machine take them. Cores, memory, the shared folders and
    // the adapter's video memory are the domain definition rather than
    // properties of a running guest, so the definition is rewritten
    // between the stop and the start.
    if (services_.vms().records().isEmpty()) {
        return;
    }
    services_.apply_render_sizing();

    auto& settings = services_.manager_settings();
    config::VmRecord record = services_.vms().records().first();
    record.sizing.cpu_cores = settings.int_value(QLatin1String(K::kCpuCores));
    record.sizing.memory_mib = settings.int_value(QLatin1String(K::kMemoryMib));
    record.sizing.disk_gib = settings.int_value(QLatin1String(K::kDiskGib));

    vm::DomainXmlInputs inputs;
    // The template names all three CDs, so the definition has to carry
    // them even for a guest that has already ejected the media: an
    // empty source is a definition libvirt will not take. The Windows
    // and payload paths live on the record; virtio-win is where the
    // pipeline put it.
    inputs.virtio_iso =
        QDir(services_.settings().isos_dir()).filePath(QStringLiteral("virtio-win.iso"));
    // The adapter's memory bounds the modes the guest can set, so it
    // follows the resolution rather than capping it: enough for the
    // asked-for mode at 32 bits per pixel, doubled for a back buffer,
    // and never below what QEMU's VGA wants anyway.
    const int width = settings.int_value(QLatin1String(K::kGuestWidth));
    const int height = settings.int_value(QLatin1String(K::kGuestHeight));
    const int needed_kib = ((width * height * 4 * 2) / 1024) + 1024;
    inputs.video_vram_kib = qMax(16384, needed_kib);

    for (const auto& share : settings.host_shares()) {
        vm::DomainShare mapped;
        // The tag is what the guest mounts by, and a drive letter is a
        // guest-side idea, so the letter is the tag: "Z:" becomes "z".
        mapped.tag = share.drive.left(1).toLower();
        mapped.host_path = share.host_path;
        mapped.read_only = share.read_only;
        inputs.shares.push_back(mapped);
    }

    overview_->stop_guest();
    if (services_.lifecycle().redefine(record, inputs) != 0) {
        // A definition libvirt refused is one the guest must not be
        // started against, and the old one is still on disk, so the
        // honest thing is to say nothing was applied.
        status_left_->setText(
            QStringLiteral("libvirt refused the new machine definition; nothing changed"));
        return;
    }
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
