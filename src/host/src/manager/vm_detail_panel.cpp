#include "lsw/host/manager/vm_detail_panel.hpp"

#include "lsw/host/manager/console_window.hpp"
#include "lsw/host/manager/log_viewer.hpp"

#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

constexpr quint32 kAgentPort = 5102;  // see docs/transport.md
constexpr int kStatePollMs = 2000;

// Stylesheet for the panel — dark-ish neutral with accents on the
// running-state indicator and action buttons. Defined once so the
// layout code stays focused on structure.
constexpr const char* kPanelStyle = R"(
QWidget#VmDetailPanel {
    background-color: #1e1e23;
    color: #e0e0e0;
}
QGroupBox {
    border: 1px solid #3a3a44;
    border-radius: 6px;
    margin-top: 14px;
    padding: 10px 8px 8px 8px;
    background-color: #25252c;
    color: #c8c8d4;
    font-weight: bold;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 6px;
    color: #8aa0c8;
}
QPushButton {
    background-color: #2e2e38;
    color: #e0e0e0;
    border: 1px solid #44445a;
    border-radius: 4px;
    padding: 4px 12px;
    min-height: 22px;
}
QPushButton:hover  { background-color: #3a3a48; border-color: #5a5a72; }
QPushButton:pressed{ background-color: #4a4a5c; }
QPushButton:disabled { color: #666; border-color: #2a2a32; background-color: #232328; }
QPushButton#StartButton:!disabled { color: #6fcf97; }
QPushButton#StopButton:!disabled  { color: #eb5757; }
QLabel#TitleLabel {
    color: #f0f0f0;
    font-size: 20px;
    font-weight: bold;
}
QLabel#StatusBadge {
    border-radius: 2px;
    border: 1px solid rgba(255, 255, 255, 60);
    padding: 2px 10px;
    background-color: #2a2a32;
    color: #a8a8b8;
    font-weight: bold;
}
QLabel#StatusBadgeRunning {
    border-radius: 2px;
    border: 1px solid #a8e6c4;
    padding: 2px 10px;
    background-color: rgba(111, 207, 151, 40);
    color: #6fcf97;
    font-weight: bold;
}
QLabel#StatusBadgeStopped {
    border-radius: 2px;
    border: 1px solid #f5a3a3;
    padding: 2px 10px;
    background-color: rgba(235, 87, 87, 38);
    color: #eb5757;
    font-weight: bold;
}
)";

}  // namespace

