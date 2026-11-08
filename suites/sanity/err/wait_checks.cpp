#include "sanity/err/wait_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/vk/kit.hpp"
#include "support/api_policy.hpp"
#include "support/d3d12_rig.hpp"
#include "support/d3d12_tools.hpp"
#include "support/fresh_process.hpp"
#include "support/gate_window.hpp"
#include "support/gl_enums.hpp"
#include "support/gl_rig.hpp"
#include "support/gl_satellite.hpp"
#include "support/problems.hpp"
#include "support/robust_child.hpp"
#include "support/target_adapter.hpp"

#include <atomic>
#include <chrono>
#include <thread>

namespace lsw::suites::sanity::err {

namespace {

constexpr int kObserveMs = 3500;
constexpr VkDeviceSize kSize = 64 * 1024;

long long since(std::chrono::steady_clock::time_point from) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - from).count();
}

Outcome wait_vulkan(Context& context) {
    std::string error;
    const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(error);
    if (adapters.empty()) {
        return Outcome::fail("no target adapter: " + error);
    }
    VulkanRig* rig = new VulkanRig();
    if (!rig->open(adapters.front(), VulkanRigOptions{}, error)) {
        return Outcome::fail("opening the device failed: " + error);
    }
    VulkanBuffer* source = new VulkanBuffer();
    VulkanBuffer* sink = new VulkanBuffer();
    VulkanOwned<VkEvent>* gate = new VulkanOwned<VkEvent>();
    VulkanOwned<VkFence>* fence = new VulkanOwned<VkFence>();
    if (vk::Kit::host_buffer(*rig, *source, kSize) != VK_SUCCESS || vk::Kit::host_buffer(*rig, *sink, kSize) != VK_SUCCESS || rig->make_event(*gate) != VK_SUCCESS ||
        rig->make_fence(*fence) != VK_SUCCESS) {
        return Outcome::fail("creating the objects failed");
    }
    VulkanQueue& queue = rig->main();
    VkResult code = VK_SUCCESS;
    const VkCommandBuffer commands = vk::Kit::record(*rig, queue, [&](VkCommandBuffer cb) {
        VkEvent event = gate->get();
        rig->fn.vkCmdWaitEvents(cb, 1, &event, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0, nullptr, 0, nullptr);
        const VkBufferCopy region{0, 0, kSize};
        rig->fn.vkCmdCopyBuffer(cb, source->buffer, sink->buffer, 1, &region);
    }, 0, &code);
    if (commands == nullptr) {
        return Outcome::fail("recording failed: " + vk::Kit::name(code));
    }
    VkSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    info.commandBufferCount = 1;
    info.pCommandBuffers = &commands;
    const VkResult submitted = rig->submit(queue, 1, &info, fence->get());
    context.measure("submit", vk::Kit::name(submitted));
    if (submitted != VK_SUCCESS && submitted != VK_ERROR_DEVICE_LOST) {
        return Outcome::fail("vkQueueSubmit returned " + vk::Kit::name(submitted));
    }
    RobustChild::ready();
    const auto begin = std::chrono::steady_clock::now();
    VkResult status = VK_NOT_READY;
    long long lost_at = -1;
    while (since(begin) < kObserveMs) {
        status = rig->fn.vkGetFenceStatus(rig->device, fence->get());
        if (status != VK_NOT_READY && lost_at < 0) {
            lost_at = since(begin);
        }
        ::Sleep(50);
    }
    context.measure("fence_status", vk::Kit::name(status));
    context.measure("fence_changed_at_ms", lost_at);
    if (status != VK_NOT_READY && status != VK_ERROR_DEVICE_LOST) {
        return Outcome::fail("the fence of work waiting for an event nobody sets reported " + vk::Kit::name(status));
    }
    return Outcome::ok();
}

