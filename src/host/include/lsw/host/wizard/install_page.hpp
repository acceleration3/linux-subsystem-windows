#pragma once

#include "lsw/host/ui/vnc_view.hpp"
#include "lsw/host/wizard/pipeline_page.hpp"

#include <QLabel>
#include <QProgressBar>

namespace lsw::host::wizard {

class Wizard;

class InstallPage : public PipelinePage {
    Q_OBJECT
public:
    explicit InstallPage(Wizard& wizard, QWidget* parent = nullptr);

    void initializePage() override;

    [[nodiscard]] int nextId() const override;

private:
    void set_phase(const QString& text);
    Wizard& wizard_;
    QProgressBar* bar_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* elapsed_ = nullptr;
    QString phase_;
    ui::VncView* vnc_ = nullptr;
};

}
