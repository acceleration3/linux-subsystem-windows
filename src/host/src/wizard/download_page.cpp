#include "lsw/host/wizard/download_page.hpp"

#include "lsw/host/wizard/wizard.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/services/host_services.hpp"

#include <QDir>

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

    auto& state = wizard_.state();
    if (state.fido_download_url.isEmpty()) {
        status_->setText("Internal error: no download URL was resolved.");
        return;
    }
    const QString dest =
        QDir(wizard_.services().settings().isos_dir()).filePath(QStringLiteral("windows.iso"));

    downloader_ = new provision::iso::Downloader(this);
    connect(downloader_, &provision::iso::Downloader::progress, this,
            [this](qint64 received, qint64 total) {
        if (total > 0) {
            bar_->setValue(static_cast<int>((received * 100) / total));
        }
        status_->setText(QStringLiteral("%1 MiB%2")
                             .arg(received / (1024 * 1024))
                             .arg(total > 0 ? QStringLiteral(" / %1 MiB").arg(total / (1024 * 1024))
                                            : QString{}));
    });
    connect(downloader_, &provision::iso::Downloader::failed, this,
            [this](const QString& message) {
        status_->setText(QStringLiteral("Download failed: %1").arg(message));
    });
    connect(downloader_, &provision::iso::Downloader::finished, this,
            [this](const QString& path) {
        // The trim page reads this, and so does the pipeline when it
        // starts: a local path here is what makes it skip its own
        // download step.
        wizard_.state().chosen_iso_path = path;
        bar_->setValue(100);
        status_->setText("Download complete.");
        complete_ = true;
        Q_EMIT completeChanged();
        QTimer::singleShot(0, this, [this] {
            if (auto* w = wizard()) {
                w->next();
            }
        });
    });
    downloader_->start(state.fido_download_url, dest);
}

bool DownloadPage::isComplete() const {
    return complete_;
}

int DownloadPage::nextId() const {
    // The image is local now, so it can be trimmed.
    return Wizard::Debloat;
}

}  // namespace lsw::host::wizard
