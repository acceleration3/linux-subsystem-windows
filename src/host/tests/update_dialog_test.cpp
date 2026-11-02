#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/manager/update_dialog.hpp"
#include "lsw/host/services/guest_update_status.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/vm/guest_stats_probe.hpp"
#include "lsw/host/vm/libvirt_connection.hpp"
#include "lsw/host/vm/lifecycle.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QLayout>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThreadPool>

#include <cstdio>

using lsw::host::manager::UpdateDialog;
using lsw::host::services::GuestUpdateStatus;
using lsw::host::services::StatusHub;
using State = GuestUpdateStatus::State;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

class NoProbe : public lsw::host::vm::IGuestStatsProbe {
public:
    bool available() const override { return false; }
    lsw::host::vm::GuestStatsSample sample() override { return {}; }
};

QPushButton* button(UpdateDialog& dialog, const QString& text) {
    for (auto* b : dialog.findChildren<QPushButton*>()) {
        if (b->text() == text) {
            return b;
        }
    }
    return nullptr;
}

bool shown(UpdateDialog& dialog, const QString& text) {
    auto* b = button(dialog, text);
    return b != nullptr && b->isVisibleTo(&dialog);
}

GuestUpdateStatus status(State state, const QString& step = {}, const QString& message = {}) {
    GuestUpdateStatus s;
    s.state = state;
    s.step = step;
    s.message = message;
    s.shipped_version = QStringLiteral("0.2.0");
    return s;
}

}

int main(int argc, char** argv) {
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", home.path().toUtf8());
    qputenv("XDG_CACHE_HOME", home.path().toUtf8());
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    lsw::host::link::AgentRpcClient rpc;
    lsw::host::config::Settings settings;
    lsw::host::config::ManagerSettings manager_settings;
    lsw::host::vm::LibvirtConnection libvirt;
    lsw::host::vm::Lifecycle lifecycle{libvirt};
    NoProbe probe;
    StatusHub hub{libvirt, lifecycle, rpc, probe, settings, manager_settings};
    UpdateDialog dialog{hub};

    check(!dialog.isVisible(), "the dialog is not shown before there is a run");
    check(dialog.layout()->sizeConstraint() == QLayout::SetFixedSize,
          "the layout's size constraint is SetFixedSize, so each run opens at the same size");

    hub.set_update(status(State::Succeeded));
    check(!dialog.isVisible(), "a check that finds Windows current does not open it");

    hub.set_update(status(State::Running, QStringLiteral("Preparing files")));
    check(dialog.isVisible(), "the dialog opens when the update snapshot becomes Running");
    check(!shown(dialog, QStringLiteral("Close")) && !shown(dialog, QStringLiteral("Try again")),
          "while it runs neither Close nor Try again is offered");
    app.processEvents();
    const QSize running_size = dialog.size();

    static_cast<QDialog&>(dialog).reject();
    check(dialog.isVisible(), "reject (Escape) is ignored while the run goes");
    dialog.close();
    check(dialog.isVisible(), "the window's close is ignored while the run goes");
    {
        QCloseEvent event;
        QApplication::sendEvent(&dialog, &event);
        check(!event.isAccepted() && dialog.isVisible(), "a close event is refused while the run goes");
    }
    hub.set_update(status(State::Running, QStringLiteral("Sending files to Windows")));
    hub.set_update(status(State::Running, QStringLiteral("Updating Windows")));
    check(dialog.isVisible(), "going from step to step does not close it");

    GuestUpdateStatus failed = status(State::Failed, {}, QStringLiteral("disk full"));
    failed.last_steps = {{QStringLiteral("agent"), QStringLiteral("failed"), QStringLiteral("no space")}};
    hub.set_update(failed);
    check(dialog.isVisible(), "a failed run leaves it open");
    check(shown(dialog, QStringLiteral("Close")) && shown(dialog, QStringLiteral("Try again")),
          "when a run fails Close and Try again appear");
    app.processEvents();

    bool retried = false;
    QObject::connect(&dialog, &UpdateDialog::retry_requested, [&] { retried = true; });
    button(dialog, QStringLiteral("Try again"))->click();
    check(retried, "Try again asks for the run again");

    hub.set_update(status(State::Running, QStringLiteral("Preparing files")));
    app.processEvents();
    check(dialog.isVisible() && !shown(dialog, QStringLiteral("Close")) && !shown(dialog, QStringLiteral("Try again")),
          "a retried run is shown again with no way out");
    check(dialog.size() == running_size, "and opens at the same size as the first run");
    static_cast<QDialog&>(dialog).reject();
    check(dialog.isVisible(), "it is still not to be rejected");

    hub.set_update(status(State::Succeeded));
    check(shown(dialog, QStringLiteral("Close")) && !shown(dialog, QStringLiteral("Try again")),
          "a successful run offers Close and not Try again");
    static_cast<QDialog&>(dialog).reject();
    check(!dialog.isVisible(), "once it has ended it can be closed");

    hub.set_update(status(State::Running, QStringLiteral("Preparing files")));
    check(dialog.isVisible(), "a new run opens it again after it was closed");
    hub.set_update(status(State::Succeeded));
    button(dialog, QStringLiteral("Close"))->click();
    check(!dialog.isVisible(), "Close closes it");
    hub.set_update(status(State::Succeeded, {}, {}));
    check(!dialog.isVisible(), "and a snapshot that is not a new run does not bring it back");

    QThreadPool::globalInstance()->waitForDone();
    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
