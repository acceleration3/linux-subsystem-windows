#pragma once

#include "lsw/host/provision/iso/downloader.hpp"

#include <QLabel>
#include <QProgressBar>
#include <QWizardPage>

namespace lsw::host::wizard {

class Wizard;

// Streams the Windows ISO.
//
// Drives its own Downloader rather than watching the pipeline's. The
// page after this one trims the image, and it cannot do that to a file
// that does not exist yet — so the download has to finish while the
// wizard is still in charge, before the pipeline is started at all.
// The pipeline still knows how to download, and still does for the
// unattended CLI path; it simply finds the work already done here.
class DownloadPage : public QWizardPage {
    Q_OBJECT
public:
    explicit DownloadPage(Wizard& wizard, QWidget* parent = nullptr);

    void initializePage() override;
    [[nodiscard]] bool isComplete() const override;
    [[nodiscard]] int nextId() const override;

private:
    Wizard& wizard_;
    provision::iso::Downloader* downloader_ = nullptr;
    QLabel* status_ = nullptr;
    QProgressBar* bar_ = nullptr;
    bool complete_ = false;
};

}  // namespace lsw::host::wizard
