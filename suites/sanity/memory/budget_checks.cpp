#include "sanity/memory/budget_checks.hpp"

#include "core/text.hpp"
#include "support/api_loop.hpp"
#include "support/dxgi_budget.hpp"
#include "support/memory_tools.hpp"
#include "support/opencl_backend.hpp"
#include "support/target_adapter.hpp"
#include "support/vulkan_backend.hpp"

#include <algorithm>
#include <windows.h>

namespace lsw::suites::sanity::memory {

namespace {

constexpr std::uint64_t kSize = 256ull << 20;

}  // namespace

Outcome BudgetChecks::dxgi_moves_with_alloc(Context& context) {
    Problems problems;
    ApiLoop::run(problems, {"d3d11", "d3d12"}, [&](GpuBackend& backend, const std::string& api, const AvAdapter& adapter) {
        VideoMemory before;
        VideoMemory during;
        VideoMemory after;
        std::string error;
        const std::uint64_t resource_limit = std::max<std::uint64_t>(128ull << 20, std::min<std::uint64_t>(adapter.dedicated_video / 4, 2048ull << 20));
        const std::uint64_t size = std::min(kSize, resource_limit);
        if (!DxgiBudget::query(adapter.luid, true, before, error)) {
            problems.add(api + ": " + error);
            return;
        }
        {
            AllocStatus status = AllocStatus::Ok;
            std::string detail;
            std::unique_ptr<GpuBuffer> buffer = backend.create(size, BufferKind::Device, status, detail);
            if (buffer == nullptr) {
                problems.add(api + ": " + detail);
                return;
            }
            if (!BackendHarness::fill_and_verify(backend, *buffer, 120, size, context, error) ||
                !DxgiBudget::query(adapter.luid, true, during, error)) {
                problems.add(api + ": " + error);
                return;
            }
        }
        static_cast<void>(backend.finish(error));
        context.measure(api + ".usage_before", before.usage);
        context.measure(api + ".usage_during", during.usage);
        const std::uint64_t slack = 8ull << 20;
        if (during.usage + slack < before.usage + size) {
            problems.add(api + ": CurrentUsage rose by " + std::to_string((during.usage > before.usage ? during.usage - before.usage : 0) >> 20) +
                         " MiB (" + std::to_string(before.usage) + " to " + std::to_string(during.usage) + " bytes) for a touched " +
                         std::to_string(size >> 20) + " MiB allocation");
        }
        bool fell = false;
        for (int poll = 0; poll < 30 && !fell; ++poll) {
            if (DxgiBudget::query(adapter.luid, true, after, error)) {
                fell = after.usage <= before.usage + size / 4;
            }
            if (!fell) {
                ::Sleep(100);
            }
        }
        context.measure(api + ".usage_after", after.usage);
        if (!fell) {
            problems.add(api + ": CurrentUsage did not fall after the release (" + std::to_string(after.usage >> 20) + " MiB, was " +
                         std::to_string(before.usage >> 20) + " MiB before)");
        }
    });
    return problems.outcome();
}

Outcome BudgetChecks::vulkan_budget_moves(Context& context) {
    AvAdapter adapter;
    std::string error;
    VulkanBackend backend;
    if (!TargetAdapter::pick(adapter, error) || !backend.init(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    if (!backend.shared().budget_extension) {
        std::vector<VkDeviceSize> usage;
        std::vector<VkDeviceSize> limit;
        if (backend.shared().budget(usage, limit)) {
            return Outcome::fail("VK_EXT_memory_budget is not listed but budget data is returned");
        }
        context.note("VK_EXT_memory_budget is not exposed and no budget data is reported");
        return Outcome::ok();
    }
    static_cast<void>(backend.release_staging(error));
    const std::uint64_t before = backend.heap_usage(true);
    std::uint64_t during = 0;
    Problems problems;
    {
        AllocStatus status = AllocStatus::Ok;
        std::string detail;
        std::unique_ptr<GpuBuffer> buffer = backend.create(kSize, BufferKind::Device, status, detail);
        if (buffer == nullptr) {
            return Outcome::fail(detail);
        }
        if (!BackendHarness::fill_and_verify(backend, *buffer, 121, kSize, context, error)) {
            return Outcome::fail(error);
        }
        during = backend.heap_usage(true);
        std::vector<VkDeviceSize> usage;
        std::vector<VkDeviceSize> limit;
        static_cast<void>(backend.shared().budget(usage, limit));
        for (std::size_t h = 0; h < usage.size(); ++h) {
            if (usage[h] > limit[h]) {
                problems.add("heap " + std::to_string(h) + " usage " + std::to_string(usage[h]) + " exceeds its budget " + std::to_string(limit[h]));
            }
        }
    }
    static_cast<void>(backend.release_staging(error));
    context.measure("usage.before", before);
    context.measure("usage.during", during);
    if (during + (8ull << 20) < before + kSize) {
        problems.add("device-local heap usage went from " + std::to_string(before) + " to " + std::to_string(during) +
                     " bytes (rose " + std::to_string(during > before ? (during - before) >> 20 : 0) + " MiB) for a touched " +
                     std::to_string(kSize >> 20) + " MiB allocation");
    }
    std::uint64_t after = 0;
    bool fell = false;
    for (int poll = 0; poll < 30 && !fell; ++poll) {
        after = backend.heap_usage(true);
        fell = after <= before + kSize / 4;
        if (!fell) {
            ::Sleep(100);
        }
    }
    context.measure("usage.after", after);
    if (!fell) {
        problems.add("device-local heap usage was " + std::to_string(after) + " bytes after the free, " + std::to_string(before) +
                     " before the allocation");
    }
    return problems.outcome();
}

Outcome BudgetChecks::reservation_honoured(Context& context) {
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    VideoMemory memory;
    if (!DxgiBudget::query(adapter.luid, true, memory, error)) {
        return Outcome::fail(error);
    }
    context.measure("available_for_reservation", memory.available_for_reservation);
    if (memory.available_for_reservation == 0) {
        return Outcome::fail("AvailableForReservation is zero");
    }
    Problems problems;
    const std::uint64_t amount = memory.available_for_reservation;
    const long accepted = DxgiBudget::set_reservation(adapter.luid, true, amount);
    context.measure("reservation.requested", amount);
    context.measure("reservation.hresult", Text::hex(static_cast<std::uint32_t>(accepted), 8));
    context.measure("reservation.segment_group", "local");
    VideoMemory reserved;
    if (accepted < 0) {
        problems.add("SetVideoMemoryReservation rejected the available amount, HRESULT 0x" + Text::hex(static_cast<std::uint32_t>(accepted), 8));
    } else if (!DxgiBudget::query(adapter.luid, true, reserved, error) || reserved.reservation != amount) {
        problems.add("SetVideoMemoryReservation(local, " + std::to_string(amount) + " = AvailableForReservation) returned 0x" +
                     Text::hex(static_cast<std::uint32_t>(accepted), 8) + " but CurrentReservation reads " + std::to_string(reserved.reservation));
    }
    static_cast<void>(DxgiBudget::set_reservation(adapter.luid, true, 0));
    const long rejected = DxgiBudget::set_reservation(adapter.luid, true, memory.available_for_reservation * 2 + (1ull << 30));
    context.measure("above_available_hresult", Text::hex(static_cast<std::uint32_t>(rejected), 8));
    if (rejected >= 0) {
        problems.add("a reservation above AvailableForReservation was accepted");
    }
    static_cast<void>(DxgiBudget::set_reservation(adapter.luid, true, 0));
    return problems.outcome();
}

Outcome BudgetChecks::opencl_free_memory(Context& context) {
    AvAdapter adapter;
    std::string error;
    OpenClBackend backend;
    if (!TargetAdapter::pick(adapter, error) || !backend.init(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    bool exposed = false;
    const std::uint64_t before_kb = backend.free_memory_kb(exposed);
    if (!exposed) {
        return Context::skip("cl_amd_device_attribute_query (free memory) is not advertised by the device");
    }
    std::uint64_t during_kb = 0;
    {
        AllocStatus status = AllocStatus::Ok;
        std::string detail;
        std::unique_ptr<GpuBuffer> buffer = backend.create(kSize, BufferKind::Device, status, detail);
        if (buffer == nullptr || !BackendHarness::fill_and_verify(backend, *buffer, 122, kSize, context, error)) {
            return Outcome::fail(buffer == nullptr ? detail : error);
        }
        during_kb = backend.free_memory_kb(exposed);
    }
    context.measure("free_kb.before", before_kb);
    context.measure("free_kb.during", during_kb);
    if (before_kb < during_kb + (kSize >> 10) - (8u << 10)) {
        return Outcome::fail("free memory fell by " + std::to_string((before_kb - std::min(before_kb, during_kb)) >> 10) + " MiB for a " +
                             std::to_string(kSize >> 20) + " MiB allocation");
    }
    return Outcome::ok();
}

}  // namespace lsw::suites::sanity::memory
