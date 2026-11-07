#include "sanity/err/recovery_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/d3d11/err_checks.hpp"
#include "sanity/err/delegate.hpp"
#include "sanity/err/oom_checks.hpp"
#include "sanity/err/spin_device.hpp"
#include "sanity/iso/loads.hpp"
#include "sanity/sync/sync_device.hpp"
#include "support/api_policy.hpp"
#include "support/backend_harness.hpp"
#include "support/child_runs.hpp"
#include "support/d3d12_tools.hpp"
#include "support/problems.hpp"
#include "support/robust_child.hpp"
#include "support/target_adapter.hpp"
#include "support/vulkan_buffer.hpp"
#include "support/vulkan_compute.hpp"
#include "support/vulkan_shaders.hpp"

#include <algorithm>
#include <chrono>
#include <random>

namespace lsw::suites::sanity::err {

namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

Outcome removed_d3d12(Context& context) {
    if (ApiPolicy::is_skipped("d3d12")) {
        return Context::skip("d3d12 excluded by LSW_SUITES_SKIP_APIS");
    }
    AvAdapter adapter;
    std::string error;
    Problems problems;
    {
        D3d12Rig rig;
        if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
            return Outcome::fail(error);
        }
        Com<ID3D12Device5> device5 = rig.as<ID3D12Device5>();
        if (!device5) {
            return Context::skip("ID3D12Device5::RemoveDevice is not exposed");
        }
        Com<ID3D12Fence> fence;
        rig.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), fence.put_void());
        device5->RemoveDevice();
        const HRESULT reason = rig.removed_reason();
        context.measure("removed_reason", D3d12Tools::hr(reason));
        if (!D3d12Tools::in(reason, {D3d12Tools::kDeviceRemoved, D3d12Tools::kDeviceHung, D3d12Tools::kDeviceReset, D3d12Tools::kDriverInternalError, D3d12Tools::kInvalidCall})) {
            problems.add("GetDeviceRemovedReason returned " + D3d12Tools::hr(reason) + " after RemoveDevice");
        }
        Com<ID3D12Resource> buffer;
        D3D12_RESOURCE_DESC desc = D3d12Tools::buffer_desc(4096);
        const D3D12_HEAP_PROPERTIES heap = D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT);
        const HRESULT created = rig.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), buffer.put_void());
        if (SUCCEEDED(created) || !D3d12Tools::in(created, {D3d12Tools::kDeviceRemoved, D3d12Tools::kDeviceHung, D3d12Tools::kDeviceReset, D3d12Tools::kDriverInternalError, E_FAIL, E_INVALIDARG})) {
            problems.add("CreateCommittedResource on a removed device returned " + D3d12Tools::hr(created));
        }
        if (fence && fence->GetCompletedValue() != ~0ull) {
            problems.add("GetCompletedValue of a fence on a removed device is not UINT64_MAX");
        }
    }
    std::unique_ptr<sync::SyncDevice> fresh = sync::SyncDevice::make("d3d12");
    std::string detail;
    std::uint32_t value = 0;
    if (!fresh->open(error) || !fresh->set(0, 41, error) || !fresh->add(0, 1, error) || !fresh->signal(error) || fresh->wait(20000, detail) != sync::SyncState::Done ||
        !fresh->read(0, value, error) || value != 42) {
        problems.add("a new D3D12 device after the removal does not compute correctly: " + error + detail);
    }
    return problems.outcome();
}

