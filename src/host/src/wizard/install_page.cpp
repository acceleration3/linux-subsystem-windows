#include "lsw/host/wizard/install_page.hpp"
#include "lsw/host/wizard/page_chrome.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/wizard/wizard.hpp"
#include "lsw/host/provision/pipeline.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QTimer>
#include <QVBoxLayout>
#include <QWizard>

namespace lsw::host::wizard {

InstallPage::InstallPage(Wizard& wizard, QWidget* parent) : QWizardPage(parent), wizard_(wizard) {

    auto* lay = PageChrome::install(this, QStringLiteral("Installing Windows"), QStringLiteral("Booting the VM and running the unattended installer."));
    // In the title band with the title: the wait is what this page is,
    // and how long it has been going is the first thing to read.
    auto* head = PageChrome::head_of(this);
    auto* progress_row = new QHBoxLayout;
    progress_row->setSpacing(12);
    bar_ = new QProgressBar;
    bar_->setRange(0, 0);  // indeterminate
    elapsed_ = new QLabel;
    elapsed_->setObjectName(QString::fromLatin1(ui::Theme::Role::kMeta));
    progress_row->addWidget(bar_, 1);
    progress_row->addWidget(elapsed_);
    head->addLayout(progress_row);

    status_ = new QLabel(QStringLiteral("Windows setup usually takes 10 to 30 minutes "
                                        "and reboots itself several times."));
    status_->setObjectName(QString::fromLatin1(ui::Theme::Role::kDim));
    status_->setWordWrap(true);
    head->addWidget(status_);

    // The screen is the content. A bordered frame around the guest's
    // console, with the same corner and line as every other panel.
    auto* screen = new QFrame;
    screen->setObjectName(QString::fromLatin1(ui::Theme::Role::kScreen));
    auto* screen_layout = new QVBoxLayout(screen);
    screen_layout->setContentsMargins(10, 10, 10, 10);
    screen_layout->setSpacing(8);
    auto* tag = new QLabel(QStringLiteral("GUEST CONSOLE"));
    tag->setObjectName(QStringLiteral("ScreenTag"));
    screen_layout->addWidget(tag, 0, Qt::AlignLeft | Qt::AlignTop);
    vnc_ = new ui::VncView;
    screen_layout->addWidget(vnc_, 1);
    lay->addWidget(screen, /*stretch=*/1);
}

void InstallPage::set_phase(const QString& text) {
    // The head keeps its sentence about how long this takes; the phase
    // rides beside the elapsed time, where the eye already is.
    phase_ = text;
    elapsed_->setText(text);
}

void InstallPage::initializePage() {
    complete_ = false;
    Q_EMIT completeChanged();

    auto* p = wizard_.pipeline();
    if (p == nullptr) {
        set_phase("Internal error: pipeline not started.");
        return;
    }
    using Step = ::lsw::host::provision::Pipeline::Step;

    // The pipeline phases this page is responsible for displaying.
    // BuildingPayloadIso is now shown by PayloadPage; this page picks
    // up from CreatingDisk onward.
    const auto is_install_phase = [](Step s) {
        return s == Step::CreatingDisk
            || s == Step::StartingVm || s == Step::WaitingForAgent;
    };

    connect(p, &::lsw::host::provision::Pipeline::step_started, this,
            [this, is_install_phase](Step s, const QString& desc) {
                if (!is_install_phase(s)) return;
                set_phase(desc);
            });
    connect(p, &::lsw::host::provision::Pipeline::step_progress, this,
            [this, is_install_phase](Step s, double frac, const QString& detail) {
                if (!is_install_phase(s)) return;
                if (!detail.isEmpty()) set_phase(detail);
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
    connect(p, &::lsw::host::provision::Pipeline::failed, this,
            [this, is_install_phase](Step s, const QString& err) {
                if (!is_install_phase(s)) return;
                set_phase(QStringLiteral("Install failed: %1").arg(err));
            });
    // The Pipeline emits step_finished(WaitingForAgent) right before
    // it transitions into the Setup phase — that's our signal to
    // advance the wizard to the Setup page.
    connect(p, &::lsw::host::provision::Pipeline::step_finished, this, [this](Step s) {
        if (s != Step::WaitingForAgent) return;
        bar_->setRange(0, 100);
        bar_->setValue(100);
        set_phase("The guest agent is answering. The install is finished.");
        complete_ = true;
        Q_EMIT completeChanged();
        QTimer::singleShot(0, this, [this] {
            if (auto* w = wizard()) w->next();
        });
    });
    connect(p, &::lsw::host::provision::Pipeline::vnc_port_available, this, [this](int port) {
        LSW_INFO("install_page: attaching ui::VncView to 127.0.0.1:{}", port);
        vnc_->connect_to("127.0.0.1", port);
    });
}

bool InstallPage::isComplete() const {
    return complete_;
}

int InstallPage::nextId() const {
    return Wizard::Setup;
}

}  // namespace lsw::host::wizard
