#include "lsw/host/wizard/download_page.hpp"

#include "lsw/host/wizard/wizard.hpp"
#include "lsw/host/provision/pipeline.hpp"

#include <QTimer>
#include <QVBoxLayout>
#include <QWizard>

namespace lsw::host::wizard {

DownloadPage::DownloadPage(Wizard& wizard, QWidget* parent) : QWizardPage(parent), wizard_(wizard) {
    setTitle("Downloading Windows");
    setSubTitle("Streaming the ISO from the URL Microsoft handed back.");

    auto* lay = new QVBoxLayout(this);
    status_ = new QLabel;
    status_->setWordWrap(true);
    lay->addWidget(status_);

    bar_ = new QProgressBar;
    bar_->setRange(0, 100);
    lay->addWidget(bar_);
    lay->addStretch(1);
}

void DownloadPage::initializePage() {
    complete_ = false;
    bar_->setValue(0);
    status_->setText("Connecting...");
    Q_EMIT completeChanged();

    auto* p = wizard_.pipeline();
    if (p == nullptr) {
        status_->setText("Internal error: pipeline not started.");
        return;
    }
    using Step = ::lsw::host::provision::Pipeline::Step;
    connect(p, &::lsw::host::provision::Pipeline::step_started, this,
            [this](Step s, const QString& desc) {
                if (s == Step::DownloadingIso) status_->setText(desc);
            });
    connect(p, &::lsw::host::provision::Pipeline::step_progress, this,
            [this](Step s, double frac, const QString& detail) {
                if (s != Step::DownloadingIso) return;
                if (frac >= 0.0 && frac <= 1.0) {
                    bar_->setValue(static_cast<int>(frac * 100));
                }
                status_->setText(detail);
            });
    connect(p, &::lsw::host::provision::Pipeline::step_finished, this, [this](Step s) {
        if (s != Step::DownloadingIso) return;
        bar_->setValue(100);
        status_->setText("Download complete.");
        complete_ = true;
        Q_EMIT completeChanged();
        QTimer::singleShot(0, this, [this] {
            if (auto* w = wizard()) w->next();
        });
    });
    connect(p, &::lsw::host::provision::Pipeline::failed, this,
            [this](Step s, const QString& err) {
                if (s != Step::DownloadingIso) return;
                status_->setText(QStringLiteral("Download failed: %1").arg(err));
            });
}

bool DownloadPage::isComplete() const {
    return complete_;
}

int DownloadPage::nextId() const {
    // Windows ISO download done → the Payload (build) page next.
    return Wizard::Payload;
}

}  // namespace lsw::host::wizard
