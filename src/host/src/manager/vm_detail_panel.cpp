#include "lsw/host/manager/vm_detail_panel.hpp"

#include "lsw/host/services/host_services.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/ui/panel.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

// A panel whose caption row can carry buttons beside the name, which
// is where the app grid's refresh and add live.
ui::Panel make_panel(const QString& caption) {
    auto panel = ui::Panel::make(caption);
    panel.header->setContentsMargins(12, 8, 8, 8);
    panel.header->setSpacing(8);
    panel.header->addStretch(1);
    return panel;
}

}  // namespace

VmDetailPanel::VmDetailPanel(services::HostServices& services, QWidget* parent)
    : QWidget(parent), services_(services) {
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
    preview_tag_ = new QLabel(QStringLiteral("VNC, no guest"));
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
    app_list_ = new AppListWidget(services_.app_configs(), services_.rpc());
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

    connect(&services_.status(), &services::StatusHub::changed, this, &VmDetailPanel::on_status);
    // The preview switch in Settings is live: it starts or stops the
    // stream on a running guest at once.
    connect(&services_.manager_settings(), &config::ManagerSettings::changed, this,
            [this](const QString& key) {
                if (key == config::ManagerSettings::Key::kLivePreview) {
                    follow_preview(last_running_);
                }
            });
}

void VmDetailPanel::follow_preview(bool running) {
    if (uuid_.isNull() || !running) {
        preview_->stop();
        preview_tag_->setText(uuid_.isNull() ? QStringLiteral("VNC, no guest")
                                             : QStringLiteral("VNC, the guest is off"));
        return;
    }
    // The port libvirt assigned this boot; it changes from one to the next.
    const int vp = services_.lifecycle().vnc_port(uuid_);
    if (vp <= 0) {
        preview_->stop();
        preview_tag_->setText(QStringLiteral("VNC, no display port yet"));
        return;
    }
    if (!services_.manager_settings().bool_value(config::ManagerSettings::Key::kLivePreview)) {
        preview_->stop();
        preview_tag_->setText(QStringLiteral("VNC 127.0.0.1:%1, preview off in Settings").arg(vp));
        return;
    }
    preview_tag_->setText(QStringLiteral("VNC 127.0.0.1:%1").arg(vp));
    if (!preview_->is_streaming()) {
        preview_->connect_to(QStringLiteral("127.0.0.1"), vp);
    }
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
    auto& lifecycle = services_.lifecycle();
    auto& rpc = services_.rpc();
    uuid_ = uuid;
    services_.status().set_target(uuid);
    // Start every new target as offline until the Monitor's next agent
    // poll proves otherwise, so switching targets never flashes the
    // previous one's online badge.
    last_agent_reachable_ = false;
    Q_EMIT agent_online_changed(false);

    if (uuid_.isNull()) {
        follow_preview(false);
        app_list_->clear_target();
        stats_->clear();
        rpc.detach_shmem();
        rpc.clear_target();
        services_.log_store().clear();
        return;
    }
    if (services_.bind_agent(uuid_)) {
        // libvirt's word now, before the hub's first reading: the
        // transition that reading reports is what the window follows.
        follow_preview(lifecycle.is_running(uuid_));
        app_list_->set_target(uuid_);
        services_.log_store().clear();
    }
    on_status();
}

bool VmDetailPanel::start_guest() {
    if (uuid_.isNull()) {
        return false;
    }
    // ``start`` rather than ``define_and_start``: the wizard leaves the
    // live XML in the virtio shape, and redefining here would silently
    // revert it to the manager's defaults.
    return services_.start_vm(uuid_) == 0;
}

void VmDetailPanel::stop_guest() {
    if (uuid_.isNull()) {
        return;
    }
    (void)services_.lifecycle().shutdown(uuid_);
}

void VmDetailPanel::on_status() {
    const auto& status = services_.status();
    stats_->set_vram(status.graphics().vram);
    if (uuid_.isNull() || status.vm().uuid != uuid_) {
        return;
    }
    const auto& stats = status.vm().stats;
    if (stats.running != last_running_) {
        last_running_ = stats.running;
        Q_EMIT running_changed(last_running_);
        // Track the live preview to the domain lifecycle, but only on
        // the transition, so a user who stopped the preview by hand is
        // not overridden every tick.
        follow_preview(last_running_);
    }
    if (stats.agent_reachable != last_agent_reachable_) {
        last_agent_reachable_ = stats.agent_reachable;
        Q_EMIT agent_online_changed(last_agent_reachable_);
        // What is installed is the agent's to say; the grid shows the
        // stored list until it can.
        if (last_agent_reachable_) {
            app_list_->refresh();
        }
    }
    stats_->update_stats(stats);
}

}  // namespace lsw::host::manager