Outcome event_gate_vulkan(Context& context) {
    std::string error;
    const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(error);
    if (adapters.empty()) {
        return Outcome::fail("no target adapter: " + error);
    }
    VulkanRig* rig = new VulkanRig();
    if (!rig->open(adapters.front(), VulkanRigOptions{}, error)) {
        return Outcome::fail("opening the device failed: " + error);
    }
    VulkanBuffer* source = new VulkanBuffer();
    VulkanBuffer* sink = new VulkanBuffer();
    VulkanOwned<VkEvent>* gate = new VulkanOwned<VkEvent>();
    VulkanOwned<VkFence>* fence = new VulkanOwned<VkFence>();
    if (vk::Kit::host_buffer(*rig, *source, kSize) != VK_SUCCESS || vk::Kit::host_buffer(*rig, *sink, kSize) != VK_SUCCESS || rig->make_event(*gate) != VK_SUCCESS ||
        rig->make_fence(*fence) != VK_SUCCESS) {
        return Outcome::fail("creating the objects failed");
    }
    source->fill_pattern(0xE7);
    source->publish();
    sink->fill_value(0xCD);
    sink->publish();
    VulkanQueue& queue = rig->main();
    VkResult code = VK_SUCCESS;
    const VkCommandBuffer commands = vk::Kit::record(*rig, queue, [&](VkCommandBuffer cb) {
        VkEvent event = gate->get();
        rig->fn.vkCmdWaitEvents(cb, 1, &event, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0, nullptr, 0, nullptr);
        const VkBufferCopy region{0, 0, kSize};
        rig->fn.vkCmdCopyBuffer(cb, source->buffer, sink->buffer, 1, &region);
    }, 0, &code);
    if (commands == nullptr) {
        return Outcome::fail("recording failed: " + vk::Kit::name(code));
    }
    VkSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    info.commandBufferCount = 1;
    info.pCommandBuffers = &commands;
    const VkResult submitted = rig->submit(queue, 1, &info, fence->get());
    context.measure("submit", vk::Kit::name(submitted));
    if (submitted != VK_SUCCESS && submitted != VK_ERROR_DEVICE_LOST) {
        return Outcome::fail("vkQueueSubmit returned " + vk::Kit::name(submitted));
    }
    RobustChild::ready();
    const auto begin = std::chrono::steady_clock::now();
    VkResult status = VK_NOT_READY;
    while (since(begin) < 2600) {
        status = rig->fn.vkGetFenceStatus(rig->device, fence->get());
        ::Sleep(50);
    }
    context.measure("fence_status_before_set", vk::Kit::name(status));
    const VkResult set = rig->fn.vkSetEvent(rig->device, gate->get());
    context.measure("set_event", vk::Kit::name(set));
    const VkResult waited = rig->wait_fence(fence->get(), 20000000000ull);
    context.measure("fence_wait_after_set", vk::Kit::name(waited));
    if (status != VK_NOT_READY && status != VK_ERROR_DEVICE_LOST) {
        return Outcome::fail("the fence of work waiting for a host event reported " + vk::Kit::name(status) + " before the event was set");
    }
    if (set != VK_SUCCESS && set != VK_ERROR_DEVICE_LOST) {
        return Outcome::fail("vkSetEvent returned " + vk::Kit::name(set));
    }
    if (waited != VK_SUCCESS && waited != VK_ERROR_DEVICE_LOST) {
        return Outcome::fail("the fence wait after vkSetEvent returned " + vk::Kit::name(waited));
    }
    return Outcome::ok();
}

Outcome wait_d3d12(Context& context) {
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    D3d12Rig* rig = new D3d12Rig();
    if (!rig->open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail("opening the device failed: " + error);
    }
    ID3D12Fence* gate = nullptr;
    if (FAILED(rig->device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&gate)))) {
        return Outcome::fail("CreateFence failed");
    }
    D3d12Queue& queue = rig->direct();
    queue.gpu_wait_fence(gate, 1);
    const std::uint64_t behind = queue.signal();
    RobustChild::ready();
    const auto begin = std::chrono::steady_clock::now();
    long long changed_at = -1;
    HRESULT removed = S_OK;
    while (since(begin) < kObserveMs) {
        removed = rig->removed_reason();
        if ((removed != S_OK || queue.reached(behind)) && changed_at < 0) {
            changed_at = since(begin);
        }
        ::Sleep(50);
    }
    context.measure("queue_signal_reached", queue.reached(behind));
    context.measure("removed_reason", D3d12Tools::hr(removed));
    context.measure("changed_at_ms", changed_at);
    if (removed != S_OK && removed != DXGI_ERROR_DEVICE_REMOVED && removed != DXGI_ERROR_DEVICE_HUNG && removed != DXGI_ERROR_DEVICE_RESET &&
        removed != DXGI_ERROR_DRIVER_INTERNAL_ERROR) {
        return Outcome::fail("the device reported " + D3d12Tools::hr(removed) + " for a queue waiting on a fence nobody signals");
    }
    return Outcome::ok();
}

