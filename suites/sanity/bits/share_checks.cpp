#include "sanity/bits/share_checks.hpp"

#include "core/child_mode.hpp"
#include "core/text.hpp"
#include "support/api_policy.hpp"
#include "support/child_runs.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_tools.hpp"
#include "support/pattern.hpp"
#include "support/problems.hpp"
#include "support/shared_fence.hpp"
#include "support/target_adapter.hpp"

#include <d3d11_1.h>
#include <dxgi1_2.h>

#include <cstring>
#include <thread>

namespace lsw::suites::sanity::bits {

namespace {

constexpr UINT64 kSize = 1 << 20;
constexpr UINT kSide = 64;
constexpr int kRounds = 24;

std::uint32_t color_for(int round, bool parent) {
    return static_cast<std::uint32_t>(20 + round * 7 + (parent ? 0 : 90)) | (static_cast<std::uint32_t>(200 - round * 5) << 8) | (static_cast<std::uint32_t>(round * 9 + (parent ? 3 : 40)) << 16) | 0xff000000u;
}

void as_floats(std::uint32_t color, float out[4]) {
    out[0] = static_cast<float>(color & 255) / 255.0f;
    out[1] = static_cast<float>((color >> 8) & 255) / 255.0f;
    out[2] = static_cast<float>((color >> 16) & 255) / 255.0f;
    out[3] = 1.0f;
}

Outcome buffer_peer(Context& context, const std::string& name) {
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    HANDLE handle = nullptr;
    HRESULT result = rig.device()->OpenSharedHandleByName(Text::wide(name).c_str(), GENERIC_ALL, &handle);
    Com<ID3D12Resource> buffer;
    if (SUCCEEDED(result)) {
        result = rig.device()->OpenSharedHandle(handle, __uuidof(ID3D12Resource), buffer.put_void());
        ::CloseHandle(handle);
    }
    if (FAILED(result)) {
        return Outcome::fail("opening the other bitness's shared buffer failed: " + D3d12Tools::hr(result));
    }
    D3d12Tools::Bytes bytes;
    std::vector<std::uint8_t> expected(kSize);
    Pattern::fill(1001, 0, expected.data(), expected.size());
    if (!D3d12Tools::read_buffer(rig, buffer.get(), D3D12_RESOURCE_STATE_COMMON, 0, kSize, bytes, error)) {
        return Outcome::fail(error);
    }
    if (std::memcmp(bytes.data(), expected.data(), expected.size()) != 0) {
        return Outcome::fail("the contents seen through the other bitness differ from what was written");
    }
    Pattern::fill(1002, 0, expected.data(), expected.size());
    if (!D3d12Tools::write_buffer(rig, buffer.get(), D3D12_RESOURCE_STATE_COMMON, 0, expected.data(), expected.size(), error)) {
        return Outcome::fail(error);
    }
    context.measure("verified", 1);
    return Outcome::ok();
}

Outcome fence_peer(Context&, const std::string& name) {
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    Com<ID3D12Fence> fence;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error) || !SharedFence::open(rig, name, fence, error)) {
        return Outcome::fail(error);
    }
    for (std::uint64_t base = 0; base < 40; base += 2) {
        if (!SharedFence::reaches(fence.get(), base + 1, 60000)) {
            return Outcome::fail("never saw the other bitness signal " + std::to_string(base + 1));
        }
        fence->Signal(base + 2);
    }
    return Outcome::ok();
}