Outcome vklost_role(Context& context) {
    std::string error;
    const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(error);
    if (adapters.empty()) {
        return Context::skip("Vulkan is unavailable: " + error);
    }
    VulkanRig* rig = new VulkanRig();
    VulkanRigOptions options;
    options.queues_per_family = 1;
    if (!rig->open(adapters.front(), options, error)) {
        return Outcome::fail(error);
    }
    VulkanBuffer* buffer = new VulkanBuffer();
    VulkanCompute* compute = new VulkanCompute();
    VulkanOwned<VkFence>* fence = new VulkanOwned<VkFence>();
    VkResult result = buffer->create(*rig, 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (result == VK_SUCCESS) {
        buffer->fill_value(0);
        buffer->publish();
        result = compute->create(*rig, VulkanShaders::words(VulkanShader::SpinForever), {VulkanBinding{}}, 0);
    }
    if (result == VK_SUCCESS) {
        compute->write_buffer(0, buffer->buffer);
        result = rig->make_fence(*fence);
    }
    if (result != VK_SUCCESS) {
        return Outcome::fail("creating the objects failed: " + VulkanTools::result_name(result));
    }
    VulkanQueue& queue = rig->main();
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = queue.pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    VkCommandBuffer commands = nullptr;
    result = rig->fn.vkAllocateCommandBuffers(rig->device, &allocate, &commands);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (result == VK_SUCCESS) {
        result = rig->fn.vkBeginCommandBuffer(commands, &begin);
    }
    if (result == VK_SUCCESS) {
        compute->bind(commands);
        compute->dispatch(commands, 1);
        result = rig->fn.vkEndCommandBuffer(commands);
    }
    VkSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    info.commandBufferCount = 1;
    info.pCommandBuffers = &commands;
    if (result == VK_SUCCESS) {
        result = rig->submit(queue, 1, &info, fence->get());
    }
    if (result != VK_SUCCESS) {
        return Outcome::fail("recording or submitting the never-ending shader failed: " + VulkanTools::result_name(result));
    }
    const auto begin_time = Clock::now();
    VkResult waited = VK_TIMEOUT;
    while (waited == VK_TIMEOUT && elapsed_ms(begin_time) < 25000) {
        waited = rig->wait_fence(fence->get(), 100000000ull);
    }
    context.measure("lost_after_ms", elapsed_ms(begin_time));
    if (waited == VK_TIMEOUT) {
        return Context::skip("the never-ending shader was not terminated within 25 s, so no loss occurred to observe");
    }
    if (waited != VK_ERROR_DEVICE_LOST) {
        return Outcome::fail("waiting for the never-ending shader returned " + VulkanTools::result_name(waited) + " instead of VK_ERROR_DEVICE_LOST");
    }
    Problems problems;
    const VkResult submit = rig->submit(queue, 0, nullptr, VK_NULL_HANDLE);
    if (submit != VK_ERROR_DEVICE_LOST) {
        problems.add("vkQueueSubmit on the lost device returned " + VulkanTools::result_name(submit));
    }
    const VkResult idle = rig->fn.vkDeviceWaitIdle(rig->device);
    if (idle != VK_ERROR_DEVICE_LOST) {
        problems.add("vkDeviceWaitIdle on the lost device returned " + VulkanTools::result_name(idle));
    }
    std::unique_ptr<sync::SyncDevice> fresh = sync::SyncDevice::make("vulkan");
    std::string detail;
    std::uint32_t value = 0;
    for (int attempt = 0; attempt < 20; ++attempt) {
        error.clear();
        detail.clear();
        fresh = sync::SyncDevice::make("vulkan");
        if (fresh->open(error) && fresh->set(0, 9, error) && fresh->add(0, 1, error) && fresh->signal(error) && fresh->wait(20000, detail) == sync::SyncState::Done &&
            fresh->read(0, value, error) && value == 10) {
            error.clear();
            break;
        }
        ::Sleep(500);
    }
    if (value != 10) {
        problems.add("a new Vulkan device after the loss does not compute correctly: " + error + detail);
    }
    return problems.outcome();
}

Outcome tdr_role(Context& context, const std::string& api) {
    std::unique_ptr<SpinDevice> spin = SpinDevice::make(api);
    std::string error;
    if (spin == nullptr || !spin->open(true, error)) {
        return Context::skip(api + " is unavailable: " + error);
    }
    SpinDevice* leaked = spin.release();
    if (!leaked->submit(1, error)) {
        return Outcome::fail("submit: " + error);
    }
    const auto begin = Clock::now();
    std::string detail;
    const SpinState state = leaked->poll(25000, detail);
    const double lost_ms = elapsed_ms(begin);
    context.measure("loss_ms", lost_ms);
    if (state == SpinState::Error) {
        return Outcome::fail("the guest saw an undocumented failure: " + detail);
    }
    if (state != SpinState::Lost) {
        return Outcome::fail("a shader running past the timeout was not aborted with a documented loss within 25 s (state " + std::to_string(static_cast<int>(state)) + ")");
    }
    AvAdapter adapter;
    for (int attempt = 0; attempt < 20; ++attempt) {
        std::unique_ptr<GpuBackend> backend = BackendHarness::open(api, adapter, error);
        AllocStatus status = AllocStatus::Ok;
        if (backend != nullptr && BackendHarness::roundtrip(*backend, 1u << 20, 5150 + static_cast<std::uint64_t>(attempt), status, context, error) && status == AllocStatus::Ok) {
            context.measure("recovered_ms", elapsed_ms(begin));
            return elapsed_ms(begin) < 30000 ? Outcome::ok() : Outcome::fail("the loss and the recovery took " + std::to_string(elapsed_ms(begin)) + " ms, over 30 s");
        }
        ::Sleep(500);
    }
    return Outcome::fail("a new device after the loss never computed correctly: " + error);
}

Outcome free_role(Context& context, const std::string& api) {
    AvAdapter adapter;
    std::string error;
    std::unique_ptr<GpuBackend> backend = BackendHarness::open(api, adapter, error);
    if (backend == nullptr) {
        return Context::skip(error);
    }
    for (BufferKind kind : {BufferKind::Device, BufferKind::Upload}) {
        if (kind == BufferKind::Upload && !backend->supports_map()) {
            continue;
        }
        std::vector<std::unique_ptr<GpuBuffer>> held;
        std::uint64_t chunk = 128ull << 20;
        std::uint64_t total = 0;
        const std::uint64_t cap = kind == BufferKind::Device ? (6ull << 30) : (1ull << 30);
        while (total < cap) {
            AllocStatus status = AllocStatus::Ok;
            std::string detail;
            std::unique_ptr<GpuBuffer> next = backend->create(chunk, kind, status, detail);
            if (next == nullptr) {
                if (chunk <= (4ull << 20)) {
                    break;
                }
                chunk /= 2;
                continue;
            }
            total += chunk;
            held.push_back(std::move(next));
        }
        context.measure(kind == BufferKind::Device ? "device_bytes" : "host_bytes", static_cast<long long>(total));
    }
    return Outcome::ok();
}

Outcome mapper_role(Context& context, const std::string& api) {
    AvAdapter adapter;
    std::string error;
    std::unique_ptr<GpuBackend> backend = BackendHarness::open(api, adapter, error);
    if (backend == nullptr) {
        return Context::skip(error);
    }
    if (!backend->supports_map()) {
        return Context::skip(api + " backend does not expose mapped host-visible buffers");
    }
    std::vector<std::unique_ptr<GpuBuffer>> held;
    std::uint64_t total = 0;
    while (total < (1ull << 30)) {
        AllocStatus status = AllocStatus::Ok;
        std::string detail;
        std::unique_ptr<GpuBuffer> next = backend->create(128ull << 20, BufferKind::Upload, status, detail);
        void* pointer = nullptr;
        if (next == nullptr || !backend->map(*next, &pointer, detail)) {
            break;
        }
        for (std::uint64_t at = 0; at < (128ull << 20); at += 4096) {
            static_cast<volatile std::uint8_t*>(pointer)[at] = 0x5a;
        }
        total += 128ull << 20;
        held.push_back(std::move(next));
    }
    context.measure("mapped_bytes", static_cast<long long>(total));
    if (total == 0) {
        return Context::skip("no large mapping could be created");
    }
    RobustChild::ready();
    ::Sleep(60000);
    return Outcome::ok();
}

Outcome role(Context& context, const std::string& name) {
    const std::size_t dash = name.find('-');
    const std::string kind = dash == std::string::npos ? name : name.substr(0, dash);
    const std::string api = dash == std::string::npos ? std::string() : name.substr(dash + 1);
    if (kind == "tdr") {
        return tdr_role(context, api);
    }
    if (kind == "free") {
        return free_role(context, api);
    }
    if (kind == "mapper") {
        return mapper_role(context, api);
    }
    if (kind == "vklost") {
        return vklost_role(context);
    }
    return iso::Loads::role(context, name);
}

Outcome kill_then_measure(Context& context, const std::string& hold_role, const std::string& label) {
    if (ChildMode::active()) {
        return RobustChild::child(context, role);
    }
    const std::vector<std::string> apis = ApiPolicy::filter({"d3d11", "d3d12", "vulkan", "opencl"});
    if (apis.empty()) {
        return Context::skip("all APIs of the row are excluded by LSW_SUITES_SKIP_APIS");
    }
    Problems problems;
    std::mt19937 random(static_cast<unsigned>(::GetTickCount64()));
    int measured = 0;
    for (const std::string& api : apis) {
        const CheckResult before = RobustChild::run(context.id(), "free-" + api, {}, 300, -1);
        if (before.status != Status::Ok) {
            continue;
        }
        int killed = 0;
        for (int i = 0; i < 3; ++i) {
            const CheckResult result = RobustChild::run(context.id(), hold_role + "-" + api, {}, 120, 200 + static_cast<int>(random() % 500));
            if (result.status == Status::Timeout && result.reason.rfind("terminated", 0) == 0) {
                ++killed;
            }
        }
        const CheckResult after = RobustChild::run(context.id(), "free-" + api, {}, 300, -1);
        if (after.status != Status::Ok) {
            problems.add(api + ": the process started after the kills failed: " + Outcome::name(after.status) + " " + after.reason);
            continue;
        }
        ++measured;
        context.measure(api + "." + label + ".killed", killed);
        for (const char* key : {"device_bytes", "host_bytes"}) {
            const long long was = ChildRuns::number(before, key);
            const long long now = ChildRuns::number(after, key);
            if (was <= 0) {
                continue;
            }
            context.measure(api + "." + label + "." + key + "_before", was);
            context.measure(api + "." + label + "." + key + "_after", now);
            if (now * 100 < was * 90) {
                problems.add(api + ": a new process could allocate " + std::to_string(now >> 20) + " MiB of " + key + " after the kills, against " + std::to_string(was >> 20) + " MiB before");
            }
        }
    }
    if (measured == 0 && problems.empty()) {
        return Context::skip("no API could run the allocation probe");
    }
    return problems.outcome();
}

}  // namespace

