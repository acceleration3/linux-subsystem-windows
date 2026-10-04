#include "lsw/host/provision/payload_iso_stager.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/platform/agent_exe_path.hpp"
#include "lsw/host/platform/dolphin_places.hpp"
#include "lsw/host/platform/guest_driver_paths.hpp"
#include "lsw/host/platform/mirror_exe_paths.hpp"
#include "lsw/host/provision/install/autounattend.hpp"
#include "lsw/host/provision/install/ivshmem_driver.hpp"
#include "lsw/host/provision/install/payload_iso.hpp"

#include <QtConcurrent>

namespace lsw::host::provision {

PayloadIsoStager::PayloadIsoStager(config::Settings& settings, QObject* parent)
    : QObject(parent), settings_(settings) {}

void PayloadIsoStager::build(const config::VmRecord& record, const PipelineInputs& inputs) {
    install::AutounattendInputs answers;
    answers.computer_name = inputs.computer_name.isEmpty() ? record.name : inputs.computer_name;
    answers.user_name = inputs.user_name;
    answers.user_password = inputs.user_password;
    answers.image_index = inputs.image_index;
    // install_from_path stays empty, and deliberately: the trimmed
    // image IS the install.wim on the media being booted, so Setup's
    // own default is already the right answer. A <Path> here would name
    // the same file by a route WinPE cannot resolve that early.

    // Register a Dolphin bookmark for the guest's C$ share via KIO's
    // userspace smb:// worker. No kernel cifs mount, so no junction
    // auto-submount pollution in Dolphin's Remote sidebar — one click
    // opens the guest's C:\ live.
    (void)platform::DolphinPlaces::register_windows_bookmark(inputs.user_name,
                                                             inputs.user_password);

    install::PayloadInputs payload;
    payload.output_iso_path = settings_.vm_payload_iso_path();
    payload.autounattend_xml = install::Autounattend::render(answers);

    // The shared-memory device has to exist on the first boot: it is the
    // transport the agent talks over, so a driver installed later — over
    // that same transport — never gets the chance.
    payload.ivshmem_driver_dir = install::IvshmemDriver::locate(settings_.data_dir());
    if (payload.ivshmem_driver_dir.isEmpty()) {
        // Last chance to notice: the ISO is being built now, and a
        // driver absent from it is absent from the first boot.
        LSW_WARN("provision: no ivshmem driver to stage — the guest will boot WITHOUT the "
                 "shared-memory device, so window frames and the shmem channel will be "
                 "unavailable. Put an unpacked driver in {}/ivshmem or set "
                 "LSW_IVSHMEM_DRIVER_DIR.",
                 settings_.data_dir().toStdString());
    }

    const QString agent_exe = platform::AgentExePath::locate();
    if (agent_exe.isEmpty()) {
        LSW_WARN("provision: no guest agent found; install will hang waiting for the "
                 "agent. Set LSW_AGENT_EXE or run scripts/build-agent.sh");
    } else {
        payload.agent_exe_path = agent_exe;
        LSW_INFO("provision: bundling guest agent from {}", agent_exe.toStdString());
    }

    const QString mirror_exe = platform::MirrorExePaths::guest();
    if (mirror_exe.isEmpty()) {
        LSW_WARN("provision: no guest window mirror found; applications will run in the "
                 "guest with no windows on this desktop. Set LSW_REMOTEWIN_GUEST or run "
                 "scripts/build-agent.sh");
    } else {
        payload.mirror_exe_path = mirror_exe;
        LSW_INFO("provision: bundling the window mirror's guest half from {}",
                 mirror_exe.toStdString());
    }

    // The guest drivers. Absent means the guest installs normally and
    // the agent runs; only the GPU path is missing, so this is a
    // warning rather than a failure.
    const auto x64 = platform::GuestDriverPaths::x86_64();
    payload.vulkan_icd_path = x64.vulkan_icd();
    payload.opengl_icd_path = x64.opengl_icd();
    payload.opencl_icd_path = x64.opencl_icd();
    payload.vulkan_loader_path = x64.vulkan_loader();
    if (payload.vulkan_icd_path.isEmpty() || payload.vulkan_loader_path.isEmpty()) {
        LSW_WARN("provision: no guest Vulkan driver / loader found; the guest will install "
                 "without the GPU path. Run scripts/build-agent.sh, or set "
                 "LSW_VULKAN_ICD_DLL and LSW_VULKAN_LOADER");
    } else {
        LSW_INFO("provision: bundling guest drivers vulkan={} opengl={} opencl={} loader={}",
                 payload.vulkan_icd_path.toStdString(), payload.opengl_icd_path.toStdString(),
                 payload.opencl_icd_path.toStdString(), payload.vulkan_loader_path.toStdString());
    }

    // The i686 set is what a 32-bit application loads, and every D3D8
    // and D3D9 title is one. Its absence costs only those, so it is a
    // note rather than a warning.
    const auto x86 = platform::GuestDriverPaths::i686();
    payload.vulkan_icd32_path = x86.vulkan_icd();
    payload.opengl_icd32_path = x86.opengl_icd();
    payload.opencl_icd32_path = x86.opencl_icd();
    payload.vulkan32_loader_path = x86.vulkan_loader();
    if (payload.vulkan_icd32_path.isEmpty() || payload.vulkan32_loader_path.isEmpty()) {
        LSW_INFO("provision: no 32-bit guest drivers; 32-bit applications will have no "
                 "driver. Run scripts/build-agent.sh (it builds both), or set "
                 "LSW_VULKAN_ICD_DLL_X86 and LSW_VULKAN_LOADER_X86");
    }

    // libisofs writes hundreds of megabytes; off the GUI thread. The
    // continuation is dropped if the stager is gone by the time it lands.
    struct Outcome {
        bool ok = false;
        QString error;
    };

    (void)QtConcurrent::run([payload] {
        Outcome outcome;
        outcome.ok = install::PayloadIsoBuilder::build(payload, &outcome.error);
        return outcome;
    }).then(this, [this, path = payload.output_iso_path](const Outcome& outcome) {
        if (!outcome.ok) {
            Q_EMIT failed(QStringLiteral("payload ISO build failed: %1").arg(outcome.error));
            return;
        }
        Q_EMIT finished(path);
    });
}

}  // namespace lsw::host::provision
