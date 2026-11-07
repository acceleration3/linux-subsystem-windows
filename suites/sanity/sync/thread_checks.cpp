#include "sanity/sync/thread_checks.hpp"

#include "support/api_loop.hpp"
#include "support/api_policy.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_root.hpp"
#include "support/d3d12_tools.hpp"
#include "support/pattern.hpp"
#include "support/problems.hpp"
#include "support/target_adapter.hpp"
#include "support/vulkan_buffer.hpp"
#include "support/vulkan_compute.hpp"
#include "support/vulkan_rig.hpp"
#include "support/vulkan_shaders.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <numeric>
#include <random>
#include <thread>

namespace lsw::suites::sanity::sync {

namespace {

constexpr const char* kStepHlsl =
    "cbuffer C : register(b0) { uint op; uint value; uint slot; uint pad; };\n"
    "RWStructuredBuffer<uint> o : register(u0);\n"
    "[numthreads(1, 1, 1)]\n"
    "void main() { if (op == 0) { o[slot] = value; } else { o[slot] += value; } }\n";

}  // namespace

Outcome ThreadChecks::many_threads_one_device(Context& context) {
    constexpr int kThreads = 32;
    constexpr int kRounds = 12;
    Problems problems;
    std::mutex problems_mutex;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        std::vector<std::unique_ptr<GpuBackend>> contexts;
        for (int i = 0; i < kThreads; ++i) {
            std::unique_ptr<GpuBackend> next = backend.new_context();
            if (next == nullptr) {
                problems.add(api + ": creating context " + std::to_string(i) + " failed");
                return;
            }
            contexts.push_back(std::move(next));
        }
        std::vector<std::thread> threads;
        for (int i = 0; i < kThreads; ++i) {
            threads.emplace_back([&, i] {
                GpuBackend& ctx = *contexts[static_cast<std::size_t>(i)];
                std::string error;
                bool ok = true;
                for (int round = 0; ok && round < kRounds; ++round) {
                    const std::uint64_t size = 4096ull << (static_cast<unsigned>(i + round) % 7);
                    AllocStatus status = AllocStatus::Ok;
                    std::string detail;
                    std::unique_ptr<GpuBuffer> first = ctx.create(size, BufferKind::Device, status, detail);
                    std::unique_ptr<GpuBuffer> second = first != nullptr ? ctx.create(size, BufferKind::Device, status, detail) : nullptr;
                    if (second == nullptr) {
                        error = "allocation: " + detail;
                        ok = false;
                        break;
                    }
                    std::vector<std::uint8_t> expected(static_cast<std::size_t>(size));
                    std::vector<std::uint8_t> actual(static_cast<std::size_t>(size));
                    Pattern::fill(static_cast<std::uint64_t>(i) * 100 + static_cast<std::uint64_t>(round), 0, expected.data(), expected.size());
                    ok = ctx.upload(*first, 0, expected.data(), size, error) && ctx.copy(*first, 0, *second, 0, size, error) && ctx.finish(error) &&
                         ctx.download(*second, 0, actual.data(), size, error) &&
                         BackendHarness::verify(context, api + " thread " + std::to_string(i), expected.data(), actual.data(), size, error);
                }
                if (!ok) {
                    std::lock_guard lock(problems_mutex);
                    problems.add(api + " thread " + std::to_string(i) + ": " + error);
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
    });
    return problems.outcome();
}

Outcome ThreadChecks::multi_queue_vulkan(Context& context) {
    std::string error;
    const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(error);
    if (adapters.empty()) {
        return Context::skip("Vulkan is unavailable: " + error);
    }
    VulkanRig rig;
    VulkanRigOptions options;
    options.queues_per_family = 2;
    if (!rig.open(adapters.front(), options, error)) {
        return Outcome::fail(error);
    }
    VulkanQueue* first = nullptr;
    VulkanQueue* second = nullptr;
    for (VulkanQueue* a : rig.queues_supporting(VK_QUEUE_COMPUTE_BIT)) {
        for (VulkanQueue* b : rig.queues_supporting(VK_QUEUE_COMPUTE_BIT)) {
            if (a != b && a->family == b->family && first == nullptr) {
                first = a;
                second = b;
            }
        }
    }
    if (first == nullptr) {
        return Context::skip("the device exposes no two compute queues of one family");
    }
    VulkanBuffer buffer;
    VulkanCompute compute;
    VkResult result = buffer.create(rig, 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (result == VK_SUCCESS) {
        buffer.fill_value(0);
        buffer.publish();
        result = compute.create(rig, VulkanShaders::words(VulkanShader::Step), {VulkanBinding{}}, 16);
    }
    VulkanOwned<VkSemaphore> forward;
    VulkanOwned<VkSemaphore> backward;
    VulkanOwned<VkFence> fence;
    if (result == VK_SUCCESS) {
        compute.write_buffer(0, buffer.buffer);
        result = rig.make_semaphore(forward);
    }
    if (result == VK_SUCCESS) {
        result = rig.make_semaphore(backward);
    }
    if (result == VK_SUCCESS) {
        result = rig.make_fence(fence);
    }
    if (result != VK_SUCCESS) {
        return Outcome::fail("creating the objects failed: " + VulkanTools::result_name(result));
    }
    std::vector<std::pair<VulkanQueue*, VkCommandBuffer>> issued;
    bool backward_armed = false;
    bool forward_armed = false;
    auto step = [&](VulkanQueue& queue, std::uint32_t op, std::uint32_t value, std::uint32_t slot, VkSemaphore wait, VkSemaphore signal, VkFence completion) -> VkResult {
        VkCommandBufferAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = queue.pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        VkCommandBuffer commands = nullptr;
        VkResult code = rig.fn.vkAllocateCommandBuffers(rig.device, &allocate, &commands);
        if (code != VK_SUCCESS) {
            return code;
        }
        issued.emplace_back(&queue, commands);
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        code = rig.fn.vkBeginCommandBuffer(commands, &begin);
        if (code != VK_SUCCESS) {
            return code;
        }
        const std::uint32_t values[4] = {op, value, slot, 0};
        compute.bind(commands);
        compute.push(commands, values, sizeof(values));
        compute.dispatch(commands, 1);
        VkMemoryBarrier memory{};
        memory.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memory.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        memory.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &memory, 0, nullptr, 0, nullptr);
        code = rig.fn.vkEndCommandBuffer(commands);
        if (code != VK_SUCCESS) {
            return code;
        }
        const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        VkSubmitInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.waitSemaphoreCount = wait != VK_NULL_HANDLE ? 1 : 0;
        info.pWaitSemaphores = &wait;
        info.pWaitDstStageMask = &stage;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &commands;
        info.signalSemaphoreCount = signal != VK_NULL_HANDLE ? 1 : 0;
        info.pSignalSemaphores = &signal;
        return rig.submit(queue, 1, &info, completion);
    };
    auto finish = [&](VkResult code) -> std::string {
        if (code == VK_SUCCESS) {
            VkFence raw = fence.get();
            code = rig.wait_fence(raw, 30000000000ull);
            if (code == VK_SUCCESS) {
                code = rig.fn.vkResetFences(rig.device, 1, &raw);
            }
        }
        if (code != VK_SUCCESS) {
            return VulkanTools::result_name(code);
        }
        for (auto& [queue, commands] : issued) {
            rig.fn.vkFreeCommandBuffers(rig.device, queue->pool, 1, &commands);
        }
        issued.clear();
        return {};
    };
    for (int round = 0; round < 200; ++round) {
        const std::uint32_t base = 10u * static_cast<std::uint32_t>(round);
        VkResult code = step(*first, 0, base, 0, backward_armed ? backward.get() : VK_NULL_HANDLE, forward.get(), VK_NULL_HANDLE);
        backward_armed = false;
        if (code == VK_SUCCESS) {
            code = step(*second, 1, 3, 0, forward.get(), VK_NULL_HANDLE, fence.get());
        }
        const std::string failed = finish(code);
        if (!failed.empty()) {
            return Outcome::fail("round " + std::to_string(round) + ": " + failed);
        }
        buffer.acquire();
        const std::uint32_t seen = buffer.words()[0];
        if (seen != base + 3u) {
            return Outcome::fail("round " + std::to_string(round) + ": slot 0 holds " + std::to_string(seen) + ", expected " + std::to_string(base + 3u));
        }
    }
    constexpr int kChain = 1000;
    VkResult code = VK_SUCCESS;
    for (int i = 0; i < kChain && code == VK_SUCCESS; ++i) {
        const bool last = i == kChain - 1;
        code = step(*first, 1, 1, 1, forward_armed ? backward.get() : VK_NULL_HANDLE, forward.get(), VK_NULL_HANDLE);
        if (code == VK_SUCCESS) {
            code = step(*second, 1, 1, 1, forward.get(), last ? VK_NULL_HANDLE : backward.get(), last ? fence.get() : VK_NULL_HANDLE);
        }
        forward_armed = true;
    }
    const std::string failed = finish(code);
    if (!failed.empty()) {
        return Outcome::fail("the dependency chain failed: " + failed);
    }
    buffer.acquire();
    if (buffer.words()[1] != 2u * kChain) {
        return Outcome::fail("two queues alternating " + std::to_string(2 * kChain) + " dependent increments left " + std::to_string(buffer.words()[1]));
    }
    context.measure("chain", 2 * kChain);
    return Outcome::ok();
}

Outcome ThreadChecks::d3d12_multi_queue(Context&) {
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error) || !rig.open_queues(error)) {
        return Outcome::fail(error);
    }
    D3d12Root root;
    root.constants(4, 0).uav(1);
    ID3D12RootSignature* signature = root.build(rig, error);
    D3d12Tools::Bytes code;
    ID3D12PipelineState* state = nullptr;
    if (signature != nullptr && D3d12Tools::compile(kStepHlsl, "main", "cs_5_1", code, error)) {
        state = D3d12Tools::compute_pso(rig, signature, code, error);
    }
    ID3D12Resource* buffer = D3d12Tools::device_buffer(rig, 256, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ID3D12Resource* readback = D3d12Tools::readback(rig, 256);
    if (state == nullptr || buffer == nullptr || readback == nullptr) {
        return Outcome::fail("creating the objects failed: " + error);
    }
    auto dispatch = [&](D3d12Queue& queue, std::uint32_t op, std::uint32_t value, std::string& detail) -> std::uint64_t {
        const std::uint32_t values[4] = {op, value, 0, 0};
        ID3D12CommandList* list = queue.record(
            [&](ID3D12GraphicsCommandList* l) {
                D3d12Tools::move(l, buffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                l->SetComputeRootSignature(signature);
                l->SetPipelineState(state);
                l->SetComputeRoot32BitConstants(0, 4, values, 0);
                l->SetComputeRootUnorderedAccessView(1, buffer->GetGPUVirtualAddress());
                l->Dispatch(1, 1, 1);
                D3d12Tools::move(l, buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
            },
            detail);
        return list == nullptr ? 0 : queue.execute(&list, 1);
    };
    for (int round = 0; round < 100; ++round) {
        D3d12Queue& writer = round % 2 == 0 ? rig.compute() : rig.direct();
        D3d12Queue& adder = round % 2 == 0 ? rig.direct() : rig.compute();
        const std::uint32_t base = 100u + static_cast<std::uint32_t>(round) * 13u;
        const std::uint64_t first = dispatch(writer, 0, base, error);
        if (first == 0 || !adder.gpu_wait(writer, first)) {
            return Outcome::fail("round " + std::to_string(round) + ": first stage failed: " + error);
        }
        const std::uint64_t second = dispatch(adder, 1, 5, error);
        if (second == 0 || !rig.copy().gpu_wait(adder, second)) {
            return Outcome::fail("round " + std::to_string(round) + ": second stage failed: " + error);
        }
        ID3D12CommandList* copy = rig.copy().record([&](ID3D12GraphicsCommandList* l) { l->CopyBufferRegion(readback, 0, buffer, 0, 16); }, error);
        if (copy == nullptr) {
            return Outcome::fail("recording the copy failed: " + error);
        }
        const std::uint64_t third = rig.copy().execute(&copy, 1);
        D3d12Tools::Bytes bytes;
        if (!rig.copy().wait(third, error) || !D3d12Tools::map_bytes(readback, 0, 16, bytes, error)) {
            return Outcome::fail("round " + std::to_string(round) + ": " + error);
        }
        std::uint32_t seen = 0;
        std::memcpy(&seen, bytes.data(), 4);
        if (seen != base + 5u) {
            return Outcome::fail("round " + std::to_string(round) + ": the copy queue read " + std::to_string(seen) + " after the write and the add on two other queues, expected " +
                                 std::to_string(base + 5u));
        }
    }
    return rig.idle(error) ? Outcome::ok() : Outcome::fail(error);
}

Outcome ThreadChecks::immediate_context_serialised(Context&) {
    if (ApiPolicy::is_skipped("d3d11")) {
        return Context::skip("d3d11 excluded by LSW_SUITES_SKIP_APIS");
    }
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    Com<ID3D11ComputeShader> shader = D3d11Tools::cs(device, kStepHlsl, error);
    Com<ID3D11Buffer> buffer = D3d11Tools::buffer(device, 64 * 4, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, nullptr);
    Com<ID3D11UnorderedAccessView> view = buffer ? D3d11Tools::structured_uav(device, buffer.get(), 64) : Com<ID3D11UnorderedAccessView>();
    if (!shader || !view) {
        return Outcome::fail("creating the objects failed: " + error);
    }
    constexpr int kLists = 24;
    auto constants = [&](std::uint32_t op, std::uint32_t value, std::uint32_t slot) {
        const std::uint32_t values[4] = {op, value, slot, 0};
        return D3d11Tools::buffer(device, 16, D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, values);
    };
    std::vector<Com<ID3D11Buffer>> set_slot0(kLists);
    std::vector<Com<ID3D11Buffer>> add_slot0(kLists);
    std::vector<Com<ID3D11Buffer>> add_own(kLists);
    for (int i = 0; i < kLists; ++i) {
        set_slot0[static_cast<std::size_t>(i)] = constants(0, static_cast<std::uint32_t>(i) * 3, 0);
        add_slot0[static_cast<std::size_t>(i)] = constants(1, 1000, 0);
        add_own[static_cast<std::size_t>(i)] = constants(1, 7, static_cast<std::uint32_t>(i) + 1);
        if (!set_slot0[static_cast<std::size_t>(i)] || !add_slot0[static_cast<std::size_t>(i)] || !add_own[static_cast<std::size_t>(i)]) {
            return Outcome::fail("creating the constant buffers failed");
        }
    }
    std::vector<Com<ID3D11CommandList>> lists(kLists);
    std::vector<std::string> failures(kLists);
    std::vector<std::thread> threads;
    for (int i = 0; i < kLists; ++i) {
        threads.emplace_back([&, i] {
            const std::size_t at = static_cast<std::size_t>(i);
            ID3D11DeviceContext* deferred = nullptr;
            if (FAILED(device.device->CreateDeferredContext(0, &deferred))) {
                failures[at] = "CreateDeferredContext failed";
                return;
            }
            ID3D11UnorderedAccessView* uav = view.get();
            deferred->CSSetShader(shader.get(), nullptr, 0);
            deferred->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
            for (ID3D11Buffer* cb : {set_slot0[at].get(), add_slot0[at].get(), add_own[at].get()}) {
                deferred->CSSetConstantBuffers(0, 1, &cb);
                deferred->Dispatch(1, 1, 1);
            }
            if (FAILED(deferred->FinishCommandList(FALSE, lists[at].put()))) {
                failures[at] = "FinishCommandList failed";
            }
            deferred->Release();
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    for (const std::string& failure : failures) {
        if (!failure.empty()) {
            return Outcome::fail(failure);
        }
    }
    std::vector<int> order(kLists);
    std::iota(order.begin(), order.end(), 0);
    std::mt19937 random(77);
    Problems problems;
    for (int run = 0; run < 6; ++run) {
        if (run == 1) {
            std::reverse(order.begin(), order.end());
        } else if (run > 1) {
            std::shuffle(order.begin(), order.end(), random);
        }
        const UINT zero[4] = {0, 0, 0, 0};
        device.context->ClearUnorderedAccessViewUint(view.get(), zero);
        for (int index : order) {
            device.context->ExecuteCommandList(lists[static_cast<std::size_t>(index)].get(), FALSE);
        }
        const std::vector<std::uint32_t> words = D3d11Tools::read_words(device, buffer.get(), error);
        if (words.size() < kLists + 1) {
            return Outcome::fail("reading the buffer failed: " + error);
        }
        const std::uint32_t expected0 = static_cast<std::uint32_t>(order.back()) * 3u + 1000u;
        if (words[0] != expected0) {
            problems.add("run " + std::to_string(run) + ": slot 0 holds " + std::to_string(words[0]) + ", the last executed list is " + std::to_string(order.back()) + " and gives " + std::to_string(expected0));
        }
        for (int i = 0; i < kLists; ++i) {
            if (words[static_cast<std::size_t>(i) + 1] != 7u) {
                problems.add("run " + std::to_string(run) + ": list " + std::to_string(i) + " own slot holds " + std::to_string(words[static_cast<std::size_t>(i) + 1]) + ", expected 7");
                break;
            }
        }
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::sync
