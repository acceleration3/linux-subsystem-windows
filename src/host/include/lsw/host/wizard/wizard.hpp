#pragma once

#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/wizard/wizard_state.hpp"
#include "lsw/host/provision/pipeline.hpp"

#include <QWizard>

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

// First-run wizard: an image, a machine, an unattended install, and the
// guest payload.
//
// Eight steps, and a rail down the left showing all of them, so the
// length of the process is visible from the first screen rather than
// discovered one Next at a time.
//
// Two things that used to be steps are not any more. Downloading an
// image and trimming one are tools: they produce an ISO, they are
// optional, and a wizard page could only run them once, in one place,
// with no way back once it had committed.
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
    // The step rail down the left, and the slot that keeps it in step
    // with the page being shown.
    void build_side_rail();
    void highlight_step(int page_id);

    services::HostServices& services_;
    QListWidget* steps_ = nullptr;
    WizardState state_;
    ::lsw::host::provision::Pipeline* pipeline_ = nullptr;
};

}  // namespace lsw::host::wizard
