#pragma once

#include "lsw/host/ui/vnc_view.hpp"
#include "lsw/host/wizard/pipeline_page.hpp"

#include <QLabel>
#include <QProgressBar>

namespace lsw::host::wizard {

class Wizard;

// Shows VM-install progress + live VNC view. Pure presentation:
// subscribes to the wizard's Pipeline (BuildingPayloadIso..WaitingForAgent
// phases) for status updates; the pipeline emits vnc_port_available
// when libvirt has assigned a graphics port, and we connect the
// ui::VncView to it then.
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

}  // namespace lsw::host::wizard
