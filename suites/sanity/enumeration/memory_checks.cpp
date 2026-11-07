#include "sanity/enumeration/memory_checks.hpp"

#include "support/api_policy.hpp"
#include "support/av_adapters.hpp"
#include "support/kmt_api.hpp"
#include "support/opencl_api.hpp"
#include "support/problems.hpp"
#include "support/vulkan_api.hpp"

#include <algorithm>

namespace lsw::suites::sanity::enumeration {

namespace {

constexpr std::uint64_t kDedicatedTolerancePercent = 25;
constexpr std::uint64_t kSharedTolerancePercent = 50;
constexpr std::uint64_t kKmtTolerancePercent = 10;

bool within(std::uint64_t a, std::uint64_t b, std::uint64_t percent) {
    const std::uint64_t high = std::max(a, b);
    const std::uint64_t low = std::min(a, b);
    return (high - low) * 100 <= high * percent;
}

const VulkanDevice* vulkan_for(const std::vector<VulkanDevice>& devices, const AvAdapter& adapter) {
    for (const VulkanDevice& device : devices) {
        if (device.luid_valid && AvAdapters::key(device.luid) == adapter.luid_key()) {
            return &device;
        }
    }
    return nullptr;
}

}  // namespace

Outcome MemoryChecks::dedicated_vs_shared_consistent(Context& context) {
    std::vector<AvAdapter> adapters;
    std::string error;
    KmtApi kmt;
    const bool use_vulkan = !ApiPolicy::is_skipped("vulkan");
    VulkanApi vulkan;
    std::vector<VulkanDevice> devices;
    if (!AvAdapters::all(adapters, error) || !kmt.load(error) ||
        (use_vulkan && (!vulkan.load(error) || !vulkan.create_instance(error) || !vulkan.devices(devices, error)))) {
        return Outcome::fail(error);
    }
    Problems problems;
    std::size_t checked = 0;
    const bool clamped_desc = sizeof(void*) == 4;
    for (const AvAdapter& adapter : adapters) {
        if (!adapter.accel()) {
            continue;
        }
        ++checked;
        const std::string label = adapter.name + " " + adapter.luid_text();
        context.measure(adapter.luid_text() + ".dxgi_desc.dedicated_video", adapter.dedicated_video);
        context.measure(adapter.luid_text() + ".dxgi_desc.shared_system", adapter.shared_system);
        KmtSegmentSizes segments;
        if (!kmt.segment_sizes(adapter.luid, segments)) {
            problems.add(label + ": D3DKMT segment sizes unavailable");
            continue;
        }
        context.measure(adapter.luid_text() + ".kmt.dedicated_video", segments.dedicated_video);
        context.measure(adapter.luid_text() + ".kmt.shared_system", segments.shared_system);
        if (segments.dedicated_video == 0 || segments.shared_system == 0) {
            problems.add(label + ": D3DKMT reports zero dedicated video or shared system memory");
        }
        if (!clamped_desc) {
            if (adapter.dedicated_video == 0 || adapter.shared_system == 0) {
                problems.add(label + ": DXGI reports zero dedicated video or shared system memory");
            }
            if (!within(segments.dedicated_video, adapter.dedicated_video, kKmtTolerancePercent) ||
                !within(segments.shared_system, adapter.shared_system, kKmtTolerancePercent)) {
                problems.add(label + ": D3DKMT and DXGI sizes differ by more than " +
                             std::to_string(kKmtTolerancePercent) + "%");
            }
        }
        if (!use_vulkan) {
            continue;
        }
        const VulkanDevice* device = vulkan_for(devices, adapter);
        if (device == nullptr) {
            problems.add(label + ": no Vulkan device carries this LUID");
            continue;
        }
        const std::uint64_t local = device->largest_device_local();
        const std::uint64_t host = device->total_host_heaps();
        context.measure(adapter.luid_text() + ".vk.device_local", local);
        context.measure(adapter.luid_text() + ".vk.host", host);
        if (local == 0 || host == 0) {
            problems.add(label + ": Vulkan reports a zero device-local or host heap");
        }
        if (!within(local, segments.dedicated_video, kDedicatedTolerancePercent)) {
            problems.add(label + ": Vulkan device-local heap " + std::to_string(local) + " vs D3DKMT dedicated " +
                         std::to_string(segments.dedicated_video) + " differ by more than " +
                         std::to_string(kDedicatedTolerancePercent) + "%");
        }
        if (!within(host, segments.shared_system, kSharedTolerancePercent)) {
            problems.add(label + ": Vulkan host heaps " + std::to_string(host) + " vs D3DKMT shared " +
                         std::to_string(segments.shared_system) + " differ by more than " +
                         std::to_string(kSharedTolerancePercent) + "%");
        }
    }
    if (checked == 0) {
        problems.add("DXGI lists no accel-virt adapter");
    }
    return problems.outcome();
}

Outcome MemoryChecks::opencl_global_size(Context& context) {
    std::vector<AvAdapter> adapters;
    std::string error;
    if (ApiPolicy::is_skipped("opencl")) {
        return Outcome::skip("opencl is excluded by LSW_SUITES_SKIP_APIS");
    }
    const bool use_vulkan = !ApiPolicy::is_skipped("vulkan");
    KmtApi kmt;
    VulkanApi vulkan;
    std::vector<VulkanDevice> vk_devices;
    OpenClApi opencl;
    std::vector<OpenClPlatform> platforms;
    if (!AvAdapters::accel(adapters, error) || !kmt.load(error) ||
        (use_vulkan && (!vulkan.load(error) || !vulkan.create_instance(error) || !vulkan.devices(vk_devices, error))) ||
        !opencl.load(error) || !opencl.platforms(platforms, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    std::size_t checked = 0;
    for (const OpenClPlatform& platform : platforms) {
        for (const OpenClDevice& device : platform.devices) {
            const AvAdapter* adapter = nullptr;
            if (device.luid_valid) {
                adapter = AvAdapters::find(adapters, AvAdapters::key(device.luid));
            } else if (adapters.size() == 1) {
                adapter = &adapters.front();
            }
            if (adapter == nullptr) {
                continue;
            }
            ++checked;
            const VulkanDevice* vk = vulkan_for(vk_devices, *adapter);
            const std::uint64_t vulkan_total = vk != nullptr ? vk->total_heaps() : 0;
            KmtSegmentSizes segments;
            if (!kmt.segment_sizes(adapter->luid, segments)) {
                problems.add(device.name + ": D3DKMT segment sizes unavailable");
                continue;
            }
            const std::uint64_t dxgi_total = segments.dedicated_video + segments.dedicated_system + segments.shared_system;
            const std::uint64_t limit = std::max(vulkan_total, dxgi_total);
            context.measure(device.name + ".global", device.global_memory);
            context.measure(device.name + ".max_alloc", device.max_allocation);
            context.measure(device.name + ".limit", limit);
            if (device.global_memory == 0 || device.max_allocation == 0) {
                problems.add(device.name + ": zero global size or maximum allocation" + (device.query_failures.empty() ? std::string() : " (" + device.query_failures + ")"));
            }
            if (device.global_memory > limit) {
                problems.add(device.name + ": CL_DEVICE_GLOBAL_MEM_SIZE " + std::to_string(device.global_memory) +
                             " exceeds the Vulkan/D3DKMT heap total " + std::to_string(limit));
            }
            if (device.max_allocation > device.global_memory) {
                problems.add(device.name + ": CL_DEVICE_MAX_MEM_ALLOC_SIZE exceeds the global size");
            }
        }
    }
    if (checked == 0) {
        problems.add("no OpenCL device could be attributed to an accel-virt adapter");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::enumeration
