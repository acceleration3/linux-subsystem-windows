#include "lsw/host/manager/vm_detail_panel.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

using ui::Theme;

constexpr quint32 kAgentPort = 5102;  // see docs/transport.md
constexpr int kStatePollMs = 2000;

// A panel is a bordered frame with an uppercase caption inside it,
// rather than a QGroupBox with its title floating on the border. The
// difference is that the caption can carry buttons on the same row,
// which is where the app grid's refresh and add live.
struct Panel {
    QFrame* frame = nullptr;
    QHBoxLayout* header = nullptr;
    QVBoxLayout* body = nullptr;
};

Panel make_panel(const QString& caption) {
    Panel panel;
    panel.frame = new QFrame;
    panel.frame->setObjectName(QString::fromLatin1(Theme::Role::kPanel));

    auto* outer = new QVBoxLayout(panel.frame);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* header_widget = new QWidget;
    header_widget->setObjectName(QString::fromLatin1(Theme::Role::kPanelHeader));
    panel.header = new QHBoxLayout(header_widget);
    panel.header->setContentsMargins(12, 8, 8, 8);
    panel.header->setSpacing(8);

    auto* label = new QLabel(caption.toUpper());
    label->setObjectName(QString::fromLatin1(Theme::Role::kCaption));
    panel.header->addWidget(label);
    panel.header->addStretch(1);

    auto* body_widget = new QWidget;
    panel.body = new QVBoxLayout(body_widget);
    panel.body->setContentsMargins(12, 12, 12, 12);
    panel.body->setSpacing(10);

    outer->addWidget(header_widget);
    outer->addWidget(body_widget, 1);
    return panel;
}

}  // namespace

