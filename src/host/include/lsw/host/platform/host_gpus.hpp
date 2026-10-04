#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>
#include <optional>

namespace lsw::host::platform {

// One Vulkan physical device on this host, as the accel-virt GPU device
// sees it: it binds a host GPU by ``uuid``, and the guest's adapter
// takes its name, PCI IDs and dedicated video memory from it.
struct HostGpu {
    QString name;  // VkPhysicalDeviceProperties::deviceName
    // deviceUUID as 8-4-4-4-12 lowercase hex, the form vulkaninfo prints
    // and the device's host-gpu= property takes.
    QString uuid;
    quint32 vendor_id = 0;
    quint32 device_id = 0;
    quint64 vram_bytes = 0;  // every DEVICE_LOCAL heap, summed
    QString driver;          // VkPhysicalDeviceDriverProperties::driverName; empty before 1.2
    QString driver_info;     // ...::driverInfo, e.g. "Mesa 25.2.3"; empty before 1.2
    // The PCI function, "0000:03:00.0", from VK_EXT_pci_bus_info: what
    // ties this GPU to its /sys/class/drm card. Empty when the driver
    // does not offer the extension (or the device is not on PCI).
    QString pci_address;
    bool cpu = false;        // a software rasterizer such as llvmpipe
    // A GPU sharing the processor's package. RADV names one after its
    // processor, so the guest's adapter takes its name from elsewhere
    // (provision::install::AdapterNames).
    bool integrated = false;
};

// The host GPUs a guest adapter can run on.
class HostGpus {
public:
    HostGpus() = delete;

    // Every Vulkan physical device, in the loader's order. That order is
    // the one QEMU's device sees too, which is what makes the first
    // entry the answer to "auto". Empty when there is no Vulkan loader
    // or no device, which callers treat as "cannot tell" rather than as
    // "there is no GPU": the device in QEMU does its own enumeration.
    [[nodiscard]] static QVector<HostGpu> enumerate();

    // The GPU ``setting`` names: "auto" (or empty) is the first one, as
    // the device's host-gpu=auto picks (src/device/gpu/gpu_binding.cpp:
    // index 0, whatever its type); a UUID is the GPU with that UUID, in
    // either case. nullopt when nothing matches.
    [[nodiscard]] static std::optional<HostGpu> resolve(const QVector<HostGpu>& gpus,
                                                        const QString& setting);

    // The name the guest's adapter takes from ``gpu``, as accel-virt's
    // device gives it (src/device/gpu/adapter_name.hpp): the Vulkan
    // deviceName without the trailing parenthesised group Mesa's drivers
    // append, so "AMD Radeon RX 9070 XT (RADV GFX1201)" is "AMD Radeon RX
    // 9070 XT". A name that is only such a group is kept whole.
    [[nodiscard]] static QString adapter_name(const HostGpu& gpu);
};

}  // namespace lsw::host::platform
