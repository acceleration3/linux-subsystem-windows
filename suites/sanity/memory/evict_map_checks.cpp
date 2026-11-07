#include "sanity/memory/evict_map_checks.hpp"

#include "core/child_mode.hpp"
#include "support/api_loop.hpp"
#include "support/api_policy.hpp"
#include "support/child_runs.hpp"
#include "support/d3d12_backend.hpp"
#include "support/dxgi_budget.hpp"
#include "support/memory_tools.hpp"
#include "support/pattern.hpp"
#include "support/process_sampler.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::sanity::memory {

namespace {

constexpr std::uint64_t kOriginal = 32ull << 20;
constexpr std::uint64_t kPressureChunk = 128ull << 20;

std::unique_ptr<GpuBuffer> make(GpuBackend& backend, std::uint64_t size, BufferKind kind, Problems& problems) {
    AllocStatus status = AllocStatus::Ok;
    std::string detail;
    std::unique_ptr<GpuBuffer> buffer = backend.create(size, kind, status, detail);
    if (buffer == nullptr) {
        problems.add(backend.name() + " create " + std::to_string(size >> 10) + " KiB: " + detail);
    }
    return buffer;
}

}  // namespace

Outcome EvictMapChecks::contents_survive_pressure(Context& context) {
    Problems problems;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        const std::uint64_t reported = backend.available_memory();
        std::vector<std::unique_ptr<GpuBuffer>> originals;
        for (std::uint64_t i = 0; i < 8; ++i) {
            std::unique_ptr<GpuBuffer> buffer = make(backend, kOriginal, BufferKind::Device, problems);
            std::string error;
            if (buffer == nullptr) {
                return;
            }
            std::vector<std::uint8_t> data(static_cast<std::size_t>(kOriginal));
            Pattern::fill(500 + i, 0, data.data(), data.size());
            if (!backend.upload(*buffer, 0, data.data(), kOriginal, error)) {
                problems.add(api + ": " + error);
                return;
            }
            originals.push_back(std::move(buffer));
        }
        std::vector<std::unique_ptr<GpuBuffer>> pressure;
        std::uint64_t held = 0;
        const std::uint64_t goal = std::min<std::uint64_t>(reported + reported / 4, 20ull << 30);
        std::string stop_reason = "reached the goal";
        while (held < goal) {
            AllocStatus status = AllocStatus::Ok;
            std::string detail;
            std::unique_ptr<GpuBuffer> buffer = backend.create(kPressureChunk, BufferKind::Device, status, detail);
            if (buffer == nullptr) {
                if (status == AllocStatus::Failed) {
                    problems.add(api + ": pressure allocation failed unexpectedly: " + detail);
                }
                stop_reason = "refused: " + detail;
                break;
            }
            std::string error;
            if (!MemoryTools::touch(backend, *buffer, 9000 + pressure.size(), error)) {
                problems.add(api + ": " + error);
                return;
            }
            held += kPressureChunk;
            pressure.push_back(std::move(buffer));
        }
        context.measure(api + ".pressure_mib", held >> 20);
        context.measure(api + ".pressure_stop", stop_reason);
        std::string error;
        static_cast<void>(backend.finish(error));
        std::vector<std::uint8_t> expected(static_cast<std::size_t>(kOriginal));
        std::vector<std::uint8_t> actual(expected.size());
        for (std::uint64_t i = 0; i < originals.size(); ++i) {
            Pattern::fill(500 + i, 0, expected.data(), expected.size());
            if (!backend.download(*originals[static_cast<std::size_t>(i)], 0, actual.data(), kOriginal, error) ||
                !BackendHarness::verify(context, api + " original " + std::to_string(i) + " after pressure", expected.data(), actual.data(),
                                        kOriginal, error)) {
                problems.add(error);
                return;
            }
        }
    });
    return problems.outcome();
}

