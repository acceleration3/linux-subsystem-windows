#include "sanity/kmd/surrogate_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/d3d12/share_checks.hpp"
#include "sanity/err/recovery_checks.hpp"
#include "sanity/iso/fleet.hpp"
#include "sanity/iso/handle_checks.hpp"
#include "sanity/iso/loads.hpp"
#include "sanity/iso/scene.hpp"
#include "sanity/life/life_checks.hpp"
#include "sanity/sync/process_checks.hpp"
#include "support/api_loop.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_tools.hpp"
#include "support/kmt_api.hpp"
#include "support/pattern.hpp"
#include "support/problems.hpp"
#include "support/robust_child.hpp"
#include "support/target_adapter.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace lsw::suites::sanity::kmd {

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

struct Fence12 {
    D3d12Rig rig;
    Com<ID3D12Fence> fence;

    bool open(std::string& error) {
        AvAdapter adapter;
        if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
            return false;
        }
        if (FAILED(rig.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), fence.put_void()))) {
            error = "CreateFence failed";
            return false;
        }
        return true;
    }
};

Outcome iso_child(Context& context) {
    return RobustChild::child(context, iso::Loads::role);
}

}  // namespace

Outcome SurrogateChecks::lock_returns_written_data(Context&) {
    Problems problems;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        if (!backend.supports_map()) {
            return;
        }
        for (std::uint64_t size : {4096ull, 8192ull, 65536ull, 1ull << 20, 16ull << 20}) {
            AllocStatus status = AllocStatus::Ok;
            std::string error;
            std::unique_ptr<GpuBuffer> buffer = backend.create(size, BufferKind::Upload, status, error);
            void* pointer = nullptr;
            if (buffer == nullptr || !backend.map(*buffer, &pointer, error)) {
                problems.add(api + " size " + std::to_string(size) + ": " + error);
                continue;
            }
            std::vector<std::uint8_t> expected(static_cast<std::size_t>(size));
            Pattern::fill(size, 0, expected.data(), expected.size());
            std::memcpy(pointer, expected.data(), expected.size());
            static_cast<void>(backend.unmap(*buffer, error));
            if (!backend.map(*buffer, &pointer, error)) {
                problems.add(api + " size " + std::to_string(size) + ": mapping again failed: " + error);
                continue;
            }
            if (std::memcmp(pointer, expected.data(), expected.size()) != 0) {
                problems.add(api + ": data written through a mapping is not there after unmap and map, size " + std::to_string(size));
            }
            static_cast<void>(backend.unmap(*buffer, error));
        }
    });
    return problems.outcome();
}

Outcome SurrogateChecks::lock_twice_status(Context& context) {
    Problems problems;
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    ID3D12Resource* upload = D3d12Tools::upload(rig, 4096);
    void* first = nullptr;
    void* second = nullptr;
    const D3D12_RANGE none{0, 0};
    if (upload == nullptr || FAILED(upload->Map(0, &none, &first)) || FAILED(upload->Map(0, &none, &second))) {
        return Outcome::fail("mapping an upload buffer twice failed");
    }
    if (first != second) {
        problems.add("two nested D3D12 Map calls returned different pointers");
    }
    upload->Unmap(0, nullptr);
    upload->Unmap(0, nullptr);
    D3d11Device device;
    if (device.create(adapter.luid, error)) {
        Com<ID3D11Texture2D> staging = D3d11Tools::texture(device, D3d11Tools::desc2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE));
        D3D11_MAPPED_SUBRESOURCE one{};
        D3D11_MAPPED_SUBRESOURCE two{};
        if (staging && SUCCEEDED(device.context->Map(staging.get(), 0, D3D11_MAP_READ_WRITE, 0, &one))) {
            const HRESULT again = device.context->Map(staging.get(), 0, D3D11_MAP_READ_WRITE, 0, &two);
            context.measure("d3d11_second_map", D3d11Tools::hr(again));
            if (SUCCEEDED(again) && two.pData != one.pData) {
                problems.add("a second D3D11 Map of a mapped subresource succeeded with another pointer");
            }
            device.context->Unmap(staging.get(), 0);
        }
        Com<ID3D11Texture2D> sample = D3d11Tools::texture(device, D3d11Tools::desc2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE));
        if (!sample) {
            problems.add("the D3D11 device is unusable after the double map");
        }
    }
    return problems.outcome();
}

