#include "support/api_snapshot.hpp"

#include "core/text.hpp"
#include "support/kmt_api.hpp"

namespace lsw::suites {

std::string ApiSnapshot::describe_dxgi(const std::vector<AvAdapter>& adapters, bool identity_only) {
    std::string out;
    for (std::size_t i = 0; i < adapters.size(); ++i) {
        const AvAdapter& a = adapters[i];
        out += "dxgi." + std::to_string(i) + " luid=" + a.luid_text() + " vendor=" + Text::hex(a.vendor, 4) +
               " device=" + Text::hex(a.device, 4) + " subsys=" + Text::hex(a.subsystem, 8);
        if (!identity_only) {
            out += " name=" + a.name + " dedicated_video=" + std::to_string(a.dedicated_video) +
                   " dedicated_system=" + std::to_string(a.dedicated_system) +
                   " shared=" + std::to_string(a.shared_system);
        }
        out += " outputs=";
        for (const AvOutput& output : a.outputs) {
            out += output.device_name + ",";
        }
        out += "\n";
    }
    return out;
}

std::string ApiSnapshot::describe_vulkan(const std::vector<VulkanDevice>& devices) {
    std::string out;
    for (std::size_t i = 0; i < devices.size(); ++i) {
        const VulkanDevice& d = devices[i];
        out += "vk." + std::to_string(i) + " name=" + d.name + " vendor=" + Text::hex(d.vendor, 4) +
               " device=" + Text::hex(d.device, 4) + " api=" + std::to_string(d.api_version) +
               " luid_valid=" + std::to_string(d.luid_valid ? 1 : 0) + " luid=" +
               Text::hex(static_cast<std::uint32_t>(d.luid.HighPart), 8) + ":" + Text::hex(d.luid.LowPart, 8) +
               " heaps=";
        for (const VulkanHeap& heap : d.heaps) {
            out += std::to_string(heap.size) + (heap.device_local ? "L," : "H,");
        }
        out += "\n";
    }
    return out;
}

std::string ApiSnapshot::describe_opencl(const std::vector<OpenClPlatform>& platforms) {
    std::string out;
    for (std::size_t p = 0; p < platforms.size(); ++p) {
        out += "cl.platform." + std::to_string(p) + " name=" + platforms[p].name +
               " vendor=" + platforms[p].vendor + " version=" + platforms[p].version + "\n";
        for (std::size_t d = 0; d < platforms[p].devices.size(); ++d) {
            const OpenClDevice& device = platforms[p].devices[d];
            out += "cl.device." + std::to_string(p) + "." + std::to_string(d) + " name=" + device.name +
                   " vendor_id=" + Text::hex(device.vendor_id, 4) + " global=" +
                   std::to_string(device.global_memory) + " max_alloc=" +
                   std::to_string(device.max_allocation) + " luid_valid=" +
                   std::to_string(device.luid_valid ? 1 : 0) + " luid=" +
                   Text::hex(static_cast<std::uint32_t>(device.luid.HighPart), 8) + ":" +
                   Text::hex(device.luid.LowPart, 8) + "\n";
        }
    }
    return out;
}

bool ApiSnapshot::take(ApiSnapshot& out, std::string& error) {
    std::vector<AvAdapter> adapters;
    if (!AvAdapters::all(adapters, error)) {
        return false;
    }
    out.dxgi = describe_dxgi(adapters, false);

    KmtApi kmt;
    std::vector<KmtAdapter> kmt_adapters;
    if (!kmt.load(error) || !kmt.enum_adapters(kmt_adapters, error)) {
        return false;
    }
    out.kmt.clear();
    for (std::size_t i = 0; i < kmt_adapters.size(); ++i) {
        out.kmt += "kmt." + std::to_string(i) + " luid=" +
                   Text::hex(static_cast<std::uint32_t>(kmt_adapters[i].luid.HighPart), 8) + ":" +
                   Text::hex(kmt_adapters[i].luid.LowPart, 8) +
                   " sources=" + std::to_string(kmt_adapters[i].sources) + "\n";
    }

    VulkanApi vulkan;
    std::vector<VulkanDevice> devices;
    if (!vulkan.load(error) || !vulkan.enumerate_fresh(devices, error)) {
        return false;
    }
    out.vulkan = describe_vulkan(devices);

    OpenClApi opencl;
    std::vector<OpenClPlatform> platforms;
    if (!opencl.load(error) || !opencl.platforms(platforms, error)) {
        return false;
    }
    out.opencl = describe_opencl(platforms);
    return true;
}

}  // namespace lsw::suites