Outcome wait_gl(Context& context) {
    GlRig* rig = new GlRig();
    GlRigOptions options;
    options.width = 128;
    options.height = 128;
    options.visible = false;
    std::string error;
    if (!rig->open(options, error)) {
        return Outcome::fail(error);
    }
    GlApi& api = rig->gl;
    if (!api.supports(3, 2, "GL_ARB_sync") || api.glFenceSync == nullptr || api.glWaitSync == nullptr) {
        return Context::skip("OpenGL 3.2 or GL_ARB_sync is not advertised");
    }
    GlSatellite* other = new GlSatellite();
    if (!other->open(*rig, true, 64, 64, error)) {
        return Context::skip("a shared context cannot be created: " + error);
    }
    std::atomic<void*>* sync = new std::atomic<void*>(nullptr);
    std::atomic<bool>* failed = new std::atomic<bool>(false);
    std::thread([other, sync, failed] {
        std::string thread_error;
        if (!other->bind(thread_error)) {
            failed->store(true);
            sync->store(reinterpret_cast<void*>(1));
            return;
        }
        other->gl.glClearColor(0.0f, 0.5f, 0.0f, 1.0f);
        other->gl.glClear(glc::COLOR_BUFFER_BIT);
        void* created = other->gl.glFenceSync(glc::SYNC_GPU_COMMANDS_COMPLETE, 0);
        failed->store(created == nullptr);
        sync->store(created != nullptr ? created : reinterpret_cast<void*>(1));
        ::Sleep(60000);
    }).detach();
    while (sync->load() == nullptr) {
        ::Sleep(5);
    }
    if (failed->load()) {
        return Context::skip("the shared context cannot be made current on a second thread");
    }
    api.glWaitSync(sync->load(), 0, glc::TIMEOUT_IGNORED);
    api.glClearColor(0.5f, 0.0f, 0.0f, 1.0f);
    api.glClear(glc::COLOR_BUFFER_BIT);
    void* mine = api.glFenceSync(glc::SYNC_GPU_COMMANDS_COMPLETE, 0);
    api.glFlush();
    RobustChild::ready();
    const auto begin = std::chrono::steady_clock::now();
    unsigned seen = glc::TIMEOUT_EXPIRED;
    long long changed_at = -1;
    while (since(begin) < kObserveMs) {
        seen = api.glClientWaitSync(mine, 0, 0);
        if (seen != glc::TIMEOUT_EXPIRED && changed_at < 0) {
            changed_at = since(begin);
        }
        ::Sleep(50);
    }
    context.measure("fence_wait", seen == glc::TIMEOUT_EXPIRED ? std::string("timeout expired") : seen == glc::SYNC_WAIT_FAILED ? std::string("wait failed") : std::string("signalled"));
    context.measure("changed_at_ms", changed_at);
    return Outcome::ok();
}

Outcome role(Context& context, const std::string& name) {
    if (name == "wait-vulkan") {
        return wait_vulkan(context);
    }
    if (name == "wait-d3d12") {
        return wait_d3d12(context);
    }
    if (name == "wait-gl") {
        return wait_gl(context);
    }
    if (name == "event-gate-vulkan") {
        return event_gate_vulkan(context);
    }
    return Outcome::fail("unknown role " + name);
}

}  // namespace

Outcome WaitChecks::never_signalled(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, role);
    }
    const std::vector<std::string> apis = ApiPolicy::filter({"vulkan", "d3d12", "gl"});
    if (apis.empty()) {
        return Context::skip("all APIs of the row are excluded by LSW_SUITES_SKIP_APIS");
    }
    Problems problems;
    std::vector<std::string> skipped;
    GateWindow window(context.id());
    window.open(problems);
    for (const std::string& api : apis) {
        const CheckResult result = RobustChild::run(context.id(), "wait-" + api, {}, 90, -1);
        context.measure(api + ".child_status", Outcome::name(result.status));
        RobustChild::forward(context, api + ".", result);
        if (result.status == Status::Skip) {
            skipped.push_back(api + ": " + result.reason);
        } else if (result.status != Status::Ok) {
            problems.add(api + ": " + Outcome::name(result.status) + " " + result.reason);
        }
    }
    window.close(context, problems, "never_signalled", 5000000);
    FreshProcess::check(context, problems, context.id(), "never_signalled");
    if (!problems.empty()) {
        return problems.outcome();
    }
    if (skipped.size() == apis.size()) {
        return Context::skip(skipped.front());
    }
    for (const std::string& note : skipped) {
        context.note(note);
    }
    return Outcome::ok();
}

Outcome WaitChecks::vk_host_event_gate(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, role);
    }
    if (ApiPolicy::is_skipped("vulkan")) {
        return Context::skip("api excluded by LSW_SUITES_SKIP_APIS");
    }
    Problems problems;
    GateWindow window(context.id());
    window.open(problems);
    const CheckResult result = RobustChild::run(context.id(), "event-gate-vulkan", {}, 90, -1);
    context.measure("child_status", Outcome::name(result.status));
    RobustChild::forward(context, "", result);
    window.close(context, problems, "vk_host_event_gate", 5000000);
    FreshProcess::check(context, problems, context.id(), "vk_host_event_gate");
    if (result.status == Status::Skip && problems.empty()) {
        return Context::skip(result.reason);
    }
    if (result.status == Status::Fail || result.status == Status::Timeout) {
        problems.add("child: " + std::string(Outcome::name(result.status)) + " " + result.reason);
    } else if (result.status == Status::Crash) {
        context.note("the child process crashed on the host-set event gate: " + result.reason);
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::err
