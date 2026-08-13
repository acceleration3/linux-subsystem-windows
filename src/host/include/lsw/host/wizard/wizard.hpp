#pragma once

#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/wizard/wizard_state.hpp"
#include "lsw/host/provision/pipeline.hpp"

#include <QWizard>

namespace lsw::host::services { class HostServices; }

namespace lsw::host::wizard {

class WelcomePage;
class LicensePage;
class IsoChoicePage;
class DownloadPage;
class VmSizePage;
class PayloadPage;
class InstallPage;
class SetupPage;
class DonePage;

// First-run wizard: walks the user through ISO acquisition, VM sizing,
// VM creation, unattended install, payload install, and confirmation.
//
class Wizard : public QWizard {
    Q_OBJECT
public:
    enum PageId {
        Welcome = 0,
        License,
        IsoChoice,
        Download,
        VmSize,
        Payload,
        Install,
        Setup,
        Done,
    };

    explicit Wizard(services::HostServices& services, QWidget* parent = nullptr);

    [[nodiscard]] WizardState& state() noexcept { return state_; }

    [[nodiscard]] services::HostServices& services() noexcept { return services_; }

    // The install pipeline. Returns nullptr until the user clicks Next
    // on VmSizePage (which is when the pipeline is constructed and
    // ``start()``-ed). After that, every subsequent page can subscribe
    // to its signals via ``connect(p, &Pipeline::step_started, ...)``.
    [[nodiscard]] ::lsw::host::provision::Pipeline* pipeline() noexcept { return pipeline_; }

    // Called by VmSizePage on Next — builds a ``PipelineInputs`` from
    // the current ``state_``, constructs the pipeline, and starts it.
    // Idempotent.
    void start_pipeline();

Q_SIGNALS:
    void complete();

protected:
    void accept() override;

private:
    void detect_host_caps();

    services::HostServices& services_;
    WizardState state_;
    ::lsw::host::provision::Pipeline* pipeline_ = nullptr;
};

}  // namespace lsw::host::wizard