VmDetailPanel::VmDetailPanel(config::VmState& vms, vm::Lifecycle& lifecycle, vm::Monitor& monitor,
                             apps::AppConfigStore& app_configs, link::AgentRpcClient& rpc,
                             apps::LogStore& log_store,
                             QWidget* parent)
    : QWidget(parent),
      vms_(vms),
      lifecycle_(lifecycle),
      monitor_(monitor),
      rpc_(rpc),
      log_store_(log_store) {
    setObjectName(QStringLiteral("VmDetailPanel"));
    setStyleSheet(QString::fromUtf8(kPanelStyle));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    // ---- Header bar -----------------------------------------------------
    auto* header_frame = new QFrame;
    header_frame->setObjectName(QStringLiteral("HeaderFrame"));
    auto* header = new QHBoxLayout(header_frame);
    header->setContentsMargins(4, 4, 4, 4);
    header->setSpacing(10);

    // The window title and the VM record's name are both "LSW" /
    // "lsw" — the old header label was redundant. The status badge
    // alone is enough at the top-left; if we ever support multiple
    // VMs the title label can come back.
    title_label_ = nullptr;

    status_label_ = new QLabel;
    status_label_->setObjectName(QStringLiteral("StatusBadge"));
    status_label_->setAlignment(Qt::AlignCenter);
    header->addWidget(status_label_);

    header->addStretch(1);

    start_button_ = new QPushButton(QStringLiteral("▶ Start"));
    start_button_->setObjectName(QStringLiteral("StartButton"));
    stop_button_ = new QPushButton(QStringLiteral("■ Stop"));
    stop_button_->setObjectName(QStringLiteral("StopButton"));
    open_console_button_ = new QPushButton(QStringLiteral("⌨ Command Line"));
    open_log_button_ = new QPushButton(QStringLiteral("≣ Log"));

    for (auto* b : {start_button_, stop_button_, open_console_button_, open_log_button_}) {
        b->setEnabled(false);
        header->addWidget(b);
    }
    root->addWidget(header_frame);

    // log_store_ is already wired to rpc_ by App at construction —
    // no per-panel attach needed.

    // ---- Body splitter --------------------------------------------------
    // Left column: preview + stats stacked. Right column: app grid.
    // Splitter lets the user adjust the divide between preview and
    // app grid.
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    // Wide handle + transparent fill — gives breathing room between
    // the left (preview + stats) and right (apps) panes instead of
    // them butting up against a thin grabber.
    splitter->setHandleWidth(16);
    splitter->setStyleSheet(QStringLiteral(
        "QSplitter::handle { background: transparent; }"));

    // -- Left column (preview + stats) --
    auto* left_col = new QWidget;
    auto* left_layout = new QVBoxLayout(left_col);
    // Small right-side margin so the groupbox border doesn't kiss
    // the splitter handle; mirrored on the right pane below.
    left_layout->setContentsMargins(0, 0, 4, 0);
    left_layout->setSpacing(10);

    auto* preview_group = new QGroupBox(QStringLiteral("Live preview"));
    auto* preview_layout = new QVBoxLayout(preview_group);
    preview_layout->setContentsMargins(8, 8, 8, 8);
    preview_ = new lsw::host::ui::VncView;
    preview_->setMinimumHeight(220);
    preview_->set_controls_visible(true);  // hover Start/Stop + grey fade
    preview_layout->addWidget(preview_);
    left_layout->addWidget(preview_group);

    auto* stats_group = new QGroupBox(QStringLiteral("System"));
    auto* stats_layout = new QVBoxLayout(stats_group);
    stats_layout->setContentsMargins(8, 8, 8, 8);
    stats_ = new StatsWidget;
    stats_layout->addWidget(stats_);
    left_layout->addWidget(stats_group);

    left_layout->addStretch(1);
    splitter->addWidget(left_col);

    // -- Right column (apps) --
    auto* right_col = new QWidget;
    auto* right_layout = new QVBoxLayout(right_col);
    right_layout->setContentsMargins(4, 0, 0, 0);  // mirror of left pane's right margin
    right_layout->setSpacing(0);
    auto* apps_group = new QGroupBox(QStringLiteral("Installed applications"));
    auto* apps_layout = new QVBoxLayout(apps_group);
    apps_layout->setContentsMargins(8, 8, 8, 8);
    app_list_ = new AppListWidget(app_configs, rpc_);
    apps_layout->addWidget(app_list_, 1);
    right_layout->addWidget(apps_group);
    splitter->addWidget(right_col);

    // 35% left / 65% right by default — the app grid is the main
    // focus, preview is secondary.
    splitter->setStretchFactor(0, 35);
    splitter->setStretchFactor(1, 65);
    splitter->setSizes({420, 780});

    root->addWidget(splitter, 1);

    // ---- Wiring ---------------------------------------------------------
    connect(start_button_, &QPushButton::clicked, this, &VmDetailPanel::on_start_clicked);
    connect(stop_button_, &QPushButton::clicked, this, &VmDetailPanel::on_stop_clicked);
    connect(open_log_button_, &QPushButton::clicked, this, &VmDetailPanel::on_open_log_clicked);
    connect(open_console_button_, &QPushButton::clicked, this,
            &VmDetailPanel::on_open_console_clicked);
    connect(&monitor_, &vm::Monitor::stats_updated, this, &VmDetailPanel::on_stats_updated);

    state_timer_.setInterval(kStatePollMs);
    connect(&state_timer_, &QTimer::timeout, this, &VmDetailPanel::on_state_tick);
}

void VmDetailPanel::set_target(const QUuid& uuid) {
    if (!uuid_.isNull() && uuid_ != uuid) {
        monitor_.unwatch(uuid_);
    }
    uuid_ = uuid;
    // Start every new target as OFFLINE until the Monitor's next
    // agent poll proves otherwise — otherwise switching VMs flashes
    // the previous target's ONLINE badge.
    last_agent_reachable_ = false;
    refresh_header();
    if (uuid_.isNull()) {
        preview_->stop();
        app_list_->clear_target();
        stats_->clear();
        rpc_.detach_shmem();
        rpc_.clear_target();
        log_store_.clear();
        state_timer_.stop();
        return;
    }
    monitor_.watch(uuid_);
    auto* rec = vms_.find(uuid_);
    if (rec != nullptr) {
        // Bind the per-VM IVSHMEM transport BEFORE set_target so the
        // first frame_received signal after connect routes through
        // shmem rather than the legacy TCP envelope path.
        rpc_.attach_shmem(lifecycle_.ivshmem_server(uuid_),
                          lifecycle_.shmem_region(uuid_));
        rpc_.set_target(rec->vsock_cid, kAgentPort);
        const int vp = lifecycle_.vnc_port(uuid_);
        if (vp > 0) {
            preview_->connect_to(QStringLiteral("127.0.0.1"), vp);
        } else {
            preview_->stop();
        }
        app_list_->set_target(uuid_);
        log_store_.clear();
    }
    state_timer_.start();
    on_state_tick();
}

