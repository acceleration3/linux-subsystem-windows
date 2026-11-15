#include "support/stress_load.hpp"

#include "core/child_mode.hpp"
#include "core/text.hpp"
#include "support/api_loop.hpp"
#include "support/d3d11_backend.hpp"
#include "support/opencl_backend.hpp"
#include "support/pattern.hpp"
#include "support/process_sampler.hpp"
#include "support/target_adapter.hpp"

#include <chrono>

namespace lsw::suites {

Outcome StressLoad::roundtrips(Context& context, double seconds, const std::vector<std::uint64_t>& sizes,
                               StressResult* result) {
    Problems problems;
    const auto begin = std::chrono::steady_clock::now();
    std::uint64_t bytes = 0;
    const std::vector<std::string> apis = GpuBackends::names();
    ApiLoop::run(problems, apis, [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        const auto end = begin + std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0 / static_cast<double>(apis.size())));
        const auto api_begin = std::chrono::steady_clock::now();
        const auto stop = api_begin + (end - begin);
        std::uint64_t seed = 70000;
        while (std::chrono::steady_clock::now() < stop) {
            for (std::uint64_t size : sizes) {
                AllocStatus status = AllocStatus::Ok;
                std::string error;
                if (!BackendHarness::roundtrip(backend, size, ++seed, status, context, error)) {
                    problems.add(api + " " + std::to_string(size) + ": " + error);
                    return;
                }
                if (status == AllocStatus::Ok) {
                    bytes += size * 2;
                }
            }
        }
    });
    if (result != nullptr) {
        result->bytes = bytes;
        result->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    }
    return problems.outcome();
}

Outcome StressLoad::render(Context&, double seconds) {
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 1920;
    desc.Height = 1080;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* target = nullptr;
    ID3D11Texture2D* copy = nullptr;
    ID3D11RenderTargetView* view = nullptr;
    if (FAILED(device.device->CreateTexture2D(&desc, nullptr, &target)) || FAILED(device.device->CreateTexture2D(&desc, nullptr, &copy)) ||
        FAILED(device.device->CreateRenderTargetView(target, nullptr, &view))) {
        return Outcome::fail("creating the render load targets failed");
    }
    const auto stop = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long long>(seconds * 1000));
    unsigned frame = 0;
    while (std::chrono::steady_clock::now() < stop) {
        const float color[4] = {static_cast<float>(frame % 7) / 7.0f, static_cast<float>(frame % 5) / 5.0f, 0.25f, 1.0f};
        device.context->ClearRenderTargetView(view, color);
        device.context->CopyResource(copy, target);
        if (++frame % 16 == 0 && !device.wait_idle(error)) {
            return Outcome::fail(error);
        }
    }
    static_cast<void>(device.wait_idle(error));
    view->Release();
    copy->Release();
    target->Release();
    return Outcome::ok();
}

Outcome StressLoad::compute(Context&, double seconds) {
    AvAdapter adapter;
    std::string error;
    OpenClBackend backend;
    if (!TargetAdapter::pick(adapter, error) || !backend.init(adapter.luid, error) || !backend.run_kernel_load(seconds, error)) {
        return Outcome::fail(error);
    }
    return Outcome::ok();
}

Outcome StressLoad::hog(Context& context, double seconds) {
    const std::uint64_t available = ProcessSampler::available_physical();
    const std::uint64_t target = available / 10 * 7;
    constexpr std::uint64_t kStep = 64ull << 20;
    std::vector<void*> blocks;
    std::uint64_t held = 0;
    while (held + kStep <= target) {
        void* block = ::VirtualAlloc(nullptr, kStep, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (block == nullptr) {
            break;
        }
        for (std::uint64_t at = 0; at < kStep; at += 4096) {
            static_cast<volatile char*>(block)[at] = 1;
        }
        blocks.push_back(block);
        held += kStep;
    }
    context.measure("hog.bytes", held);
    const std::string name = ChildMode::variable(L"LSW_SUITES_READY_EVENT");
    if (!name.empty()) {
        HANDLE ready = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, Text::wide(name).c_str());
        if (ready != nullptr) {
            ::SetEvent(ready);
            ::CloseHandle(ready);
        }
    }
    ::Sleep(static_cast<DWORD>(seconds * 1000));
    for (void* block : blocks) {
        ::VirtualFree(block, 0, MEM_RELEASE);
    }
    return Outcome::ok();
}

}  // namespace lsw::suites