Outcome mutex_peer(Context& context, const std::string& name) {
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    Com<ID3D11Device1> device1;
    device.device->QueryInterface(__uuidof(ID3D11Device1), device1.put_void());
    Com<ID3D11Texture2D> texture;
    if (!device1 || FAILED(device1->OpenSharedResourceByName(Text::wide(name).c_str(), DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, __uuidof(ID3D11Texture2D), texture.put_void()))) {
        return Outcome::fail("opening the keyed shared texture by name failed");
    }
    Com<IDXGIKeyedMutex> mutex;
    texture->QueryInterface(__uuidof(IDXGIKeyedMutex), mutex.put_void());
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, texture.get());
    Com<ID3D11Texture2D> staging = D3d11Tools::texture(device, D3d11Tools::desc2d(kSide, kSide, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
    if (!mutex || !view || !staging) {
        return Outcome::fail("the keyed mutex peer objects cannot be created");
    }
    for (int round = 0; round < kRounds; ++round) {
        if (mutex->AcquireSync(1, 30000) != S_OK) {
            return Outcome::fail("round " + std::to_string(round) + ": AcquireSync(1) did not succeed");
        }
        device.context->CopyResource(staging.get(), texture.get());
        TexelImage image;
        if (!D3d11Tools::read_texture(device, staging.get(), 0, image, error)) {
            return Outcome::fail(error);
        }
        if (image.pixel(0, 0) != color_for(round, true) || image.pixel(kSide - 1, kSide - 1) != color_for(round, true)) {
            return Outcome::fail("round " + std::to_string(round) + ": the other bitness's frame was not visible after the hand-off");
        }
        float clear[4];
        as_floats(color_for(round, false), clear);
        device.context->ClearRenderTargetView(view.get(), clear);
        device.context->Flush();
        mutex->ReleaseSync(0);
    }
    context.measure("rounds", kRounds);
    return Outcome::ok();
}

Outcome peer_role(Context& context) {
    const std::string name = ChildMode::variable(L"LSW_SUITES_BITS_NAME");
    const std::string kind = ChildMode::variable(L"LSW_SUITES_BITS_KIND");
    context.measure("peer", kind.empty() ? std::string("mutex") : kind);
    if (kind == "buffer") {
        return buffer_peer(context, name);
    }
    if (kind == "fence") {
        return fence_peer(context, name);
    }
    return mutex_peer(context, name);
}

}  // namespace

Outcome ShareChecks::cross_bitness_nt_handle(Context& context) {
    if (ChildMode::active()) {
        return peer_role(context);
    }
    if (ApiPolicy::is_skipped("d3d12")) {
        return Context::skip("d3d12 excluded by LSW_SUITES_SKIP_APIS");
    }
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    HRESULT result = S_OK;
    ID3D12Resource* buffer = D3d12Tools::committed(rig, D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_SHARED, D3d12Tools::buffer_desc(kSize), D3D12_RESOURCE_STATE_COMMON, nullptr, &result);
    const std::string name = "Local\\lsw-suites-bits-buffer-" + std::to_string(::GetCurrentProcessId());
    HANDLE handle = nullptr;
    if (buffer == nullptr || FAILED(rig.device()->CreateSharedHandle(buffer, nullptr, GENERIC_ALL, Text::wide(name).c_str(), &handle))) {
        return Context::skip("resource sharing is unsupported: " + D3d12Tools::hr(result));
    }
    std::vector<std::uint8_t> data(kSize);
    Pattern::fill(1001, 0, data.data(), data.size());
    if (!D3d12Tools::write_buffer(rig, buffer, D3D12_RESOURCE_STATE_COMMON, 0, data.data(), data.size(), error)) {
        return Outcome::fail(error);
    }
    ChildRunOptions options;
    options.timeout_seconds = 120;
    options.env = {{"LSW_SUITES_BITS_NAME", name}, {"LSW_SUITES_BITS_KIND", "buffer"}};
    CheckResult other;
    if (!ChildRuns::run_sibling(context.id(), options, other, error)) {
        ::CloseHandle(handle);
        return Context::skip(error);
    }
    ::CloseHandle(handle);
    if (other.status != Status::Ok) {
        return Outcome::fail("the other bitness: " + other.reason);
    }
    D3d12Tools::Bytes back;
    Pattern::fill(1002, 0, data.data(), data.size());
    if (!D3d12Tools::read_buffer(rig, buffer, D3D12_RESOURCE_STATE_COMMON, 0, kSize, back, error) || std::memcmp(back.data(), data.data(), data.size()) != 0) {
        return Outcome::fail("what the other bitness wrote is not visible to the owner: " + error);
    }
    return Outcome::ok();
}

