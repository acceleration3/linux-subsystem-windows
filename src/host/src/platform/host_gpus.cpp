#include "lsw/host/platform/host_gpus.hpp"

#include "lsw/base/log.hpp"

#include <QLatin1StringView>
#include <cstdint>
#include <cstring>
#include <vulkan/vulkan.h>

namespace lsw::host::platform {

namespace {

// 8-4-4-4-12, lowercase, the bytes in order: what vulkaninfo prints and
// accel-virt's list_gpus.py passes through untouched, so a UUID copied
// from either names the same GPU here.
QString format_uuid(const std::uint8_t (&bytes)[VK_UUID_SIZE]) {
    QString out;
    out.reserve(36);
    for (std::uint32_t i = 0; i < VK_UUID_SIZE; ++i) {
        if (i == 4U || i == 6U || i == 8U || i == 10U) {
            out += QLatin1Char('-');
        }
        out += QStringLiteral("%1").arg(bytes[i], 2, 16, QLatin1Char('0'));
    }
    return out;
}

bool has_extension(VkPhysicalDevice device, const char* name) {
    std::uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS) {
        return false;
    }
    QVector<VkExtensionProperties> extensions(static_cast<qsizetype>(count));
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()) < 0) {
        return false;
    }
    for (const auto& extension : extensions) {
        if (std::strcmp(extension.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

HostGpu describe(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties basic{};
    vkGetPhysicalDeviceProperties(device, &basic);

    // The driver name is a 1.2 struct (or VK_KHR_driver_properties), and
    // chaining a struct the device does not know is invalid usage rather
    // than something it ignores, so it is only asked for when known.
    const bool knows_driver = VK_API_VERSION_MINOR(basic.apiVersion) >= 2 ||
                              VK_API_VERSION_MAJOR(basic.apiVersion) > 1 ||
                              has_extension(device, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME);
    VkPhysicalDeviceDriverProperties driver{};
    driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    // Same rule for the bus address: only chained where the extension is.
    const bool knows_pci = has_extension(device, VK_EXT_PCI_BUS_INFO_EXTENSION_NAME);
    VkPhysicalDevicePCIBusInfoPropertiesEXT pci{};
    pci.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT;
    pci.pNext = knows_driver ? &driver : nullptr;
    VkPhysicalDeviceIDProperties id{};
    id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    if (knows_pci) {
        id.pNext = &pci;
    } else {
        id.pNext = knows_driver ? &driver : nullptr;
    }
    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &id;
    vkGetPhysicalDeviceProperties2(device, &properties);

    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(device, &memory);

    HostGpu gpu;
    gpu.name = QString::fromUtf8(properties.properties.deviceName);
    gpu.uuid = format_uuid(id.deviceUUID);
    gpu.vendor_id = properties.properties.vendorID;
    gpu.device_id = properties.properties.deviceID;
    gpu.cpu = properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
    if (knows_driver) {
        gpu.driver = QString::fromUtf8(driver.driverName);
        gpu.driver_info = QString::fromUtf8(driver.driverInfo);
    }
    if (knows_pci) {
        gpu.pci_address = QStringLiteral("%1:%2:%3.%4")
                              .arg(pci.pciDomain, 4, 16, QLatin1Char('0'))
                              .arg(pci.pciBus, 2, 16, QLatin1Char('0'))
                              .arg(pci.pciDevice, 2, 16, QLatin1Char('0'))
                              .arg(pci.pciFunction);
    }
    // The same sum the device reports to Windows as dedicated video
    // memory, so the number a settings page shows is the one dxdiag will.
    for (std::uint32_t heap = 0; heap < memory.memoryHeapCount; ++heap) {
        if ((memory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
            gpu.vram_bytes += memory.memoryHeaps[heap].size;
        }
    }
    return gpu;
}

}  // namespace

QVector<HostGpu> HostGpus::enumerate() {
    // The instance QEMU's device creates, near enough: API 1.1, no
    // layers asked for, no extensions. Asking for more would not change
    // which devices exist, but it could change whether the instance can
    // be made at all.
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "lsw-manager";
    application.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create.pApplicationInfo = &application;

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&create, nullptr, &instance) != VK_SUCCESS) {
        LSW_WARN("host GPUs: no Vulkan instance; cannot list host GPUs");
        return {};
    }
    QVector<HostGpu> out;
    std::uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(instance, &count, nullptr) == VK_SUCCESS && count > 0) {
        QVector<VkPhysicalDevice> devices(static_cast<qsizetype>(count));
        // VK_INCOMPLETE still fills what fits, so anything not an error
        // is a list worth reading.
        if (vkEnumeratePhysicalDevices(instance, &count, devices.data()) >= 0) {
            out.reserve(static_cast<qsizetype>(count));
            for (std::uint32_t i = 0; i < count; ++i) {
                out.push_back(describe(devices[static_cast<qsizetype>(i)]));
            }
        }
    }
    vkDestroyInstance(instance, nullptr);
    return out;
}

QString HostGpus::adapter_name(const HostGpu& gpu) {
    const QString name = gpu.name.trimmed();
    if (!name.endsWith(QLatin1Char(')'))) {
        return name;
    }
    const qsizetype open = name.lastIndexOf(QLatin1Char('('));
    const QString kept = open > 0 ? name.left(open).trimmed() : QString{};
    return kept.isEmpty() ? name : kept;
}

std::optional<HostGpu> HostGpus::resolve(const QVector<HostGpu>& gpus, const QString& setting) {
    const QString wanted = setting.trimmed();
    // The device's rule exactly: "auto" or nothing binds index 0,
    // whatever kind of device that is.
    if (wanted.isEmpty() || wanted == QLatin1StringView("auto")) {
        if (gpus.isEmpty()) {
            return std::nullopt;
        }
        return gpus.first();
    }
    // The device parses hex digits in either case, so a UUID typed in
    // upper case still names the GPU it means.
    for (const auto& gpu : gpus) {
        if (gpu.uuid.compare(wanted, Qt::CaseInsensitive) == 0) {
            return gpu;
        }
    }
    return std::nullopt;
}

}  // namespace lsw::host::platform
