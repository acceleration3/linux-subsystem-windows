#include "lsw/host/provision/payload_iso_stager.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/platform/accel_virt_paths.hpp"
#include "lsw/host/platform/agent_exe_path.hpp"
#include "lsw/host/platform/dolphin_places.hpp"
#include "lsw/host/platform/mirror_exe_paths.hpp"
#include "lsw/host/provision/install/autounattend.hpp"
#include "lsw/host/provision/install/ivshmem_driver.hpp"
#include "lsw/host/provision/install/payload_iso.hpp"

#include <QtConcurrent>

#include <utility>

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

    // The display driver. Not optional, unlike everything above: the
    // steady-state machine has the accel-virt adapter as its only
    // display, so a guest installed without the package boots to a
    // device with no driver and no desktop. Refusing here, before the
    // disk exists, is the cheapest place to find out.
    payload.av_driver_tree = platform::AccelVirtPaths::driver_tree();
    if (payload.av_driver_tree.isEmpty()) {
        Q_EMIT failed(QStringLiteral(
            "the accel-virt driver package was not found, and the guest's only display "
            "adapter needs it. Build it (`cmake --build build --target drivers && cmake "
            "--install build` in external/accel-virt), or set $LSW_AV_DRIVERS to an "
            "installed package: the directory holding provision.ps1 and package/"));
        return;
    }
    LSW_INFO("provision: bundling the accel-virt driver package from {}",
             payload.av_driver_tree.toStdString());

    // The Vulkan loaders. Windows 11 may ship one (26200 carries
    // 1.3.300.0 in both views) and the guest keeps it when it does, so
    // a missing build of ours costs only a guest whose Windows has none.
    payload.vulkan_loader_x64_path = platform::AccelVirtPaths::vulkan_loader(false);
    payload.vulkan_loader_x86_path = platform::AccelVirtPaths::vulkan_loader(true);
    for (const auto& [path, bits] : {std::pair{payload.vulkan_loader_x64_path, "64"},
                                     std::pair{payload.vulkan_loader_x86_path, "32"}}) {
        if (path.isEmpty()) {
            LSW_WARN("provision: no {}-bit Vulkan loader built; if this Windows ships none, "
                     "{}-bit applications will find no Vulkan. Run scripts/build-agent.sh",
                     bits, bits);
        }
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