Outcome SurrogateChecks::lock_pointer_stable_while_locked(Context&) {
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    constexpr UINT64 kSize = 1 << 20;
    ID3D12Resource* upload = D3d12Tools::upload(rig, kSize);
    ID3D12Resource* target = D3d12Tools::device_buffer(rig, kSize, D3D12_RESOURCE_STATE_COPY_DEST);
    void* pointer = nullptr;
    const D3D12_RANGE none{0, 0};
    if (upload == nullptr || target == nullptr || FAILED(upload->Map(0, &none, &pointer))) {
        return Outcome::fail("preparing the mapped buffer failed");
    }
    Problems problems;
    for (int round = 0; round < 30; ++round) {
        std::vector<std::uint8_t> data(kSize);
        Pattern::fill(500 + static_cast<std::uint64_t>(round), 0, data.data(), data.size());
        std::memcpy(pointer, data.data(), data.size());
        if (!rig.direct().run_wait([&](ID3D12GraphicsCommandList* list) { list->CopyBufferRegion(target, 0, upload, 0, kSize); }, error)) {
            problems.add("round " + std::to_string(round) + ": " + error);
            break;
        }
        void* again = nullptr;
        if (FAILED(upload->Map(0, &none, &again)) || again != pointer) {
            problems.add("the mapped pointer changed while GPU work ran");
            break;
        }
        upload->Unmap(0, nullptr);
        D3d12Tools::Bytes back;
        if (!D3d12Tools::read_buffer(rig, target, D3D12_RESOURCE_STATE_COPY_DEST, 0, kSize, back, error) || std::memcmp(back.data(), data.data(), data.size()) != 0) {
            problems.add("round " + std::to_string(round) + ": the GPU did not see what was written through the held mapping");
            break;
        }
    }
    upload->Unmap(0, nullptr);
    return problems.outcome();
}

Outcome SurrogateChecks::discard_and_nooverwrite(Context&) {
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    constexpr UINT kSize = 4096;
    std::vector<std::uint8_t> sentinel(kSize, 0x5A);
    Com<ID3D11Buffer> dynamic = D3d11Tools::buffer(device, kSize, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0, nullptr);
    Com<ID3D11Buffer> neighbour = D3d11Tools::buffer(device, kSize, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0, sentinel.data());
    Com<ID3D11Buffer> staging = D3d11Tools::buffer(device, kSize, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0, nullptr);
    Com<ID3D11Buffer> staging_neighbour = D3d11Tools::buffer(device, kSize, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0, nullptr);
    if (!dynamic || !neighbour || !staging || !staging_neighbour) {
        return Outcome::fail("creating the buffers failed");
    }
    Problems problems;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    for (int round = 0; round < 20; ++round) {
        if (FAILED(device.context->Map(dynamic.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)) || mapped.pData == nullptr) {
            return Outcome::fail("Map(WRITE_DISCARD) failed");
        }
        std::memset(mapped.pData, 0x11 + round, kSize / 2);
        device.context->Unmap(dynamic.get(), 0);
        if (FAILED(device.context->Map(dynamic.get(), 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped))) {
            return Outcome::fail("Map(WRITE_NO_OVERWRITE) failed");
        }
        std::memset(static_cast<std::uint8_t*>(mapped.pData) + kSize / 2, 0x80 + round, kSize / 2);
        device.context->Unmap(dynamic.get(), 0);
        device.context->CopyResource(staging.get(), dynamic.get());
        device.context->CopyResource(staging_neighbour.get(), neighbour.get());
        D3D11_MAPPED_SUBRESOURCE a{};
        D3D11_MAPPED_SUBRESOURCE b{};
        if (FAILED(device.context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &a)) || FAILED(device.context->Map(staging_neighbour.get(), 0, D3D11_MAP_READ, 0, &b))) {
            return Outcome::fail("mapping the staging buffers failed");
        }
        const auto* bytes = static_cast<const std::uint8_t*>(a.pData);
        if (bytes[0] != 0x11 + round || bytes[kSize / 2 - 1] != 0x11 + round || bytes[kSize / 2] != 0x80 + round || bytes[kSize - 1] != 0x80 + round) {
            problems.add("round " + std::to_string(round) + ": the discard and no-overwrite halves do not hold what was written");
        }
        if (std::memcmp(b.pData, sentinel.data(), kSize) != 0) {
            problems.add("round " + std::to_string(round) + ": a neighbouring buffer changed");
        }
        device.context->Unmap(staging.get(), 0);
        device.context->Unmap(staging_neighbour.get(), 0);
        if (!problems.empty()) {
            break;
        }
    }
    return problems.outcome();
}

