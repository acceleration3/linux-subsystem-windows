#pragma once

#include <QLabel>
#include <QProgressBar>
#include <QWizardPage>

namespace lsw::host::wizard {

class Wizard;

// Streams the Windows ISO. Pure presentation: subscribes to the
// wizard's Pipeline (DownloadingIso phase) for progress and completion;
// auto-advances when the pipeline finishes the phase.
class DownloadPage : public QWizardPage {
    Q_OBJECT
public:
    explicit DownloadPage(Wizard& wizard, QWidget* parent = nullptr);

    void initializePage() override;
    [[nodiscard]] bool isComplete() const override;
    [[nodiscard]] int nextId() const override;

private:
    Wizard& wizard_;
    QLabel* status_ = nullptr;
    QProgressBar* bar_ = nullptr;
    bool complete_ = false;
};

}  // namespace lsw::host::wizard
