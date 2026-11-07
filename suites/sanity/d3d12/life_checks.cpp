#include "sanity/d3d12/life_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/d3d12/kit.hpp"
#include "support/child_runs.hpp"
#include "support/dxgi_budget.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace lsw::suites::sanity::d3d12 {

namespace {

constexpr UINT64 kMiB = 1024 * 1024;

bool round_trip(D3d12Rig& rig, std::uint64_t seed, std::string& error) {
    const UINT64 size = 64 * 1024;
    ID3D12Resource* target = D3d12Tools::device_buffer(rig, size, D3D12_RESOURCE_STATE_COPY_DEST);
    if (target == nullptr) {
        error = "creating a buffer failed";
        return false;
    }
    const std::vector<std::uint8_t> data = Kit::bytes(seed, static_cast<std::size_t>(size));
    std::vector<std::uint8_t> back;
    if (!D3d12Tools::write_buffer(rig, target, D3D12_RESOURCE_STATE_COPY_DEST, 0, data.data(), size, error) ||
        !D3d12Tools::read_buffer(rig, target, D3D12_RESOURCE_STATE_COPY_DEST, 0, size, back, error)) {
        return false;
    }
    if (std::memcmp(back.data(), data.data(), data.size()) != 0) {
        error = "the data differs after the round trip";
        return false;
    }
    return true;
}

const char* kAdd =
    "cbuffer B : register(b0) { uint base; uint3 pad; }\n"
    "cbuffer K : register(b1) { uint k; uint3 pad2; }\n"
    "RWStructuredBuffer<uint> o : register(u0);\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) { o[base + id.x] += k; }\n";

}  // namespace

Outcome LifeChecks::removal_reasons(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::string error;
        if (rig.removed_reason() != S_OK) {
            problems.add("GetDeviceRemovedReason on a healthy device returned " + Kit::hr(rig.removed_reason()));
            return {};
        }
        Com<ID3D12Device5> device5 = rig.as<ID3D12Device5>();
        if (!device5) {
            return "ID3D12Device5 (RemoveDevice) is not exposed by this runtime";
        }
        D3d12Rig witness;
        if (!witness.open_luid(rig.luid, D3D_FEATURE_LEVEL_11_0, error)) {
            problems.add("the witness device cannot be created: " + error);
            return {};
        }
        if (!round_trip(rig, 1500, error) || !round_trip(witness, 1501, error)) {
            problems.add("the devices fail before the removal: " + error);
            return {};
        }
        device5->RemoveDevice();
        const HRESULT reason = rig.removed_reason();
        context.measure("d3d12.removed_reason", Kit::hr(reason));
        if (!D3d12Tools::in(reason, {D3d12Tools::kDeviceRemoved, D3d12Tools::kDeviceHung, D3d12Tools::kDeviceReset, D3d12Tools::kDriverInternalError, D3d12Tools::kInvalidCall})) {
            problems.add("GetDeviceRemovedReason after RemoveDevice returned " + Kit::hr(reason) + ", which is not a documented removal reason");
        } else if (reason != D3d12Tools::kDeviceRemoved) {
            problems.add("GetDeviceRemovedReason after RemoveDevice returned " + Kit::hr(reason) + ", expected DXGI_ERROR_DEVICE_REMOVED");
        }
        if (rig.direct().fence()->GetCompletedValue() != ~0ull) {
            problems.add("the fence of a removed device does not report UINT64_MAX");
        }
        std::string wait_error;
        if (!rig.direct().wait(~0ull - 1, 5000, wait_error)) {
            problems.add("waiting on a removed device's fence does not complete: " + wait_error);
        }
        HRESULT made = S_OK;
        ID3D12Resource* created = D3d12Tools::device_buffer(rig, 4096, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_NONE, &made);
        if (created != nullptr) {
            problems.add("a resource was created on a removed device");
            rig.release(created);
        } else {
            context.measure("d3d12.create_after_removal", Kit::hr(made));
        }
        std::string submit_error;
        const auto started = std::chrono::steady_clock::now();
        static_cast<void>(rig.direct().run_wait([&](ID3D12GraphicsCommandList*) {}, submit_error));
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(10)) {
            problems.add("submitting to a removed device took more than 10 seconds");
        }
        if (witness.removed_reason() != S_OK) {
            problems.add("removing one device removed another device on the same adapter: " + Kit::hr(witness.removed_reason()));
        }
        if (!round_trip(witness, 1502, error)) {
            problems.add("the other device does not work after the removal: " + error);
        }
        D3d12Rig fresh;
        if (!fresh.open_luid(rig.luid, D3D_FEATURE_LEVEL_11_0, error)) {
            problems.add("a new device cannot be created on the adapter after a removal: " + error);
        } else if (!round_trip(fresh, 1503, error)) {
            problems.add("the new device does not work: " + error);
        }
        return {};
    });
}