Outcome EvictMapChecks::make_resident_semantics(Context& context) {
    AvAdapter adapter;
    std::string error;
    D3d12Backend backend;
    if (!TargetAdapter::pick(adapter, error) || !backend.init(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    AllocStatus status = AllocStatus::Ok;
    std::string detail;
    std::unique_ptr<GpuBuffer> buffer = backend.create(kOriginal, BufferKind::Device, status, detail);
    if (buffer == nullptr) {
        return Outcome::fail(detail);
    }
    std::vector<std::uint8_t> expected(static_cast<std::size_t>(kOriginal));
    std::vector<std::uint8_t> actual(expected.size());
    Pattern::fill(601, 0, expected.data(), expected.size());
    if (!backend.upload(*buffer, 0, expected.data(), kOriginal, error)) {
        return Outcome::fail(error);
    }
    for (int round = 0; round < 5; ++round) {
        if (!backend.evict(*buffer, error) || !backend.make_resident(*buffer, error) ||
            !backend.download(*buffer, 0, actual.data(), kOriginal, error) ||
            !BackendHarness::verify(context, "after evict and make resident, round " + std::to_string(round), expected.data(), actual.data(),
                                    kOriginal, error)) {
            problems.add(error);
            return problems.outcome();
        }
    }
    std::vector<std::unique_ptr<GpuBuffer>> many;
    const std::uint64_t budget = backend.available_memory();
    for (std::uint64_t held = 0; held < budget / 2; held += kPressureChunk) {
        std::unique_ptr<GpuBuffer> next = backend.create(kPressureChunk, BufferKind::Device, status, detail);
        if (next == nullptr) {
            break;
        }
        many.push_back(std::move(next));
    }
    for (std::unique_ptr<GpuBuffer>& item : many) {
        if (!backend.evict(*item, error)) {
            problems.add(error);
            return problems.outcome();
        }
    }
    std::size_t resident = 0;
    for (std::unique_ptr<GpuBuffer>& item : many) {
        if (backend.make_resident(*item, error)) {
            ++resident;
        } else if (error.find("0x8007000e") == std::string::npos) {
            problems.add("MakeResident failed with something other than E_OUTOFMEMORY: " + error);
            break;
        }
    }
    context.measure("made_resident", resident);
    context.measure("evicted", many.size());
    if (!backend.download(*buffer, 0, actual.data(), kOriginal, error) ||
        !BackendHarness::verify(context, "original after mass eviction", expected.data(), actual.data(), kOriginal, error)) {
        problems.add(error);
    }
    return problems.outcome();
}

Outcome EvictMapChecks::pointer_valid_until_unmap(Context& context) {
    constexpr std::uint64_t kSize = 4ull << 20;
    Problems problems;
    ApiLoop::run(problems, {"vulkan", "d3d12", "gl"}, [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        if (!backend.supports_map()) {
            context.note(api + ": persistent mappings are not available");
            return;
        }
        std::unique_ptr<GpuBuffer> upload = make(backend, kSize, BufferKind::Upload, problems);
        std::unique_ptr<GpuBuffer> device = make(backend, kSize, BufferKind::Device, problems);
        if (upload == nullptr || device == nullptr) {
            return;
        }
        void* pointer = nullptr;
        std::string error;
        if (!backend.map(*upload, &pointer, error)) {
            problems.add(api + ": " + error);
            return;
        }
        std::vector<std::unique_ptr<GpuBuffer>> pressure;
        const std::uint64_t goal = std::min<std::uint64_t>(backend.available_memory() / 4, 2ull << 30);
        std::vector<std::uint8_t> expected(static_cast<std::size_t>(kSize));
        std::vector<std::uint8_t> actual(expected.size());
        for (int round = 0; round < 4; ++round) {
            Pattern::fill(700 + static_cast<std::uint64_t>(round), 0, expected.data(), expected.size());
            std::memcpy(pointer, expected.data(), expected.size());
            if (!backend.copy(*upload, 0, *device, 0, kSize, error) || !backend.download(*device, 0, actual.data(), kSize, error) ||
                !BackendHarness::verify(context, api + " mapped write, round " + std::to_string(round), expected.data(), actual.data(), kSize,
                                        error)) {
                problems.add(error);
                return;
            }
            if (round == 1) {
                for (std::uint64_t held = 0; held < goal; held += kPressureChunk) {
                    AllocStatus status = AllocStatus::Ok;
                    std::string detail;
                    std::unique_ptr<GpuBuffer> extra = backend.create(kPressureChunk, BufferKind::Device, status, detail);
                    if (extra == nullptr || !MemoryTools::touch(backend, *extra, 77, detail)) {
                        break;
                    }
                    pressure.push_back(std::move(extra));
                }
            }
            void* again = nullptr;
            if (!backend.map(*upload, &again, error) || again != pointer) {
                problems.add(api + ": the mapped pointer changed while the mapping was held");
                return;
            }
        }
        if (std::memcmp(pointer, expected.data(), expected.size()) != 0) {
            problems.add(api + ": mapped memory changed under the held pointer");
        }
        static_cast<void>(backend.unmap(*upload, error));
    });
    return problems.outcome();
}

Outcome EvictMapChecks::unmap_flushes(Context& context) {
    constexpr std::uint64_t kSize = 1 << 20;
    Problems problems;
    ApiLoop::run(problems, {"d3d11", "d3d12", "vulkan", "opencl", "gl"}, [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        if (!backend.supports_map()) {
            context.note(api + ": mapping is not available");
            return;
        }
        std::unique_ptr<GpuBuffer> upload = make(backend, kSize, BufferKind::Upload, problems);
        std::unique_ptr<GpuBuffer> device = make(backend, kSize, BufferKind::Device, problems);
        std::unique_ptr<GpuBuffer> readback = make(backend, kSize, BufferKind::Readback, problems);
        if (upload == nullptr || device == nullptr || readback == nullptr) {
            return;
        }
        std::vector<std::uint8_t> expected(static_cast<std::size_t>(kSize));
        std::vector<std::uint8_t> actual(expected.size());
        Pattern::fill(801, 0, expected.data(), expected.size());
        void* pointer = nullptr;
        std::string error;
        if (!backend.map(*upload, &pointer, error)) {
            problems.add(api + ": " + error);
            return;
        }
        std::memcpy(pointer, expected.data(), expected.size());
        if (!backend.unmap(*upload, error) || !backend.copy(*upload, 0, *device, 0, kSize, error) ||
            !backend.download(*device, 0, actual.data(), kSize, error) ||
            !BackendHarness::verify(context, api + " write through a mapping", expected.data(), actual.data(), kSize, error)) {
            problems.add(error);
            return;
        }
        Pattern::fill(802, 0, expected.data(), expected.size());
        if (!backend.upload(*device, 0, expected.data(), kSize, error) || !backend.copy(*device, 0, *readback, 0, kSize, error) ||
            !backend.finish(error) || !backend.map(*readback, &pointer, error)) {
            problems.add(api + ": " + error);
            return;
        }
        std::memcpy(actual.data(), pointer, actual.size());
        static_cast<void>(backend.unmap(*readback, error));
        if (!BackendHarness::verify(context, api + " read of GPU writes through a mapping", expected.data(), actual.data(), kSize, error)) {
            problems.add(error);
        }
    });
    return problems.outcome();
}

Outcome EvictMapChecks::exit_with_live_mappings(Context& context) {
    AvAdapter adapter;
    std::string error;
    if (ChildMode::active()) {
        for (const std::string& api : ApiPolicy::filter({"vulkan", "d3d12", "opencl", "d3d11"})) {
            std::unique_ptr<GpuBackend> backend = BackendHarness::open(api, adapter, error);
            if (backend == nullptr) {
                return Outcome::fail(error);
            }
            AllocStatus status = AllocStatus::Ok;
            std::string detail;
            GpuBuffer* device = backend->create(64ull << 20, BufferKind::Device, status, detail).release();
            GpuBuffer* host = backend->create(64ull << 20, BufferKind::Upload, status, detail).release();
            void* pointer = nullptr;
            if (device == nullptr || host == nullptr || !backend->map(*host, &pointer, detail)) {
                return Outcome::fail(api + ": " + detail);
            }
            std::memset(pointer, 0x5A, 4096);
            for (int i = 0; i < 8; ++i) {
                if (!backend->copy(*host, 0, *device, 0, 64ull << 20, detail)) {
                    return Outcome::fail(api + ": " + detail);
                }
            }
            static_cast<void>(backend.release());
        }
        return Outcome::ok();
    }
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    const std::string own_api = ApiPolicy::first_enabled({"d3d12", "d3d11", "vulkan", "opencl"});
    if (own_api.empty()) {
        return Context::skip("every API of this row is excluded by LSW_SUITES_SKIP_APIS");
    }
    std::unique_ptr<GpuBackend> mine = BackendHarness::open(own_api, adapter, error);
    if (mine == nullptr) {
        return Outcome::fail(error);
    }
    const std::uint64_t before = mine->available_memory();
    ChildRunOptions options;
    options.timeout_seconds = 180;
    const CheckResult child = ChildRuns::run_self("sanity.mem.map.exit_with_live_mappings", options);
    Problems problems;
    if (child.status != Status::Ok) {
        problems.add("the exiting process failed: " + child.reason);
    }
    AllocStatus later = AllocStatus::Ok;
    if (!BackendHarness::roundtrip(*mine, 8 << 20, 902, later, context, error) || later != AllocStatus::Ok) {
        problems.add("this process's device fails after the other process exited with live mappings: " + error);
    }
    bool restored = false;
    std::uint64_t after = 0;
    for (int poll = 0; poll < 100 && !restored; ++poll) {
        after = mine->available_memory();
        restored = after + (256ull << 20) >= before;
        if (!restored) {
            ::Sleep(100);
        }
    }
    context.measure("available.before", before);
    context.measure("available.after", after);
    if (!restored) {
        problems.add("the memory of the exited process was not released");
    }
    return problems.outcome();
}

Outcome EvictMapChecks::large_map_many(Context& context) {
    constexpr int kBuffers = 64;
    constexpr std::uint64_t kSize = 64ull << 20;
    Problems problems;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        if (!backend.supports_map()) {
            return;
        }
        if (ProcessSampler::available_physical() < (kBuffers * kSize) / 2) {
            context.note(api + ": the guest has too little free memory to map 4 GiB");
        }
        std::vector<std::unique_ptr<GpuBuffer>> buffers;
        std::vector<void*> pointers;
        std::string stop = "all 64 mapped";
        for (int i = 0; i < kBuffers; ++i) {
            AllocStatus status = AllocStatus::Ok;
            std::string detail;
            std::unique_ptr<GpuBuffer> buffer = backend.create(kSize, BufferKind::Upload, status, detail);
            void* pointer = nullptr;
            if (buffer == nullptr) {
                if (status == AllocStatus::Failed) {
                    problems.add(api + ": creation " + std::to_string(i) + " failed unexpectedly: " + detail);
                }
                stop = "creation refused at " + std::to_string(i);
                break;
            }
            if (!backend.map(*buffer, &pointer, detail)) {
                stop = "mapping refused at " + std::to_string(i) + ": " + detail;
                break;
            }
            buffers.push_back(std::move(buffer));
            pointers.push_back(pointer);
        }
        context.measure(api + ".mapped", pointers.size());
        context.measure(api + ".stop", stop);
        for (std::size_t i = 0; i < pointers.size(); ++i) {
            auto* bytes = static_cast<std::uint8_t*>(pointers[i]);
            Pattern::fill(i, 0, bytes, 4096);
            Pattern::fill(i + 1000, 0, bytes + kSize - 4096, 4096);
        }
        std::vector<std::uint8_t> expected(4096);
        for (std::size_t i = 0; i < pointers.size(); ++i) {
            auto* bytes = static_cast<std::uint8_t*>(pointers[i]);
            Pattern::fill(i, 0, expected.data(), 4096);
            if (std::memcmp(bytes, expected.data(), 4096) != 0) {
                problems.add(api + ": mapping " + std::to_string(i) + " lost its head");
                break;
            }
            Pattern::fill(i + 1000, 0, expected.data(), 4096);
            if (std::memcmp(bytes + kSize - 4096, expected.data(), 4096) != 0) {
                problems.add(api + ": mapping " + std::to_string(i) + " lost its tail");
                break;
            }
        }
        std::string error;
        for (std::unique_ptr<GpuBuffer>& buffer : buffers) {
            static_cast<void>(backend.unmap(*buffer, error));
        }
    });
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::memory
