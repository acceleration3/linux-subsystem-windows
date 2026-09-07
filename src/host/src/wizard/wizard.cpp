#include "lsw/host/wizard/wizard.hpp"

#include "lsw/host/services/host_services.hpp"
#include "lsw/host/wizard/done_page.hpp"
#include "lsw/host/wizard/download_page.hpp"
#include "lsw/host/wizard/install_page.hpp"
#include "lsw/host/wizard/debloat_page.hpp"
#include "lsw/host/wizard/iso_choice_page.hpp"
#include "lsw/host/wizard/license_page.hpp"
#include "lsw/host/wizard/payload_page.hpp"
#include "lsw/host/wizard/setup_page.hpp"
#include "lsw/host/wizard/vm_size_page.hpp"
#include "lsw/host/wizard/welcome_page.hpp"

#include <QDir>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>

namespace lsw::host::wizard {

Wizard::Wizard(services::HostServices& services, QWidget* parent) : QWizard(parent), services_(services) {
    setWindowTitle("LSW first-run wizard");
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::IndependentPages, false);
    setOption(QWizard::NoCancelButtonOnLastPage);
    resize(720, 480);

    detect_host_caps();

    setPage(Welcome, new WelcomePage);
    setPage(License, new LicensePage);
    setPage(IsoChoice, new IsoChoicePage(*this));
    setPage(Debloat, new DebloatPage(*this));
    setPage(Download, new DownloadPage(*this));
    setPage(VmSize, new VmSizePage(*this));
    setPage(Payload, new PayloadPage(*this));
    setPage(Install, new InstallPage(*this));
    setPage(Setup, new SetupPage(*this));
    setPage(Done, new DonePage);
    setStartId(Welcome);

    // Hide Back+Next on the long-running pages (Download/Install/Setup).
    // They auto-advance via ``next()`` once the pipeline finishes their
    // phase, so the user has nothing useful to click; a stray Back
    // during Install would tear the VM down mid-Setup.
    connect(this, &QWizard::currentIdChanged, this, [this](int id) {
        const bool hide_nav =
            (id == Download || id == Payload || id == Install || id == Setup);
        if (auto* b = button(BackButton)) b->setVisible(!hide_nav);
        if (auto* b = button(NextButton)) b->setVisible(!hide_nav);
    });
}

void Wizard::detect_host_caps() {
    state_.host_cpu_threads = std::max(1, QThread::idealThreadCount());

    QFile mi{"/proc/meminfo"};
    if (mi.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const auto text = QString::fromLatin1(mi.readAll());
        const auto idx = text.indexOf("MemTotal:");
        if (idx >= 0) {
            const auto rest = text.mid(idx);
            const auto kb_str = rest.split(' ', Qt::SkipEmptyParts).value(1);
            const auto kb = kb_str.toLongLong();
            if (kb > 0) {
                state_.host_ram_mib = static_cast<int>(kb / 1024);
            }
        }
    }

    QStorageInfo info{QDir(services_.settings().disks_dir())};
    state_.host_free_disk_bytes = info.bytesAvailable();
}

void Wizard::start_pipeline() {
    if (pipeline_ != nullptr) {
        return;
    }
    pipeline_ = new ::lsw::host::provision::Pipeline(services_, this);
    ::lsw::host::provision::PipelineInputs in;
    in.iso_local_path = state_.chosen_iso_path;
    in.iso_download_url = state_.fido_download_url;
    in.iso_display_label = state_.fido_edition_label;
    in.virtio_iso_path = state_.virtio_iso_path;
    in.user_name = state_.user_name;
    in.user_password = state_.user_password;
    in.computer_name = state_.computer_name;
    in.sizing = state_.draft.sizing;
    in.image_index = state_.image_index;
    in.debloat = state_.debloat;
    pipeline_->set_inputs(in);
    pipeline_->start();
}

void Wizard::accept() {
    QWizard::accept();
    Q_EMIT complete();
}

}  // namespace lsw::host::wizard