Outcome RecoveryChecks::device_removed_reason(Context& context) {
    return Delegate::all(context, {d3d11::ErrChecks::device_removed, removed_d3d12});
}

Outcome RecoveryChecks::device_lost_vulkan(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, role);
    }
    if (ApiPolicy::is_skipped("vulkan")) {
        return Context::skip("vulkan excluded by LSW_SUITES_SKIP_APIS");
    }
    const CheckResult result = RobustChild::run(context.id(), "vklost-vulkan", {}, 180, -1);
    RobustChild::forward(context, "child.", result);
    if (result.status == Status::Skip) {
        return Context::skip(result.reason);
    }
    return result.status == Status::Ok ? Outcome::ok() : Outcome::fail(std::string(Outcome::name(result.status)) + " " + result.reason);
}

Outcome RecoveryChecks::tdr_recovers(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, role);
    }
    const std::vector<std::string> apis = ApiPolicy::filter({"d3d11", "d3d12", "vulkan"});
    if (apis.empty()) {
        return Context::skip("all APIs of the row are excluded by LSW_SUITES_SKIP_APIS");
    }
    Problems problems;
    int ran = 0;
    for (const std::string& api : apis) {
        const CheckResult result = RobustChild::run(context.id(), "tdr-" + api, {}, 120, -1);
        RobustChild::forward(context, api + ".", result);
        if (result.status == Status::Ok) {
            ++ran;
        } else if (result.status != Status::Skip) {
            problems.add(api + ": " + Outcome::name(result.status) + " " + result.reason);
        }
    }
    if (problems.empty() && ran == 0) {
        return Context::skip("no API could run the timeout shader");
    }
    return problems.outcome();
}

Outcome RecoveryChecks::process_crash_cleanup(Context& context) {
    return kill_then_measure(context, "hammer", "crash");
}

Outcome RecoveryChecks::oom_clean(Context& context) {
    return OomChecks::every_api(context);
}

Outcome RecoveryChecks::kill_during_map(Context& context) {
    return kill_then_measure(context, "mapper", "map");
}

}  // namespace lsw::suites::sanity::err
