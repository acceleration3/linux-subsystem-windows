#include "sanity/iso/loads.hpp"

#include "core/child_mode.hpp"
#include "sanity/err/spin_device.hpp"
#include "sanity/sync/sync_device.hpp"
#include "support/backend_harness.hpp"
#include "support/d3d12_rig.hpp"
#include "support/pattern.hpp"
#include "support/robust_child.hpp"
#include "support/target_adapter.hpp"

#include <d3d12.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <vector>

namespace lsw::suites::sanity::iso {

namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

Outcome open_backend(const std::string& api, AvAdapter& adapter, std::unique_ptr<GpuBackend>& backend) {
    std::string error;
    backend = BackendHarness::open(api, adapter, error);
    return backend == nullptr ? Context::skip(error) : Outcome::ok();
}

Outcome verified(Context& context, const std::string& api) {
    AvAdapter adapter;
    std::unique_ptr<GpuBackend> backend;
    const Outcome opened = open_backend(api, adapter, backend);
    if (opened.status() != Status::Ok) {
        return opened;
    }
    RobustChild::ready();
    const auto end = Clock::now() + std::chrono::milliseconds(static_cast<long long>(Loads::seconds() * 1000));
    std::uint64_t seed = 900000;
    long long rounds = 0;
    while (Clock::now() < end) {
        for (std::uint64_t size : {65536ull, 1ull << 20, 4ull << 20}) {
            AllocStatus status = AllocStatus::Ok;
            std::string error;
            if (!BackendHarness::roundtrip(*backend, size, ++seed, status, context, error)) {
                return Outcome::fail(api + " round " + std::to_string(rounds) + " size " + std::to_string(size) + ": " + error);
            }
            ++rounds;
        }
    }
    context.measure("rounds", rounds);
    return Outcome::ok();
}

Outcome latency(Context& context, const std::string& api) {
    AvAdapter adapter;
    std::unique_ptr<GpuBackend> backend;
    const Outcome opened = open_backend(api, adapter, backend);
    if (opened.status() != Status::Ok) {
        return opened;
    }
    AllocStatus status = AllocStatus::Ok;
    std::string error;
    std::unique_ptr<GpuBuffer> buffer = backend->create(65536, BufferKind::Device, status, error);
    if (buffer == nullptr) {
        return Outcome::fail(api + ": " + error);
    }
    RobustChild::ready();
    std::vector<double> times;
    const auto end = Clock::now() + std::chrono::milliseconds(static_cast<long long>(Loads::seconds() * 1000));
    std::uint64_t seed = 1;
    while (Clock::now() < end) {
        std::vector<std::uint8_t> expected(65536);
        std::vector<std::uint8_t> actual(65536);
        Pattern::fill(++seed, 0, expected.data(), expected.size());
        const auto begin = Clock::now();
        if (!backend->upload(*buffer, 0, expected.data(), expected.size(), error) || !backend->finish(error) ||
            !backend->download(*buffer, 0, actual.data(), actual.size(), error)) {
            return Outcome::fail(api + ": roundtrip " + std::to_string(times.size()) + " failed: " + error);
        }
        times.push_back(elapsed_ms(begin));
        if (!BackendHarness::verify(context, api + " latency", expected.data(), actual.data(), expected.size(), error)) {
            return Outcome::fail(error);
        }
        ::Sleep(5);
    }
    std::sort(times.begin(), times.end());
    context.measure("median_ms", times[times.size() / 2]);
    context.measure("max_ms", times.back());
    context.measure("count", static_cast<long long>(times.size()));
    return Outcome::ok();
}

Outcome holder(Context& context, const std::string& api) {
    AvAdapter adapter;
    std::unique_ptr<GpuBackend> backend;
    const Outcome opened = open_backend(api, adapter, backend);
    if (opened.status() != Status::Ok) {
        return opened;
    }
    constexpr std::uint64_t kSize = 16ull << 20;
    AllocStatus status = AllocStatus::Ok;
    std::string error;
    std::unique_ptr<GpuBuffer> buffer = backend->create(kSize, BufferKind::Device, status, error);
    if (buffer == nullptr) {
        return Outcome::fail(api + ": " + error);
    }
    std::vector<std::uint8_t> expected(kSize);
    std::vector<std::uint8_t> actual(kSize);
    Pattern::fill(31337, 0, expected.data(), expected.size());
    if (!backend->upload(*buffer, 0, expected.data(), kSize, error) || !backend->finish(error)) {
        return Outcome::fail(api + ": " + error);
    }
    RobustChild::ready();
    const auto end = Clock::now() + std::chrono::milliseconds(static_cast<long long>(Loads::seconds() * 1000));
    long long checks = 0;
    while (Clock::now() < end) {
        if (!backend->download(*buffer, 0, actual.data(), kSize, error) || !BackendHarness::verify(context, api + " holder", expected.data(), actual.data(), kSize, error)) {
            return Outcome::fail(api + ": the held allocation failed after " + std::to_string(checks) + " checks: " + error);
        }
        ++checks;
        ::Sleep(50);
    }
    context.measure("checks", checks);
    return Outcome::ok();
}

Outcome hammer(Context& context, const std::string& api) {
    AvAdapter adapter;
    std::unique_ptr<GpuBackend> backend;
    const Outcome opened = open_backend(api, adapter, backend);
    if (opened.status() != Status::Ok) {
        return opened;
    }
    const std::uint64_t size = std::min<std::uint64_t>(64ull << 20, backend->max_allocation() != 0 ? backend->max_allocation() : 64ull << 20);
    AllocStatus status = AllocStatus::Ok;
    std::string error;
    std::unique_ptr<GpuBuffer> first = backend->create(size, BufferKind::Device, status, error);
    std::unique_ptr<GpuBuffer> second = first != nullptr ? backend->create(size, BufferKind::Device, status, error) : nullptr;
    if (second == nullptr) {
        return Outcome::fail(api + ": " + error);
    }
    if (!BackendHarness::fill_and_verify(*backend, *first, 4242, std::min<std::uint64_t>(size, 1 << 20), context, error)) {
        return Outcome::fail(api + ": " + error);
    }
    RobustChild::ready();
    const auto end = Clock::now() + std::chrono::seconds(60);
    while (Clock::now() < end) {
        if (!backend->copy(*first, 0, *second, 0, size, error) || !backend->copy(*second, 0, *first, 0, size, error)) {
            return Outcome::fail(api + ": a copy failed: " + error);
        }
    }
    return Outcome::ok();
}

Outcome spin_forever(Context& context, const std::string& api) {
    err::SpinDevice* device = err::SpinDevice::make(api).release();
    std::string error;
    if (device == nullptr || !device->open(true, error)) {
        return Context::skip(api + " is unavailable: " + error);
    }
    if (!device->submit(1, error)) {
        return Outcome::fail("submit: " + error);
    }
    RobustChild::ready();
    std::string detail;
    const err::SpinState state = device->poll(static_cast<int>(Loads::seconds() * 1000), detail);
    context.measure("state", static_cast<int>(state));
    context.measure("detail", detail);
    return state == err::SpinState::Error ? Outcome::fail("the spinning process saw an undocumented failure: " + detail) : Outcome::ok();
}

Outcome spin_loop(Context& context, const std::string& api) {
    std::unique_ptr<err::SpinDevice> device = err::SpinDevice::make(api);
    std::string error;
    if (device == nullptr || !device->open(false, error)) {
        return Context::skip(api + " is unavailable: " + error);
    }
    std::uint32_t outer = 0;
    double took = 0;
    if (!err::SpinDevice::calibrate(*device, 250.0, outer, took, error)) {
        return Outcome::fail(error);
    }
    RobustChild::ready();
    const auto end = Clock::now() + std::chrono::milliseconds(static_cast<long long>(Loads::seconds() * 1000));
    long long jobs = 0;
    while (Clock::now() < end) {
        std::string detail;
        if (!device->submit(outer, error) || device->poll(60000, detail) != err::SpinState::Done) {
            return Outcome::fail("the saturating loop stopped after " + std::to_string(jobs) + " jobs: " + error + detail);
        }
        ++jobs;
    }
    context.measure("jobs", jobs);
    return Outcome::ok();
}

Outcome exhaust(Context& context, const std::string& api) {
    AvAdapter adapter;
    std::unique_ptr<GpuBackend> backend;
    const Outcome opened = open_backend(api, adapter, backend);
    if (opened.status() != Status::Ok) {
        return opened;
    }
    std::vector<std::unique_ptr<GpuBuffer>> held;
    std::uint64_t chunk = 256ull << 20;
    if (backend->max_allocation() != 0) {
        chunk = std::min(chunk, backend->max_allocation());
    }
    std::uint64_t total = 0;
    for (;;) {
        AllocStatus status = AllocStatus::Ok;
        std::string detail;
        std::unique_ptr<GpuBuffer> next = backend->create(chunk, BufferKind::Device, status, detail);
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
    context.measure("held_bytes", static_cast<long long>(total));
    RobustChild::ready();
    ::Sleep(static_cast<DWORD>(Loads::seconds() * 1000));
    return Outcome::ok();
}

Outcome upload_hog(Context& context, const std::string& api) {
    AvAdapter adapter;
    std::unique_ptr<GpuBackend> backend;
    const Outcome opened = open_backend(api, adapter, backend);
    if (opened.status() != Status::Ok) {
        return opened;
    }
    std::uint64_t size = 512ull << 20;
    AllocStatus status = AllocStatus::Ok;
    std::string error;
    std::unique_ptr<GpuBuffer> buffer;
    while (size >= (16ull << 20) && (buffer = backend->create(size, BufferKind::Device, status, error)) == nullptr) {
        size /= 2;
    }
    if (buffer == nullptr) {
        return Outcome::fail(api + ": " + error);
    }
    std::vector<std::uint8_t> chunk(static_cast<std::size_t>(16ull << 20));
    Pattern::fill(5, 0, chunk.data(), chunk.size());
    RobustChild::ready();
    const auto end = Clock::now() + std::chrono::milliseconds(static_cast<long long>(Loads::seconds() * 1000));
    long long passes = 0;
    while (Clock::now() < end) {
        for (std::uint64_t at = 0; at < size; at += chunk.size()) {
            if (!backend->upload(*buffer, at, chunk.data(), std::min<std::uint64_t>(chunk.size(), size - at), error)) {
                return Outcome::fail(api + ": upload failed: " + error);
            }
        }
        if (!backend->finish(error)) {
            return Outcome::fail(api + ": " + error);
        }
        ++passes;
    }
    context.measure("passes", passes);
    context.measure("buffer_bytes", static_cast<long long>(size));
    return Outcome::ok();
}

Outcome out_of_bounds(Context& context, const std::string& api) {
    std::unique_ptr<sync::SyncDevice> device = sync::SyncDevice::make(api);
    std::string error;
    if (device == nullptr || !device->open(error)) {
        return Context::skip(api + " is unavailable: " + error);
    }
    RobustChild::ready();
    const std::uint32_t slots[] = {sync::SyncDevice::kSlots, 1u << 12, 1u << 16, 1u << 20, 1u << 24, 1u << 28, 0x3fffffffu, 0x7fffffffu, 0xfffffff0u};
    int issued = 0;
    for (int round = 0; round < 8; ++round) {
        for (std::uint32_t slot : slots) {
            std::string detail;
            if (!device->set(slot, 0xBADBAD00u + slot, error) || !device->signal(error)) {
                context.measure("stopped_after", issued);
                return Outcome::ok();
            }
            ++issued;
            if (device->wait(5000, detail) != sync::SyncState::Done) {
                context.measure("stopped_after", issued);
                context.measure("detail", detail);
                return Outcome::ok();
            }
        }
    }
    context.measure("issued", issued);
    return Outcome::ok();
}

Outcome remove_device(Context& context, const std::string&) {
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Context::skip("D3D12 is unavailable: " + error);
    }
    Com<ID3D12Device5> device5 = rig.as<ID3D12Device5>();
    if (!device5) {
        return Context::skip("ID3D12Device5::RemoveDevice is not exposed");
    }
    RobustChild::ready();
    device5->RemoveDevice();
    const HRESULT reason = rig.removed_reason();
    context.measure("removed_reason", static_cast<long long>(static_cast<std::uint32_t>(reason)));
    if (reason == S_OK) {
        return Outcome::fail("GetDeviceRemovedReason is S_OK after RemoveDevice");
    }
    return Outcome::ok();
}

}  // namespace

