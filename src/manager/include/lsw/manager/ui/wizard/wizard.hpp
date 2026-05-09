#pragma once

#include "lsw/manager/config/vm_state.hpp"
#include "lsw/manager/wizard/pipeline.hpp"

#include <QWizard>

namespace lsw::manager {
class App;
}

namespace lsw::manager::ui::wizard {

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
// The wizard accumulates user choices into ``draft_`` (a ``VmRecord``)
// and a small ``WizardState`` shared with each page; on Next from the
// VmSize page, it constructs a ``manager::wizard::Pipeline`` and calls
// ``start()`` — every subsequent page is presentation only, subscribing
// to pipeline signals to render progress and auto-advance.
struct WizardState {
    config::VmRecord draft;           // accumulated by the pages
    QString chosen_iso_path;          // either downloaded or user-picked
    QString virtio_iso_path;          // downloaded virtio-win.iso
    QString payload_iso_path;         // built by the pipeline
    int host_cpu_threads = 4;         // detected at construction
    int host_ram_mib = 8192;          // detected at construction
    qint64 host_free_disk_bytes = 0;  // detected at construction
    // Local Windows account credentials baked into autounattend.xml.
    QString user_name = "lsw";
    QString user_password = "lsw";
    QString computer_name;  // Windows hostname; empty → VM name
    // 1-based WIM image index chosen on VmSizePage (which Windows
    // edition to install). 1 when unknown / Microsoft-download path.
    int image_index = 1;
    // Fido pipeline state. ``fido_edition_ids`` is the list of
    // ``productEditionId`` values returned by IsoSourceFetcher.
    QList<int> fido_edition_ids;
    QString fido_edition_label;
    QString fido_preferred_arch = "x64";
    QString fido_download_url;
    bool microsoft_banned = false;
};

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

    explicit Wizard(App& app, QWidget* parent = nullptr);

    [[nodiscard]] WizardState& state() noexcept { return state_; }

    [[nodiscard]] App& app() noexcept { return app_; }

    // The install pipeline. Returns nullptr until the user clicks Next
    // on VmSizePage (which is when the pipeline is constructed and
    // ``start()``-ed). After that, every subsequent page can subscribe
    // to its signals via ``connect(p, &Pipeline::step_started, ...)``.
    [[nodiscard]] ::lsw::manager::wizard::Pipeline* pipeline() noexcept { return pipeline_; }

    // Called by VmSizePage on Next — builds a ``Pipeline::Inputs`` from
    // the current ``state_``, constructs the pipeline, and starts it.
    // Idempotent.
    void start_pipeline();

Q_SIGNALS:
    void complete();

protected:
    void accept() override;

private:
    void detect_host_caps();

    App& app_;
    WizardState state_;
    ::lsw::manager::wizard::Pipeline* pipeline_ = nullptr;
};

}  // namespace lsw::manager::ui::wizard