Outcome SurrogateChecks::destroy_while_locked(Context&) {
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    for (int round = 0; round < 50; ++round) {
        Com<ID3D11Buffer> buffer = D3d11Tools::buffer(device, 1 << 20, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0, 0, nullptr);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (!buffer || FAILED(device.context->Map(buffer.get(), 0, D3D11_MAP_READ_WRITE, 0, &mapped))) {
            return Outcome::fail("mapping a staging buffer failed");
        }
        std::memset(mapped.pData, 0x77, 4096);
        buffer.reset();
    }
    device.context->Flush();
    Com<ID3D11Buffer> next = D3d11Tools::buffer(device, 1 << 20, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, nullptr);
    if (!next || device.device->GetDeviceRemovedReason() != S_OK) {
        problems.add("allocation or the device fails after destroying resources that were still mapped");
    }
    D3d12Rig rig;
    if (rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        for (int round = 0; round < 50; ++round) {
            ID3D12Resource* upload = D3d12Tools::upload(rig, 1 << 20);
            void* pointer = nullptr;
            const D3D12_RANGE none{0, 0};
            if (upload == nullptr || FAILED(upload->Map(0, &none, &pointer))) {
                problems.add("mapping a D3D12 upload buffer failed");
                break;
            }
            rig.release(upload);
        }
        ID3D12Resource* again = D3d12Tools::upload(rig, 1 << 20);
        if (again == nullptr) {
            problems.add("D3D12 allocation fails after releasing mapped resources");
        }
    }
    return problems.outcome();
}

Outcome SurrogateChecks::nt_handle_share(Context& context) {
    return d3d12::ShareChecks::heap_resource_nt(context);
}

Outcome SurrogateChecks::nt_handle_wrong_adapter(Context&) {
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    D3d12Rig warp;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    if (!warp.open_warp(D3D_FEATURE_LEVEL_11_0, error)) {
        return Context::skip("there is no second adapter: " + error);
    }
    constexpr UINT64 kSize = 65536;
    HRESULT result = S_OK;
    ID3D12Resource* buffer = D3d12Tools::committed(rig, D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_SHARED, D3d12Tools::buffer_desc(kSize), D3D12_RESOURCE_STATE_COMMON, nullptr, &result);
    HANDLE handle = nullptr;
    if (buffer == nullptr || FAILED(rig.device()->CreateSharedHandle(buffer, nullptr, GENERIC_ALL, nullptr, &handle))) {
        return Context::skip("resource sharing is unsupported: " + D3d12Tools::hr(result));
    }
    std::vector<std::uint8_t> data(kSize);
    Pattern::fill(99, 0, data.data(), data.size());
    if (!D3d12Tools::write_buffer(rig, buffer, D3D12_RESOURCE_STATE_COMMON, 0, data.data(), data.size(), error)) {
        return Outcome::fail(error);
    }
    Com<ID3D12Resource> opened;
    const HRESULT status = warp.device()->OpenSharedHandle(handle, __uuidof(ID3D12Resource), opened.put_void());
    ::CloseHandle(handle);
    if (FAILED(status)) {
        if (!D3d12Tools::in(status, {E_INVALIDARG, DXGI_ERROR_UNSUPPORTED, E_ACCESSDENIED, DXGI_ERROR_INVALID_CALL, E_FAIL, E_NOTIMPL})) {
            return Outcome::fail("opening on another adapter failed with the undocumented " + D3d12Tools::hr(status));
        }
        return Outcome::ok();
    }
    D3d12Tools::Bytes back;
    if (!D3d12Tools::read_buffer(warp, opened.get(), D3D12_RESOURCE_STATE_COMMON, 0, kSize, back, error)) {
        return Outcome::ok();
    }
    return std::memcmp(back.data(), data.data(), data.size()) == 0 ? Outcome::ok() : Outcome::fail("the object opened on another adapter holds other content");
}

Outcome SurrogateChecks::share_requires_handle(Context& context) {
    return iso::HandleChecks::no_access_without_handle(context);
}