double Loads::seconds() {
    const std::string text = ChildMode::variable(L"LSW_SUITES_ISO_SECONDS");
    const double value = text.empty() ? 10.0 : std::atof(text.c_str());
    return value > 0 ? value : 10.0;
}

Outcome Loads::role(Context& context, const std::string& role_name) {
    const std::size_t dash = role_name.find('-');
    const std::string kind = dash == std::string::npos ? role_name : role_name.substr(0, dash);
    const std::string api = dash == std::string::npos ? std::string() : role_name.substr(dash + 1);
    if (kind == "verified") {
        return verified(context, api);
    }
    if (kind == "latency") {
        return latency(context, api);
    }
    if (kind == "holder") {
        return holder(context, api);
    }
    if (kind == "hammer") {
        return hammer(context, api);
    }
    if (kind == "spinforever") {
        return spin_forever(context, api);
    }
    if (kind == "spinloop") {
        return spin_loop(context, api);
    }
    if (kind == "exhaust") {
        return exhaust(context, api);
    }
    if (kind == "uploadhog") {
        return upload_hog(context, api);
    }
    if (kind == "oob") {
        return out_of_bounds(context, api);
    }
    if (kind == "remove") {
        return remove_device(context, api);
    }
    return Outcome::fail("unknown role " + role_name);
}

}  // namespace lsw::suites::sanity::iso