Outcome LifeChecks::lifetime_exit(Context& context) {
    const std::string mode = ChildMode::variable(L"LSW_SUITES_STRESS_MODE");
    if (ChildMode::active() && !mode.empty()) {
        std::string error;
        AvAdapter adapter;
        if (!TargetAdapter::pick(adapter, error)) {
            return Outcome::fail(error);
        }
        D3d12Rig* rig = new D3d12Rig();
        if (!rig->open(adapter, D3D_FEATURE_LEVEL_11_0, error)) {
            return Outcome::fail(error);
        }
        const UINT64 size = 16 * kMiB;
        ID3D12Resource* upload = D3d12Tools::upload(*rig, size);
        ID3D12Resource* target = D3d12Tools::device_buffer(*rig, size, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* big = D3d12Tools::device_buffer(*rig, 128 * kMiB, D3D12_RESOURCE_STATE_COMMON);
        if (upload == nullptr || target == nullptr || big == nullptr) {
            return Outcome::fail("the exiting process cannot allocate its buffers");
        }
        for (int i = 0; i < 8; ++i) {
            std::uint64_t value = 0;
            if (!rig->direct().run([&](ID3D12GraphicsCommandList* list) { list->CopyBufferRegion(target, 0, upload, 0, size); }, value, error)) {
                return Outcome::fail(error);
            }
        }
        context.measure("child.held", static_cast<long long>(128 * kMiB + 2 * size));
        return Outcome::ok();
    }
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    VideoMemory baseline;
    if (!DxgiBudget::query(adapter.luid, true, baseline, error)) {
        return Outcome::fail(error);
    }
    const std::uint64_t free_before = baseline.budget > baseline.usage ? baseline.budget - baseline.usage : 0;
    for (int round = 0; round < 12; ++round) {
        D3d12Rig rig;
        if (!rig.open(adapter, D3D_FEATURE_LEVEL_11_0, error) || !rig.open_queues(error)) {
            problems.add("round " + std::to_string(round) + ": " + error);
            break;
        }
        const UINT64 size = 8 * kMiB;
        D3D12_HEAP_DESC heap_desc{};
        heap_desc.SizeInBytes = size;
        heap_desc.Properties = D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT);
        heap_desc.Flags = Kit::buffer_heap_flags(rig);
        Com<ID3D12Heap> heap_owner;
        Com<ID3D12Resource> placed_owner;
        if (FAILED(rig.device()->CreateHeap(&heap_desc, __uuidof(ID3D12Heap), heap_owner.put_void()))) {
            problems.add("round " + std::to_string(round) + ": CreateHeap failed");
            break;
        }
        ID3D12Heap* heap = rig.hold(std::move(heap_owner));
        const D3D12_RESOURCE_DESC desc = D3d12Tools::buffer_desc(size);
        rig.device()->CreatePlacedResource(heap, 0, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), placed_owner.put_void());
        ID3D12Resource* placed = rig.hold(std::move(placed_owner));
        ID3D12Resource* upload = D3d12Tools::upload(rig, size);
        ID3D12Resource* copy_target = D3d12Tools::device_buffer(rig, size, D3D12_RESOURCE_STATE_COMMON);
        if (placed == nullptr || upload == nullptr || copy_target == nullptr) {
            problems.add("round " + std::to_string(round) + ": creating the resources failed");
            break;
        }
        for (int i = 0; i < 6; ++i) {
            std::uint64_t value = 0;
            if (!rig.direct().run([&](ID3D12GraphicsCommandList* list) { list->CopyBufferRegion(placed, 0, upload, 0, size); }, value, error) ||
                !rig.copy().run([&](ID3D12GraphicsCommandList* list) { list->CopyBufferRegion(copy_target, 0, upload, 0, size); }, value, error)) {
                problems.add("round " + std::to_string(round) + ": " + error);
                break;
            }
        }
    }
    ChildRunOptions options;
    options.timeout_seconds = 120;
    options.env = {{"LSW_SUITES_STRESS_MODE", "exit"}};
    const CheckResult child = ChildRuns::run_self("sanity.d3d12.stress.lifetime_exit", options);
    if (child.status != Status::Ok) {
        problems.add("the process that exits with queued work failed: " + child.reason);
    }
    bool restored = false;
    VideoMemory after;
    for (int poll = 0; poll < 100 && !restored; ++poll) {
        if (DxgiBudget::query(adapter.luid, true, after, error)) {
            const std::uint64_t free_after = after.budget > after.usage ? after.budget - after.usage : 0;
            restored = free_after + (256 * kMiB) >= free_before;
        }
        if (!restored) {
            ::Sleep(100);
        }
    }
    context.measure("budget.free_before", static_cast<long long>(free_before));
    context.measure("budget.free_after", static_cast<long long>(after.budget > after.usage ? after.budget - after.usage : 0));
    if (!restored) {
        problems.add("video memory did not return to the baseline after twelve device lifetimes and a process that exited with queued work");
    }
    D3d12Rig check;
    if (!check.open(adapter, D3D_FEATURE_LEVEL_11_0, error) || !round_trip(check, 1600, error)) {
        problems.add("a device created after the churn does not work: " + error);
    }
    return problems.outcome();
}