Outcome SurrogateChecks::monitored_fence_monotonic(Context&) {
    Fence12 fence;
    std::string error;
    if (!fence.open(error)) {
        return Outcome::fail(error);
    }
    std::atomic<bool> stop{false};
    std::atomic<int> regressions{0};
    std::vector<std::thread> observers;
    for (int i = 0; i < 4; ++i) {
        observers.emplace_back([&] {
            std::uint64_t last = 0;
            while (!stop.load()) {
                const std::uint64_t now = fence.fence->GetCompletedValue();
                if (now < last) {
                    regressions.fetch_add(1);
                }
                last = now;
            }
        });
    }
    D3d12Queue& queue = fence.rig.direct();
    std::uint64_t value = 0;
    for (int i = 0; i < 20000; ++i) {
        queue.queue()->Signal(fence.fence.get(), ++value);
        if (i % 7 == 0) {
            fence.fence->Signal(value + 1);
            ++value;
        }
    }
    queue.touch();
    HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    fence.fence->SetEventOnCompletion(value, event);
    const DWORD waited = ::WaitForSingleObject(event, 30000);
    ::CloseHandle(event);
    stop.store(true);
    for (std::thread& thread : observers) {
        thread.join();
    }
    Problems problems;
    if (waited != WAIT_OBJECT_0) {
        problems.add("the last signalled value was never reached");
    }
    if (regressions.load() != 0) {
        problems.add("the observed fence value decreased " + std::to_string(regressions.load()) + " times");
    }
    return problems.outcome();
}