VmDetailPanel::VmDetailPanel(config::VmState& vms, vm::Lifecycle& lifecycle,
                             vm::Monitor& monitor, apps::AppConfigStore& app_configs,
                             link::AgentRpcClient& rpc, apps::LogStore& log_store,
                             QWidget* parent)
    : QWidget(parent),
      vms_(vms),
      lifecycle_(lifecycle),
      monitor_(monitor),
      rpc_(rpc),
      log_store_(log_store) {
    setObjectName(QStringLiteral("VmDetailPanel"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(0);

    // Left column is the machine, right column is what runs on it. The
    // split favours the right because the applications are the point;
    // the preview is confirmation that the machine is alive.
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(14);

    auto* left_col = new QWidget;
    auto* left_layout = new QVBoxLayout(left_col);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(14);

    auto preview_panel = make_panel(QStringLiteral("Live preview"));
    preview_panel.body->setContentsMargins(10, 10, 10, 10);
    preview_panel.body->setSpacing(8);
    // What the picture is, for when there is not one yet.
    preview_tag_ = new QLabel(QStringLiteral("VNC 127.0.0.1"));
    preview_tag_->setObjectName(QStringLiteral("ScreenTag"));
    preview_panel.body->addWidget(preview_tag_, 0, Qt::AlignLeft | Qt::AlignTop);
    preview_ = new lsw::host::ui::VncView;
    preview_->setMinimumHeight(220);
    preview_->set_controls_visible(true);
    preview_slot_ = preview_panel.body;
    preview_slot_->addWidget(preview_, 1);
    left_layout->addWidget(preview_panel.frame, 1);

    auto stats_panel = make_panel(QStringLiteral("System"));
    stats_ = new StatsWidget;
    stats_panel.body->addWidget(stats_);
    left_layout->addWidget(stats_panel.frame);

    splitter->addWidget(left_col);

    auto apps_panel = make_panel(QStringLiteral("Installed applications"));
    apps_panel.body->setContentsMargins(0, 0, 0, 0);
    app_list_ = new AppListWidget(app_configs, rpc_);
    // The list carries its own toolbar; hoisting its actions into the
    // panel header is what makes the two panels read as one object.
    if (auto* toolbar = app_list_->toolbar()) {
        toolbar->setParent(nullptr);
        apps_panel.header->addWidget(toolbar);
    }
    apps_panel.body->addWidget(app_list_, 1);
    splitter->addWidget(apps_panel.frame);

    splitter->setStretchFactor(0, 38);
    splitter->setStretchFactor(1, 62);
    splitter->setSizes({400, 640});
    root->addWidget(splitter, 1);

    connect(&monitor_, &vm::Monitor::stats_updated, this, &VmDetailPanel::on_stats_updated);

    state_timer_.setInterval(kStatePollMs);
    connect(&state_timer_, &QTimer::timeout, this, &VmDetailPanel::on_state_tick);
}

void VmDetailPanel::pop_out_preview() {
    if (preview_ == nullptr || preview_->window() != window()) {
        return;  // already out
    }
    auto* holder = new QWidget(nullptr, Qt::Window);
    holder->setAttribute(Qt::WA_DeleteOnClose);
    holder->setWindowTitle(QStringLiteral("LSW preview"));
    holder->resize(960, 600);
    auto* layout = new QVBoxLayout(holder);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(preview_);

    // Back into the panel when the window goes, so the page is not
    // left with a hole in it.
    connect(holder, &QObject::destroyed, this, [this] {
        if (preview_slot_ != nullptr && preview_ != nullptr) {
            preview_slot_->addWidget(preview_);
            preview_->show();
        }
    });
    holder->show();
}

void VmDetailPanel::set_target(const QUuid& uuid) {
    if (!uuid_.isNull() && uuid_ != uuid) {
        monitor_.unwatch(uuid_);
    }
    uuid_ = uuid;
    // Start every new target as offline until the Monitor's next agent
    // poll proves otherwise, so switching targets never flashes the
    // previous one's online badge.
    last_agent_reachable_ = false;
    Q_EMIT agent_online_changed(false);

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
        rpc_.attach_shmem(lifecycle_.ivshmem_server(uuid_), lifecycle_.shmem_region(uuid_));
        rpc_.set_target(rec->vsock_cid, kAgentPort);
        const int vp = lifecycle_.vnc_port(uuid_);
        if (vp > 0) {
            preview_tag_->setText(QStringLiteral("VNC 127.0.0.1:%1").arg(vp));
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

void VmDetailPanel::on_state_tick() {
    if (uuid_.isNull()) {
        return;
    }
    const bool running = lifecycle_.is_running(uuid_);
    if (running != last_running_) {
        last_running_ = running;
        Q_EMIT running_changed(running);
        // Track the live preview to the domain lifecycle, but only on
        // the transition, so a user who stopped the preview by hand is
        // not overridden every tick.
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

void VmDetailPanel::start_guest() {
    if (uuid_.isNull()) {
        return;
    }
    // ``start`` rather than ``define_and_start``: the wizard leaves the
    // live XML in the virtio shape, and redefining here would silently
    // revert it to the manager's defaults.
    //
    // Whether the guest has booted before decides whether it should
    // still be carrying the install media, which is where a VM
    // provisioned before the eject existed gets noticed.
    const auto* record = vms_.find(uuid_);
    (void)lifecycle_.start(uuid_, record != nullptr && record->first_boot_done);
    on_state_tick();
}

void VmDetailPanel::stop_guest() {
    if (uuid_.isNull()) {
        return;
    }
    (void)lifecycle_.shutdown(uuid_);
    on_state_tick();
}

void VmDetailPanel::on_stats_updated(const QUuid& uuid, const vm::VmStats& stats) {
    if (uuid != uuid_) {
        return;
    }
    if (stats.agent_reachable != last_agent_reachable_) {
        last_agent_reachable_ = stats.agent_reachable;
        Q_EMIT agent_online_changed(last_agent_reachable_);
    }
    stats_->update_stats(stats);
    Q_EMIT stats_changed(stats);
}

}  // namespace lsw::host::manager