Outcome ShareChecks::cross_bitness_fence(Context& context) {
    if (ChildMode::active()) {
        return peer_role(context);
    }
    if (ApiPolicy::is_skipped("d3d12")) {
        return Context::skip("d3d12 excluded by LSW_SUITES_SKIP_APIS");
    }
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    Com<ID3D12Fence> fence;
    HANDLE handle = nullptr;
    const std::string name = "Local\\lsw-suites-bits-fence-" + std::to_string(::GetCurrentProcessId());
    if (!SharedFence::create(rig, name, fence, handle, error)) {
        return Context::skip("shared fences are unsupported: " + error);
    }
    ChildRunOptions options;
    options.timeout_seconds = 180;
    options.env = {{"LSW_SUITES_BITS_NAME", name}, {"LSW_SUITES_BITS_KIND", "fence"}};
    CheckResult other;
    std::string peer_error;
    bool ran = false;
    std::thread runner([&] { ran = ChildRuns::run_sibling(context.id(), options, other, peer_error); });
    Problems problems;
    for (std::uint64_t base = 0; base < 40; base += 2) {
        fence->Signal(base + 1);
        if (!SharedFence::reaches(fence.get(), base + 2, 60000)) {
            problems.add("the other bitness never answered " + std::to_string(base + 1));
            fence->Signal(1000);
            break;
        }
    }
    runner.join();
    ::CloseHandle(handle);
    if (!ran) {
        return Context::skip(peer_error);
    }
    if (other.status != Status::Ok) {
        problems.add("the other bitness: " + other.reason);
    }
    return problems.outcome();
}

Outcome ShareChecks::cross_bitness_dxgi_keyed_mutex(Context& context) {
    if (ChildMode::active()) {
        return peer_role(context);
    }
    if (ApiPolicy::is_skipped("d3d11")) {
        return Context::skip("d3d11 excluded by LSW_SUITES_SKIP_APIS");
    }
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(kSide, kSide, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    Com<ID3D11Texture2D> texture = D3d11Tools::texture(device, desc);
    Com<IDXGIResource1> resource;
    Com<IDXGIKeyedMutex> mutex;
    if (texture) {
        texture->QueryInterface(__uuidof(IDXGIResource1), resource.put_void());
        texture->QueryInterface(__uuidof(IDXGIKeyedMutex), mutex.put_void());
    }
    const std::string name = "Local\\lsw-suites-bits-mutex-" + std::to_string(::GetCurrentProcessId());
    HANDLE handle = nullptr;
    if (!resource || !mutex || FAILED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, Text::wide(name).c_str(), &handle))) {
        return Context::skip("keyed-mutex NT-handle sharing is unsupported");
    }
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, texture.get());
    Com<ID3D11Texture2D> staging = D3d11Tools::texture(device, D3d11Tools::desc2d(kSide, kSide, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
    if (!view || !staging) {
        return Outcome::fail("creating the views failed");
    }
    ChildRunOptions options;
    options.timeout_seconds = 180;
    options.env = {{"LSW_SUITES_BITS_NAME", name}, {"LSW_SUITES_BITS_KIND", "mutex"}};
    CheckResult other;
    std::string peer_error;
    bool ran = false;
    std::thread runner([&] { ran = ChildRuns::run_sibling(context.id(), options, other, peer_error); });
    Problems problems;
    for (int round = 0; round < kRounds && problems.empty(); ++round) {
        if (mutex->AcquireSync(0, 30000) != S_OK) {
            problems.add("round " + std::to_string(round) + ": AcquireSync(0) did not succeed");
            break;
        }
        if (round > 0) {
            device.context->CopyResource(staging.get(), texture.get());
            TexelImage image;
            if (!D3d11Tools::read_texture(device, staging.get(), 0, image, error) || image.pixel(0, 0) != color_for(round - 1, false)) {
                problems.add("round " + std::to_string(round) + ": the other bitness's frame was not visible after the hand-off");
            }
        }
        float clear[4];
        as_floats(color_for(round, true), clear);
        device.context->ClearRenderTargetView(view.get(), clear);
        device.context->Flush();
        mutex->ReleaseSync(1);
    }
    if (!problems.empty()) {
        mutex->ReleaseSync(1);
    }
    runner.join();
    ::CloseHandle(handle);
    if (!ran) {
        return Context::skip(peer_error);
    }
    if (other.status != Status::Ok) {
        problems.add("the other bitness: " + other.reason);
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::bits
