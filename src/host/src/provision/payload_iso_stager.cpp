#include "lsw/host/provision/payload_iso_stager.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/config/vm_name.hpp"
#include "lsw/host/platform/agent_exe_path.hpp"
#include "lsw/host/platform/guest_vulkan_paths.hpp"
#include "lsw/host/platform/dolphin_places.hpp"
#include "lsw/host/provision/install/autounattend.hpp"
#include "lsw/host/provision/install/ivshmem_driver.hpp"
#include "lsw/host/provision/install/payload_iso.hpp"

#include <QDir>
#include <QMetaObject>
#include <QPointer>
#include <QtConcurrent>

namespace lsw::host::provision {

PayloadIsoStager::PayloadIsoStager(config::Settings& settings, QObject* parent)
    : QObject(parent), settings_(settings) {}

void PayloadIsoStager::build(const config::VmRecord& record, const PipelineInputs& inputs) {
    install::AutounattendInputs answers;
    answers.computer_name =
        inputs.computer_name.isEmpty() ? record.name : inputs.computer_name;
    answers.user_name = inputs.user_name;
    answers.user_password = inputs.user_password;
    answers.image_index = inputs.image_index;

    // Register a Dolphin bookmark for the guest's C$ share via KIO's
    // userspace smb:// worker. No kernel cifs mount, so no junction
    // auto-submount pollution in Dolphin's Remote sidebar — one click
    // opens the guest's C:\ live.
    (void)platform::DolphinPlaces::register_windows_bookmark(inputs.user_name,
                                                             inputs.user_password);

    install::PayloadInputs payload;
    payload.output_iso_path = QDir(settings_.isos_dir())
        .filePath(QStringLiteral("%1-payload.iso").arg(QLatin1String(config::kVmName)));
    payload.autounattend_xml = install::Autounattend::render(answers);

    // The shared-memory device has to exist on the first boot: it is the
    // transport the agent talks over, so a driver installed later — over
    // that same transport — never gets the chance.
    payload.ivshmem_driver_dir = install::IvshmemDriver::locate(settings_.data_dir());

    const QString agent_exe = platform::AgentExePath::locate();
    if (agent_exe.isEmpty()) {
        LSW_WARN("provision: no guest agent found; install will hang waiting for the "
                 "agent. Set LSW_AGENT_EXE or run scripts/build-agent.sh");
    } else {
        payload.agent_exe_path = agent_exe;
        LSW_INFO("provision: bundling guest agent from {}", agent_exe.toStdString());
    }

    // The guest Vulkan pair. Absent means the guest installs normally
    // and the agent runs; only the GPU path is missing, so this is a
    // warning rather than a failure.
    const QString icd_dll = platform::GuestVulkanPaths::icd_dll();
    const QString loader = platform::GuestVulkanPaths::vulkan_loader();
    if (icd_dll.isEmpty() || loader.isEmpty()) {
        LSW_WARN("provision: no guest Vulkan ICD / loader found; the guest will install "
                 "without the GPU path. Run scripts/build-agent.sh, or set LSW_ICD_DLL "
                 "and LSW_VULKAN_LOADER");
    } else {
        payload.icd_dll_path = icd_dll;
        payload.vulkan_loader_path = loader;
        payload.icd_manifest_json = platform::GuestVulkanPaths::icd_manifest_json();
        LSW_INFO("provision: bundling guest Vulkan ICD from {} and loader from {}",
                 icd_dll.toStdString(), loader.toStdString());
    }

    // libisofs writes hundreds of megabytes; off the GUI thread.
    const QPointer<PayloadIsoStager> self{this};
    (void)QtConcurrent::run([self, payload]() {
        const int rc = install::PayloadIsoBuilder::build(payload);
        QMetaObject::invokeMethod(self.data(), [self, rc, payload]() {
            if (!self) {
                return;
            }
            if (rc != 0) {
                Q_EMIT self->failed(QStringLiteral("payload ISO build failed"));
                return;
            }
            Q_EMIT self->finished(payload.output_iso_path);
        }, Qt::QueuedConnection);
    });
}

}  // namespace lsw::host::provision
