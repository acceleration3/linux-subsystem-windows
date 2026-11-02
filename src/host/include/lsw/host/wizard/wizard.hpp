#pragma once

#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/wizard/wizard_state.hpp"
#include "lsw/host/provision/pipeline.hpp"

#include <QStringList>
#include <QWizard>

class QLabel;
class QListWidget;

namespace lsw::host::services { class HostServices; }

namespace lsw::host::wizard {

class WelcomePage;
class LicensePage;
class IsoChoicePage;
class VmSizePage;
class PayloadPage;
class InstallPage;
class SetupPage;
class DonePage;

class Wizard : public QWizard {
    Q_OBJECT
public:
    enum PageId {
        Welcome = 0,
        License,
        IsoChoice,
        VmSize,
        Payload,
        Install,
        Setup,
        Done,
    };

    explicit Wizard(services::HostServices& services, QWidget* parent = nullptr);

    [[nodiscard]] WizardState& state() noexcept { return state_; }

    [[nodiscard]] services::HostServices& services() noexcept { return services_; }

    [[nodiscard]] ::lsw::host::provision::Pipeline* pipeline() noexcept { return pipeline_; }

    void start_pipeline();

Q_SIGNALS:
    void complete();

protected:
    void accept() override;

private:
    void detect_host_caps();
    [[nodiscard]] static const QStringList& step_names();
    void highlight_step(int page_id);

    services::HostServices& services_;
    WizardState state_;
    ::lsw::host::provision::Pipeline* pipeline_ = nullptr;
};

}