void VmDetailPanel::clear_target() {
    set_target({});
}

void VmDetailPanel::refresh_header() {
    if (uuid_.isNull()) {
        status_label_->setText({});
        status_label_->setObjectName(QStringLiteral("StatusBadge"));
        status_label_->style()->unpolish(status_label_);
        status_label_->style()->polish(status_label_);
        for (auto* b : {start_button_, stop_button_, open_console_button_, open_log_button_}) {
            b->setEnabled(false);
        }
        return;
    }
    // ONLINE is driven by *agent* reachability — libvirt may report
    // "running" for a minute or two before the guest finishes booting
    // and the agent answers, and the manager UI shouldn't pretend the
    // VM is interactive until then. The Start/Stop buttons still
    // track libvirt state because they manage the qemu process,
    // independent of the agent.
    if (last_agent_reachable_) {
        status_label_->setText(QStringLiteral("● ONLINE"));
        status_label_->setObjectName(QStringLiteral("StatusBadgeRunning"));
    } else {
        status_label_->setText(QStringLiteral("○ OFFLINE"));
        status_label_->setObjectName(QStringLiteral("StatusBadgeStopped"));
    }
    // Re-apply stylesheet after objectName change — Qt doesn't
    // re-polish automatically when the selector changes.
    status_label_->style()->unpolish(status_label_);
    status_label_->style()->polish(status_label_);

    start_button_->setEnabled(!last_running_);
    stop_button_->setEnabled(last_running_);
    open_console_button_->setEnabled(last_agent_reachable_);
    // Log button stays available even when stopped — the store may
    // still hold records from the previous run.
    open_log_button_->setEnabled(true);
}

void VmDetailPanel::on_state_tick() {
    if (uuid_.isNull()) {
        return;
    }
    const bool running = lifecycle_.is_running(uuid_);
    if (running != last_running_) {
        last_running_ = running;
        refresh_header();
        // Track the live preview to the domain lifecycle, but only on
        // the transition — so a user who manually hit Stop on the
        // preview isn't overridden every tick.
        if (!running) {
            preview_->stop();
        } else if (!preview_->is_streaming()) {
            const int vp = lifecycle_.vnc_port(uuid_);
            if (vp > 0) {
                preview_->connect_to(QStringLiteral("127.0.0.1"), vp);
            }
        }
    }
}

void VmDetailPanel::on_start_clicked() {
    if (uuid_.isNull()) return;
    // ``start`` (not ``define_and_start``) — we don't want to
    // overwrite the persisted XML with the manager's defaults. The
    // wizard's post-virtio swap leaves the live XML in the
    // virtio-optimised shape; a redefine here would silently revert
    // it back to pre-virtio.
    // Pass whether the guest has booted before: a VM past that point
    // should not still be carrying the install media, and starting is
    // where that gets noticed for one provisioned before the eject
    // existed.
    const auto* record = vms_.find(uuid_);
    (void)lifecycle_.start(uuid_, record != nullptr && record->first_boot_done);
    on_state_tick();
}

void VmDetailPanel::on_stop_clicked() {
    if (uuid_.isNull()) return;
    (void)lifecycle_.shutdown(uuid_);
    on_state_tick();
}

void VmDetailPanel::on_open_log_clicked() {
    auto* viewer = new LogViewer(log_store_, this);
    viewer->setWindowFlag(Qt::Window, true);
    viewer->show();
    viewer->raise();
    viewer->activateWindow();
}

void VmDetailPanel::on_open_console_clicked() {
    auto* w = new ConsoleWindow(rpc_, this);
    w->setWindowFlag(Qt::Window, true);
    w->show();
    w->raise();
    w->activateWindow();
}

void VmDetailPanel::on_stats_updated(const QUuid& uuid, const vm::VmStats& stats) {
    if (uuid != uuid_) return;
    if (stats.agent_reachable != last_agent_reachable_) {
        last_agent_reachable_ = stats.agent_reachable;
        refresh_header();
    }
    stats_->update_stats(stats);
}

}  // namespace lsw::host::manager
