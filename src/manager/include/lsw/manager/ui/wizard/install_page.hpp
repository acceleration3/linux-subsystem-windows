#pragma once

#include "lsw/manager/ui/vnc_view.hpp"

#include <QLabel>
#include <QProgressBar>
#include <QWizardPage>

namespace lsw::manager::ui::wizard {

class Wizard;

// Shows VM-install progress + live VNC view. Pure presentation:
// subscribes to the wizard's Pipeline (BuildingPayloadIso..WaitingForAgent
// phases) for status updates; the pipeline emits vnc_port_available
// when libvirt has assigned a graphics port, and we connect the
// VncView to it then.
class InstallPage : public QWizardPage {
    Q_OBJECT
public:
    explicit InstallPage(Wizard& wizard, QWidget* parent = nullptr);

    void initializePage() override;
    [[nodiscard]] bool isComplete() const override;

    [[nodiscard]] int nextId() const override;

private:
    Wizard& wizard_;
    QProgressBar* bar_ = nullptr;
    QLabel* status_ = nullptr;
    ui::VncView* vnc_ = nullptr;
    bool complete_ = false;
};

}  // namespace lsw::manager::ui::wizard
