#include "sanity/d3d12/fence_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/d3d12/kit.hpp"
#include "support/child_runs.hpp"

#include <atomic>
#include <cstring>
#include <thread>

namespace lsw::suites::sanity::d3d12 {

namespace {

const char* kShowPs =
    "StructuredBuffer<uint> t : register(t0);\n"
    "float4 main() : SV_Target { return float4(t[0], t[1], t[2], t[3]) / 255.0; }\n";

const char* kComputeValues =
    "RWStructuredBuffer<uint> o : register(u0);\n"
    "[numthreads(4, 1, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) { o[id.x] = (id.x + 1u) * 37u; }\n";

std::uint32_t pack(const std::uint32_t values[4]) {
    return Kit::rgba(static_cast<int>(values[0]), static_cast<int>(values[1]), static_cast<int>(values[2]), static_cast<int>(values[3]));
}

bool show_buffer(D3d12Rig& rig, D3d12Queue& wait_on, std::uint64_t value, ID3D12Resource* buffer, D3D12_RESOURCE_STATES state, ID3D12RootSignature* signature,
                 ID3D12PipelineState* pso, std::uint32_t expected, const std::string& label, Problems& problems) {
    D3d12Frame frame;
    std::string error;
    TexelImage image;
    if (!frame.open(rig, 4, 4, error)) {
        problems.add(label + ": " + error);
        return false;
    }
    if (!rig.direct().gpu_wait(wait_on, value)) {
        problems.add(label + ": the wait value was not signalled by the producing queue");
        return false;
    }
    const bool done = frame.run(
        [&](ID3D12GraphicsCommandList* list) {
            if (state != D3D12_RESOURCE_STATE_COMMON) {
                D3d12Tools::move(list, buffer, state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
            list->SetGraphicsRootSignature(signature);
            list->SetPipelineState(pso);
            list->SetGraphicsRootShaderResourceView(0, buffer->GetGPUVirtualAddress());
            list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            list->DrawInstanced(3, 1, 0, 0);
        },
        error);
    if (!done || !frame.read(image, error)) {
        problems.add(label + ": " + error);
        return false;
    }
    std::string where;
    if (!Kit::uniform(image, expected, where)) {
        problems.add(label + ": " + where);
        return false;
    }
    return true;
}

HANDLE make_event() {
    return ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

bool reaches(ID3D12Fence* fence, std::uint64_t value, DWORD timeout) {
    HANDLE event = make_event();
    if (FAILED(fence->SetEventOnCompletion(value, event))) {
        ::CloseHandle(event);
        return false;
    }
    const DWORD waited = ::WaitForSingleObject(event, timeout);
    ::CloseHandle(event);
    return waited == WAIT_OBJECT_0;
}

}  // namespace

Outcome FenceChecks::cross_queue(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::string error;
        if (!rig.open_queues(error)) {
            problems.add(error);
            return {};
        }
        D3d12Root show_root;
        show_root.srv(0);
        ID3D12RootSignature* show_signature = show_root.build(rig, error);
        ID3D12PipelineState* show = show_signature != nullptr ? Kit::pipeline(rig, show_signature, kShowPs, DXGI_FORMAT_R8G8B8A8_UNORM, error) : nullptr;
        D3d12Root compute_root;
        compute_root.uav(0);
        ID3D12RootSignature* compute_signature = compute_root.build(rig, error);
        ID3D12PipelineState* compute = compute_signature != nullptr ? D3d12Tools::compute_from(rig, compute_signature, kComputeValues, error) : nullptr;
        if (show == nullptr || compute == nullptr) {
            problems.add("setup failed: " + error);
            return {};
        }
        const UINT64 big = 16 * 1024 * 1024;
        {
            const std::uint32_t values[4] = {17, 99, 201, 250};
            ID3D12Resource* upload = D3d12Tools::upload(rig, 16);
            ID3D12Resource* weight_source = D3d12Tools::device_buffer(rig, big, D3D12_RESOURCE_STATE_COMMON);
            ID3D12Resource* weight_target = D3d12Tools::device_buffer(rig, big, D3D12_RESOURCE_STATE_COMMON);
            ID3D12Resource* target = D3d12Tools::device_buffer(rig, 16, D3D12_RESOURCE_STATE_COMMON);
            if (upload == nullptr || weight_source == nullptr || weight_target == nullptr || target == nullptr) {
                problems.add("copy to direct: creating the buffers failed");
                return {};
            }
            void* mapped = nullptr;
            const D3D12_RANGE none{0, 0};
            upload->Map(0, &none, &mapped);
            std::memcpy(mapped, values, sizeof(values));
            upload->Unmap(0, nullptr);
            std::uint64_t produced = 0;
            if (!rig.copy().run(
                    [&](ID3D12GraphicsCommandList* list) {
                        list->CopyBufferRegion(weight_target, 0, weight_source, 0, big);
                        list->CopyBufferRegion(target, 0, upload, 0, 16);
                    },
                    produced, error)) {
                problems.add("copy to direct: " + error);
                return {};
            }
            show_buffer(rig, rig.copy(), produced, target, D3D12_RESOURCE_STATE_COMMON, show_signature, show, pack(values), "a draw that waits for a copy queue upload", problems);
        }
        {
            ID3D12Resource* produced_buffer = D3d12Tools::device_buffer(rig, 16, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            if (produced_buffer == nullptr) {
                problems.add("compute to direct: creating the buffer failed");
                return {};
            }
            std::uint64_t produced = 0;
            if (!rig.compute().run(
                    [&](ID3D12GraphicsCommandList* list) {
                        list->SetComputeRootSignature(compute_signature);
                        list->SetPipelineState(compute);
                        list->SetComputeRootUnorderedAccessView(0, produced_buffer->GetGPUVirtualAddress());
                        list->Dispatch(1, 1, 1);
                    },
                    produced, error)) {
                problems.add("compute to direct: " + error);
                return {};
            }
            const std::uint32_t expected[4] = {37, 74, 111, 148};
            show_buffer(rig, rig.compute(), produced, produced_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, show_signature, show, pack(expected),
                        "a draw that waits for a compute queue dispatch", problems);
        }
        {
            ID3D12Resource* upload = D3d12Tools::upload(rig, big);
            ID3D12Resource* staging = D3d12Tools::device_buffer(rig, big, D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12Resource* readback = D3d12Tools::readback(rig, 4096);
            if (upload == nullptr || staging == nullptr || readback == nullptr) {
                problems.add("direct to copy: creating the buffers failed");
                return {};
            }
            const std::vector<std::uint8_t> data = Kit::bytes(700, 4096);
            void* mapped = nullptr;
            const D3D12_RANGE none{0, 0};
            upload->Map(0, &none, &mapped);
            std::memcpy(mapped, data.data(), data.size());
            upload->Unmap(0, nullptr);
            std::uint64_t produced = 0;
            if (!rig.direct().run(
                    [&](ID3D12GraphicsCommandList* list) {
                        list->CopyBufferRegion(staging, 0, upload, 0, big);
                        D3d12Tools::move(list, staging, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
                    },
                    produced, error)) {
                problems.add("direct to copy: " + error);
                return {};
            }
            if (!rig.copy().gpu_wait(rig.direct(), produced)) {
                problems.add("direct to copy: the wait value was not signalled");
                return {};
            }
            std::vector<std::uint8_t> back;
            if (!rig.copy().run_wait([&](ID3D12GraphicsCommandList* list) { list->CopyBufferRegion(readback, 0, staging, 0, 4096); }, error) ||
                !D3d12Tools::map_bytes(readback, 0, 4096, back, error)) {
                problems.add("direct to copy: " + error);
                return {};
            }
            static_cast<void>(Kit::same_bytes(context, problems, "a copy queue copy that waits for a direct queue upload", data.data(), back.data(), data.size()));
        }
        return {};
    });
}

Outcome FenceChecks::cpu_semantics(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        ID3D12Device* d = rig.device();
        Com<ID3D12Fence> owned;
        if (!Kit::succeeds(problems, "CreateFence", d->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), owned.put_void()))) {
            return {};
        }
        ID3D12Fence* fence = rig.hold(std::move(owned));
        if (fence->GetCompletedValue() != 0) {
            problems.add("a new fence created with 0 reports another completed value");
        }
        if (!reaches(fence, 0, 1000)) {
            problems.add("SetEventOnCompletion for an already reached value does not fire immediately");
        }
        HANDLE event = make_event();
        Kit::succeeds(problems, "SetEventOnCompletion(5)", fence->SetEventOnCompletion(5, event));
        if (::WaitForSingleObject(event, 100) != WAIT_TIMEOUT) {
            problems.add("the event for value 5 fired before the fence was signalled");
        }
        fence->Signal(3);
        if (::WaitForSingleObject(event, 100) != WAIT_TIMEOUT) {
            problems.add("the event for value 5 fired when the fence reached only 3");
        }
        if (fence->GetCompletedValue() != 3) {
            problems.add("CPU Signal(3) leaves the completed value at " + std::to_string(fence->GetCompletedValue()));
        }
        fence->Signal(5);
        if (::WaitForSingleObject(event, 5000) != WAIT_OBJECT_0) {
            problems.add("the event for value 5 did not fire after Signal(5)");
        }
        Kit::succeeds(problems, "SetEventOnCompletion(10)", fence->SetEventOnCompletion(10, event));
        fence->Signal(12);
        if (::WaitForSingleObject(event, 5000) != WAIT_OBJECT_0) {
            problems.add("the event for value 10 did not fire when the fence jumped to 12");
        }
        ::CloseHandle(event);
        fence->Signal(2);
        const std::uint64_t after_lower = fence->GetCompletedValue();
        context.measure("d3d12.lower_signal_completed", static_cast<long long>(after_lower));
        if (after_lower != 12 && after_lower != 2) {
            problems.add("signalling a lower value leaves the completed value at " + std::to_string(after_lower) + ", neither unchanged nor the new value");
        }
        fence->Signal(13);
        if (fence->GetCompletedValue() != 13 || !reaches(fence, 13, 1000)) {
            problems.add("after signalling 13 the completed value or its event is wrong");
        }
        Kit::succeeds(problems, "SetEventOnCompletion with a null event for a reached value", fence->SetEventOnCompletion(13, nullptr));
        {
            HANDLE events[4];
            for (HANDLE& item : events) {
                item = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
                fence->SetEventOnCompletion(20, item);
            }
            fence->Signal(20);
            for (HANDLE item : events) {
                if (::WaitForSingleObject(item, 5000) != WAIT_OBJECT_0) {
                    problems.add("one of four waiters on the same value did not fire");
                }
                ::CloseHandle(item);
            }
        }
        D3d12Queue& queue = rig.direct();
        Com<ID3D12Fence> gate_owner;
        if (Kit::succeeds(problems, "CreateFence(gate)", d->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), gate_owner.put_void()))) {
            ID3D12Fence* gate = rig.hold(std::move(gate_owner));
            queue.gpu_wait_fence(gate, 100);
            const std::uint64_t behind = queue.signal();
            if (queue.reached(behind)) {
                problems.add("a queue signal queued behind a wait on an unsignalled fence completed anyway");
            }
            gate->Signal(100);
            std::string error;
            if (!queue.wait(behind, error)) {
                problems.add("the queue did not continue after the CPU signalled the fence it waits on: " + error);
            }
        }
        std::string error;
        std::uint64_t previous = 0;
        std::uint64_t last = 0;
        for (int i = 0; i < 1000; ++i) {
            last = queue.signal();
        }
        for (int spin = 0; spin < 200000 && previous < last; ++spin) {
            const std::uint64_t now = queue.fence()->GetCompletedValue();
            if (now < previous) {
                problems.add("GetCompletedValue went backwards from " + std::to_string(previous) + " to " + std::to_string(now));
                break;
            }
            previous = now;
            if (previous < last) {
                ::Sleep(0);
            }
        }
        if (previous < last) {
            problems.add("1000 queued signals did not complete");
        }
        for (int i = 0; i < 10000; ++i) {
            const std::uint64_t value = queue.signal();
            if (!queue.wait(value, 5000, error)) {
                problems.add("wait " + std::to_string(i) + " of 10000: " + error);
                break;
            }
        }
        return {};
    });
}