Outcome LifeChecks::concurrent_queues(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::string error;
        D3d12Root root;
        root.constants(1, 0).cbv(1).uav(0);
        ID3D12RootSignature* signature = root.build(rig, error);
        ID3D12PipelineState* pso = signature != nullptr ? D3d12Tools::compute_from(rig, signature, kAdd, error) : nullptr;
        constexpr int kThreads = 4;
        ID3D12Resource* shared = D3d12Tools::device_buffer(rig, kThreads * 64 * 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ID3D12Resource* constants = D3d12Tools::upload(rig, 256);
        ID3D12Resource* readback = D3d12Tools::readback(rig, kThreads * 64 * 4);
        if (pso == nullptr || shared == nullptr || constants == nullptr || readback == nullptr) {
            problems.add("setup failed: " + error);
            return {};
        }
        void* mapped = nullptr;
        const D3D12_RANGE none{0, 0};
        constants->Map(0, &none, &mapped);
        const UINT one = 1;
        std::memcpy(mapped, &one, 4);
        constants->Unmap(0, nullptr);
        D3d12Queue queues[kThreads];
        for (D3d12Queue& queue : queues) {
            if (!queue.open(rig.device(), D3D12_COMMAND_LIST_TYPE_DIRECT, error)) {
                problems.add(error);
                return {};
            }
        }
        std::atomic<bool> stop{false};
        std::atomic<std::uint64_t> iterations[kThreads];
        std::string errors[kThreads];
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            iterations[t] = 0;
            threads.emplace_back([&, t] {
                D3d12Queue& queue = queues[t];
                std::string local;
                std::uint64_t count = 0;
                while (!stop.load()) {
                    std::uint64_t value = 0;
                    if (!queue.run(
                            [&](ID3D12GraphicsCommandList* list) {
                                list->SetComputeRootSignature(signature);
                                list->SetPipelineState(pso);
                                list->SetComputeRoot32BitConstant(0, static_cast<UINT>(t) * 64, 0);
                                list->SetComputeRootConstantBufferView(1, constants->GetGPUVirtualAddress());
                                list->SetComputeRootUnorderedAccessView(2, shared->GetGPUVirtualAddress());
                                list->Dispatch(1, 1, 1);
                            },
                            value, local)) {
                        errors[t] = local;
                        return;
                    }
                    ++count;
                    iterations[t] = count;
                    if (count % 16 == 0 && !queue.wait(value, 30000, local)) {
                        errors[t] = local;
                        return;
                    }
                }
                if (!queue.idle(local)) {
                    errors[t] = local;
                }
            });
        }
        const auto started = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - started < std::chrono::seconds(5)) {
            ::Sleep(50);
            bool failed = false;
            for (int t = 0; t < kThreads; ++t) {
                if (iterations[t].load() == 0 && std::chrono::steady_clock::now() - started > std::chrono::seconds(4)) {
                    failed = true;
                }
            }
            if (failed) {
                break;
            }
        }
        stop = true;
        for (std::thread& thread : threads) {
            thread.join();
        }
        for (int t = 0; t < kThreads; ++t) {
            if (!errors[t].empty()) {
                problems.add("thread " + std::to_string(t) + ": " + errors[t]);
            }
        }
        if (!problems.empty()) {
            return {};
        }
        for (D3d12Queue& queue : queues) {
            if (!rig.direct().gpu_wait(queue, queue.last_signalled())) {
                problems.add("a worker queue's last value was not signalled");
                return {};
            }
        }
        std::vector<std::uint8_t> raw;
        if (!rig.direct().run_wait(
                [&](ID3D12GraphicsCommandList* list) {
                    D3d12Tools::move(list, shared, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
                    list->CopyBufferRegion(readback, 0, shared, 0, kThreads * 64 * 4);
                    D3d12Tools::move(list, shared, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                },
                error) ||
            !D3d12Tools::map_bytes(readback, 0, kThreads * 64 * 4, raw, error)) {
            problems.add("reading the shared buffer failed: " + error);
            return {};
        }
        const std::vector<std::uint32_t> words = Kit::words(raw);
        std::uint64_t total = 0;
        for (int t = 0; t < kThreads; ++t) {
            const std::uint64_t expected = iterations[t].load();
            total += expected;
            for (int i = 0; i < 64; ++i) {
                if (words[static_cast<std::size_t>(t) * 64 + static_cast<std::size_t>(i)] != static_cast<std::uint32_t>(expected)) {
                    problems.add("slice " + std::to_string(t) + " element " + std::to_string(i) + " is " + std::to_string(words[static_cast<std::size_t>(t) * 64 + static_cast<std::size_t>(i)]) +
                                 " after " + std::to_string(expected) + " dispatches, expected " + std::to_string(expected));
                    break;
                }
            }
        }
        context.measure("d3d12.concurrent_dispatches", static_cast<long long>(total));
        if (rig.removed_reason() != S_OK) {
            problems.add("the device was removed: " + Kit::hr(rig.removed_reason()));
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::d3d12
