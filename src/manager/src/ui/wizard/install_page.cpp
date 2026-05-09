#include "lsw/manager/ui/wizard/install_page.hpp"

#include "lsw/log.hpp"
#include "lsw/manager/ui/wizard/wizard.hpp"
#include "lsw/manager/wizard/pipeline.hpp"

#include <QTimer>
#include <QVBoxLayout>
#include <QWizard>

namespace lsw::manager::ui::wizard {

InstallPage::InstallPage(Wizard& wizard, QWidget* parent) : QWizardPage(parent), wizard_(wizard) {
    setTitle("Installing Windows");
    setSubTitle("Booting the VM and running the unattended installer.");

    auto* lay = new QVBoxLayout(this);
    bar_ = new QProgressBar;
    bar_->setRange(0, 0);  // indeterminate
    status_ = new QLabel("Preparing...");
    status_->setWordWrap(true);
    vnc_ = new VncView;

    lay->addWidget(bar_);
    lay->addWidget(status_);
    lay->addWidget(vnc_, /*stretch=*/1);
}

void InstallPage::initializePage() {
    complete_ = false;
    Q_EMIT completeChanged();

    auto* p = wizard_.pipeline();
    if (p == nullptr) {
        status_->setText("Internal error: pipeline not started.");
        return;
    }
    using Step = ::lsw::manager::wizard::Pipeline::Step;

    // The pipeline phases this page is responsible for displaying.
    // BuildingPayloadIso is now shown by PayloadPage; this page picks
    // up from CreatingDisk onward.
    const auto is_install_phase = [](Step s) {
        return s == Step::CreatingDisk
            || s == Step::StartingVm || s == Step::WaitingForAgent;
    };

    connect(p, &::lsw::manager::wizard::Pipeline::step_started, this,
            [this, is_install_phase](Step s, const QString& desc) {
                if (!is_install_phase(s)) return;
                status_->setText(desc);
            });
    connect(p, &::lsw::manager::wizard::Pipeline::step_progress, this,
            [this, is_install_phase](Step s, double frac, const QString& detail) {
                if (!is_install_phase(s)) return;
                if (!detail.isEmpty()) status_->setText(detail);
                // A real fraction (e.g. the virtio-win.iso download
                // inside BuildingPayloadIso) → determinate bar;
                // anything indeterminate → busy spinner.
                if (frac >= 0.0 && frac <= 1.0) {
                    bar_->setRange(0, 100);
                    bar_->setValue(static_cast<int>(frac * 100));
                } else {
                    bar_->setRange(0, 0);
                }
            });
    connect(p, &::lsw::manager::wizard::Pipeline::failed, this,
            [this, is_install_phase](Step s, const QString& err) {
                if (!is_install_phase(s)) return;
                status_->setText(QStringLiteral("Install failed: %1").arg(err));
            });
    // The Pipeline emits step_finished(WaitingForAgent) right before
    // it transitions into the Setup phase — that's our signal to
    // advance the wizard to the Setup page.
    connect(p, &::lsw::manager::wizard::Pipeline::step_finished, this, [this](Step s) {
        if (s != Step::WaitingForAgent) return;
        bar_->setRange(0, 100);
        bar_->setValue(100);
        status_->setText("Guest agent reachable — install finished.");
        complete_ = true;
        Q_EMIT completeChanged();
        QTimer::singleShot(0, this, [this] {
            if (auto* w = wizard()) w->next();
        });
    });
    connect(p, &::lsw::manager::wizard::Pipeline::vnc_port_available, this, [this](int port) {
        LSW_INFO("install_page: attaching VncView to 127.0.0.1:{}", port);
        vnc_->connect_to("127.0.0.1", port);
    });
}

bool InstallPage::isComplete() const {
    return complete_;
}

int InstallPage::nextId() const {
    return Wizard::Setup;
}

}  // namespace lsw::manager::ui::wizard