Outcome FenceChecks::shared(Context& context) {
    const std::string name_text = ChildMode::variable(L"LSW_SUITES_FENCE_NAME");
    if (ChildMode::active() && !name_text.empty()) {
        return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
            HANDLE handle = nullptr;
            if (!Kit::succeeds(problems, "OpenSharedHandleByName", rig.device()->OpenSharedHandleByName(Text::wide(name_text).c_str(), GENERIC_ALL, &handle))) {
                return {};
            }
            Com<ID3D12Fence> opened;
            const HRESULT result = rig.device()->OpenSharedHandle(handle, __uuidof(ID3D12Fence), opened.put_void());
            ::CloseHandle(handle);
            if (!Kit::succeeds(problems, "OpenSharedHandle", result)) {
                return {};
            }
            ID3D12Fence* fence = rig.hold(std::move(opened));
            if (!reaches(fence, 1, 60000)) {
                problems.add("the child never saw the parent's CPU signal of 1");
                return {};
            }
            fence->Signal(2);
            if (!reaches(fence, 3, 60000)) {
                problems.add("the child never saw the parent's queue signal of 3");
                return {};
            }
            fence->Signal(4);
            return {};
        });
    }
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        ID3D12Device* d = rig.device();
        Com<ID3D12Fence> created;
        const HRESULT made = d->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), created.put_void());
        if (FAILED(made)) {
            Com<ID3D12Fence> plain;
            const HRESULT local = d->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), plain.put_void());
            if (FAILED(local)) {
                problems.add("a plain fence cannot be created: " + Kit::hr(local));
                return {};
            }
            return "shared fences are unsupported: CreateFence(SHARED) returned " + Kit::hr(made);
        }
        ID3D12Fence* fence = rig.hold(std::move(created));
        const std::string name = "Local\\lsw-suites-d3d12-fence-" + std::to_string(::GetCurrentProcessId());
        HANDLE handle = nullptr;
        if (!Kit::succeeds(problems, "CreateSharedHandle(fence)", d->CreateSharedHandle(fence, nullptr, GENERIC_ALL, Text::wide(name).c_str(), &handle))) {
            return {};
        }
        D3d12Rig second;
        std::string error;
        if (!second.open_luid(rig.luid, D3D_FEATURE_LEVEL_11_0, error)) {
            problems.add("the second device cannot be created: " + error);
            ::CloseHandle(handle);
            return {};
        }
        Com<ID3D12Fence> twin_owner;
        if (!Kit::succeeds(problems, "OpenSharedHandle in the second device", second.device()->OpenSharedHandle(handle, __uuidof(ID3D12Fence), twin_owner.put_void()))) {
            ::CloseHandle(handle);
            return {};
        }
        ID3D12Fence* twin = second.hold(std::move(twin_owner));
        fence->Signal(7);
        if (twin->GetCompletedValue() != 7) {
            problems.add("a CPU signal on the first device is not visible through the second device's fence");
        }
        twin->Signal(8);
        if (!reaches(fence, 8, 5000)) {
            problems.add("a CPU signal on the second device does not complete a wait on the first");
        }
        ::CloseHandle(handle);
        handle = nullptr;
        Com<ID3D12Fence> shared_again;
        if (!Kit::succeeds(problems, "CreateFence(SHARED) for the process test", d->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), shared_again.put_void()))) {
            return {};
        }
        ID3D12Fence* process_fence = rig.hold(std::move(shared_again));
        HANDLE process_handle = nullptr;
        if (!Kit::succeeds(problems, "CreateSharedHandle(process fence)", d->CreateSharedHandle(process_fence, nullptr, GENERIC_ALL, Text::wide(name + "-p").c_str(), &process_handle))) {
            return {};
        }
        process_fence->Signal(1);
        ChildRunOptions options;
        options.timeout_seconds = 150;
        options.env = {{"LSW_SUITES_FENCE_NAME", name + "-p"}};
        CheckResult child;
        std::thread runner([&] { child = ChildRuns::run_self("sanity.d3d12.fence.shared", options); });
        if (!reaches(process_fence, 2, 90000)) {
            problems.add("the child process never completed the wait for 1 and signalled 2");
        }
        D3d12Queue& queue = rig.direct();
        queue.queue()->Signal(process_fence, 3);
        queue.touch();
        if (!reaches(process_fence, 4, 90000)) {
            problems.add("the child process did not see the queue signal of 3 and answer with 4");
        }
        runner.join();
        if (child.status != Status::Ok) {
            problems.add("the child process failed: " + child.reason);
        }
        ::CloseHandle(process_handle);
        rig.release(process_fence);
        if (!queue.idle(error)) {
            problems.add(error);
        }
        rig.release(fence);
        twin->Signal(9);
        if (!reaches(twin, 9, 5000) || twin->GetCompletedValue() != 9) {
            problems.add("after the first device's handle and fence were closed the other device's fence is unusable");
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::d3d12