Outcome SurrogateChecks::wait_releases_on_signal(Context&) {
    Fence12 fence;
    std::string error;
    if (!fence.open(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    for (int round = 0; round < 20; ++round) {
        const std::uint64_t target = static_cast<std::uint64_t>(round) * 10 + 10;
        HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        fence.fence->SetEventOnCompletion(target, event);
        for (std::uint64_t value = target - 9; value < target; ++value) {
            fence.fence->Signal(value);
            if (::WaitForSingleObject(event, 0) == WAIT_OBJECT_0) {
                problems.add("the wait for " + std::to_string(target) + " was released at " + std::to_string(value));
                break;
            }
        }
        const auto begin = Clock::now();
        fence.fence->Signal(target);
        if (::WaitForSingleObject(event, 2000) != WAIT_OBJECT_0) {
            problems.add("the wait for " + std::to_string(target) + " was not released after the signal");
        }
        if (ms_since(begin) > 1000) {
            problems.add("the release took " + std::to_string(ms_since(begin)) + " ms");
        }
        ::CloseHandle(event);
    }
    return problems.outcome();
}

Outcome SurrogateChecks::wait_timeout(Context& context) {
    Fence12 fence;
    std::string error;
    if (!fence.open(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    for (DWORD timeout : {20u, 100u, 400u}) {
        HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        fence.fence->SetEventOnCompletion(1000, event);
        const auto begin = Clock::now();
        const DWORD result = ::WaitForSingleObject(event, timeout);
        const double took = ms_since(begin);
        context.measure("wait_" + std::to_string(timeout) + "_ms", took);
        if (result != WAIT_TIMEOUT) {
            problems.add("a wait on an unsignalled value returned " + std::to_string(result));
        }
        if (took < timeout - 30.0 || took > timeout + 500.0) {
            problems.add("a wait of " + std::to_string(timeout) + " ms took " + std::to_string(took) + " ms");
        }
        ::CloseHandle(event);
    }
    return problems.outcome();
}

Outcome SurrogateChecks::shared_fence_across_processes(Context& context) {
    return sync::ProcessChecks::shared_fence_ping_pong(context);
}

Outcome SurrogateChecks::cpu_signal_visible_to_gpu(Context&) {
    Fence12 fence;
    std::string error;
    if (!fence.open(error)) {
        return Outcome::fail(error);
    }
    D3d12Rig& rig = fence.rig;
    constexpr UINT64 kSize = 65536;
    ID3D12Resource* upload = D3d12Tools::upload(rig, kSize);
    ID3D12Resource* readback = D3d12Tools::readback(rig, kSize);
    void* pointer = nullptr;
    const D3D12_RANGE none{0, 0};
    if (upload == nullptr || readback == nullptr || FAILED(upload->Map(0, &none, &pointer))) {
        return Outcome::fail("preparing the buffers failed");
    }
    Problems problems;
    for (int round = 0; round < 20; ++round) {
        std::memset(pointer, 0, kSize);
        rig.direct().gpu_wait_fence(fence.fence.get(), static_cast<std::uint64_t>(round) + 1);
        std::uint64_t done = 0;
        ID3D12CommandList* list = rig.direct().record([&](ID3D12GraphicsCommandList* l) { l->CopyBufferRegion(readback, 0, upload, 0, kSize); }, error);
        if (list == nullptr) {
            return Outcome::fail(error);
        }
        done = rig.direct().execute(&list, 1);
        ::Sleep(20);
        if (rig.direct().reached(done)) {
            problems.add("round " + std::to_string(round) + ": GPU work queued behind a wait ran before the CPU signal");
        }
        std::vector<std::uint8_t> data(kSize);
        Pattern::fill(700 + static_cast<std::uint64_t>(round), 0, data.data(), data.size());
        std::memcpy(pointer, data.data(), data.size());
        fence.fence->Signal(static_cast<std::uint64_t>(round) + 1);
        D3d12Tools::Bytes back;
        if (!rig.direct().wait(done, error) || !D3d12Tools::map_bytes(readback, 0, kSize, back, error)) {
            problems.add("round " + std::to_string(round) + ": " + error);
            break;
        }
        if (std::memcmp(back.data(), data.data(), data.size()) != 0) {
            problems.add("round " + std::to_string(round) + ": work after the wait did not see memory written before the CPU signal");
            break;
        }
    }
    return problems.outcome();
}

Outcome SurrogateChecks::tdr_restores_service(Context& context) {
    return err::RecoveryChecks::tdr_recovers(context);
}

Outcome SurrogateChecks::tdr_other_adapter_unaffected(Context& context) {
    if (ChildMode::active()) {
        return iso_child(context);
    }
    D3d12Rig warp;
    std::string error;
    if (!warp.open_warp(D3D_FEATURE_LEVEL_11_0, error)) {
        return Context::skip("there is no second adapter: " + error);
    }
    iso::Fleet hang;
    hang.start(context.id(), "spinforever-d3d12", "spinforever-d3d12", iso::Scene::seconds(6), 120);
    Problems problems;
    ID3D12Resource* buffer = D3d12Tools::device_buffer(warp, 65536, D3D12_RESOURCE_STATE_COMMON);
    const auto begin = Clock::now();
    int rounds = 0;
    while (ms_since(begin) < 9000 && buffer != nullptr) {
        std::vector<std::uint8_t> data(65536);
        Pattern::fill(static_cast<std::uint64_t>(rounds), 0, data.data(), data.size());
        D3d12Tools::Bytes back;
        if (!D3d12Tools::write_buffer(warp, buffer, D3D12_RESOURCE_STATE_COMMON, 0, data.data(), data.size(), error) ||
            !D3d12Tools::read_buffer(warp, buffer, D3D12_RESOURCE_STATE_COMMON, 0, data.size(), back, error) || std::memcmp(back.data(), data.data(), data.size()) != 0) {
            problems.add("round " + std::to_string(rounds) + " on the other adapter failed: " + error);
            break;
        }
        ++rounds;
    }
    hang.join();
    context.measure("rounds", rounds);
    return problems.outcome();
}

Outcome SurrogateChecks::power_transition_contents(Context& context) {
    return life::LifeChecks::sleep_resume_contents(context);
}

Outcome SurrogateChecks::query_status_after_reset(Context& context) {
    if (ChildMode::active()) {
        return iso_child(context);
    }
    iso::Fleet hang;
    hang.start(context.id(), "spinforever-d3d11", "spinforever-d3d11", iso::Scene::seconds(8), 120);
    hang.join();
    Problems problems;
    KmtApi api;
    AvAdapter adapter;
    std::string error;
    const auto begin = Clock::now();
    if (!TargetAdapter::pick(adapter, error) || !api.load(error)) {
        return Outcome::fail(error);
    }
    std::vector<KmtAdapter> adapters;
    KmtSegmentSizes sizes;
    if (!api.enum_adapters(adapters, error) || adapters.empty()) {
        problems.add("D3DKMTEnumAdapters2 fails after the reset: " + error);
    }
    if (!api.segment_sizes(adapter.luid, sizes)) {
        problems.add("the segment size query fails after the reset");
    }
    D3d11Device device;
    if (!device.create(adapter.luid, error) || device.device->GetDeviceRemovedReason() != S_OK) {
        problems.add("a new device after the reset is not healthy: " + error);
    }
    context.measure("status_calls_ms", ms_since(begin));
    if (ms_since(begin) > 15000) {
        problems.add("the status calls took " + std::to_string(ms_since(begin)) + " ms after the reset");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::kmd
