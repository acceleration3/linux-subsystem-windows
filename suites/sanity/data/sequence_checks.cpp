#include "sanity/data/sequence_checks.hpp"

#include "support/api_loop.hpp"
#include "support/compare.hpp"
#include "support/d3d11_backend.hpp"
#include "support/d3d12_backend.hpp"
#include "support/pattern.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>

namespace lsw::suites::sanity::data {

namespace {

std::unique_ptr<GpuBuffer> make(GpuBackend& backend, std::uint64_t size, Problems& problems,
                                BufferKind kind = BufferKind::Device) {
    AllocStatus status = AllocStatus::Ok;
    std::string detail;
    std::unique_ptr<GpuBuffer> buffer = backend.create(size, kind, status, detail);
    if (buffer == nullptr) {
        problems.add(backend.name() + " create " + std::to_string(size) + ": " + detail);
    }
    return buffer;
}

}  // namespace

Outcome SequenceChecks::back_to_back_10000(Context& context) {
    constexpr std::uint64_t kRegion = 64;
    constexpr std::uint64_t kCount = 10000;
    Problems problems;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        std::unique_ptr<GpuBuffer> buffer = make(backend, kRegion * kCount, problems);
        if (buffer == nullptr) {
            return;
        }
        std::vector<std::uint8_t> chunk(kRegion);
        std::string error;
        for (std::uint64_t i = 0; i < kCount; ++i) {
            Pattern::fill(5000 + i, 0, chunk.data(), kRegion);
            if (!backend.upload(*buffer, i * kRegion, chunk.data(), kRegion, error)) {
                problems.add(api + " upload " + std::to_string(i) + ": " + error);
                return;
            }
        }
        std::vector<std::uint8_t> actual(kRegion * kCount);
        if (!backend.download(*buffer, 0, actual.data(), actual.size(), error)) {
            problems.add(api + ": " + error);
            return;
        }
        for (std::uint64_t i = 0; i < kCount; ++i) {
            Pattern::fill(5000 + i, 0, chunk.data(), kRegion);
            if (std::memcmp(chunk.data(), actual.data() + i * kRegion, kRegion) != 0) {
                problems.add(api + ": region " + std::to_string(i) + " lost its own pattern");
                return;
            }
        }
    });
    return problems.outcome();
}

Outcome SequenceChecks::interleaved_small_large(Context& context) {
    constexpr std::uint64_t kLarge = 100ull << 20;
    Problems problems;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        AllocStatus status = AllocStatus::Ok;
        std::string detail;
        std::unique_ptr<GpuBuffer> large = backend.create(kLarge, BufferKind::Device, status, detail);
        if (large == nullptr) {
            if (status == AllocStatus::Failed) {
                problems.add(api + ": " + detail);
            } else {
                context.note(api + ": 100 MiB refused cleanly");
            }
            return;
        }
        std::unique_ptr<GpuBuffer> small = make(backend, 16, problems);
        if (small == nullptr) {
            return;
        }
        std::mt19937_64 random(42);
        std::vector<std::uint8_t> expected(kLarge);
        std::vector<std::uint8_t> actual(kLarge);
        for (int op = 0; op < 12; ++op) {
            const bool use_large = (random() & 1) != 0;
            const std::uint64_t size = use_large ? kLarge : 16;
            GpuBuffer& target = use_large ? *large : *small;
            Pattern::fill(7000 + static_cast<std::uint64_t>(op), 0, expected.data(), static_cast<std::size_t>(size));
            std::string error;
            if (!backend.upload(target, 0, expected.data(), size, error) || !backend.download(target, 0, actual.data(), size, error) ||
                !BackendHarness::verify(context, api + " op " + std::to_string(op), expected.data(), actual.data(), size, error)) {
                problems.add(error);
                return;
            }
        }
    });
    return problems.outcome();
}

Outcome SequenceChecks::overwrite_ordering(Context& context) {
    constexpr std::uint64_t kSize = 4096;
    Problems problems;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        std::unique_ptr<GpuBuffer> buffer = make(backend, kSize, problems);
        if (buffer == nullptr) {
            return;
        }
        std::vector<std::uint8_t> a(kSize);
        std::vector<std::uint8_t> b(kSize);
        std::vector<std::uint8_t> actual(kSize);
        Pattern::fill(1, 0, a.data(), kSize);
        Pattern::fill(2, 0, b.data(), kSize);
        std::string error;
        if (!backend.upload(*buffer, 0, a.data(), kSize, error) || !backend.upload(*buffer, 0, b.data(), kSize, error) ||
            !backend.download(*buffer, 0, actual.data(), kSize, error) ||
            !BackendHarness::verify(context, api + " two writes", b.data(), actual.data(), kSize, error)) {
            problems.add(error);
            return;
        }
        std::vector<std::uint8_t> first_read(kSize);
        for (int round = 0; round < 50; ++round) {
            if (!backend.upload(*buffer, 0, a.data(), kSize, error) || !backend.download(*buffer, 0, first_read.data(), kSize, error) ||
                !backend.upload(*buffer, 0, b.data(), kSize, error) || !backend.download(*buffer, 0, actual.data(), kSize, error) ||
                !BackendHarness::verify(context, api + " read between writes (first)", a.data(), first_read.data(), kSize, error) ||
                !BackendHarness::verify(context, api + " read between writes (second)", b.data(), actual.data(), kSize, error)) {
                problems.add(error);
                return;
            }
        }
    });
    return problems.outcome();
}

Outcome SequenceChecks::read_after_write_same_context(Context& context) {
    std::vector<std::uint64_t> sizes;
    for (std::uint64_t size : BackendHarness::boundary_sizes()) {
        if (size <= (4ull << 20)) {
            sizes.push_back(size);
        }
    }
    Problems problems;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        std::uint64_t seed = 900;
        for (std::uint64_t size : sizes) {
            std::unique_ptr<GpuBuffer> first = make(backend, size, problems);
            std::unique_ptr<GpuBuffer> second = make(backend, size, problems);
            if (first == nullptr || second == nullptr) {
                return;
            }
            std::vector<std::uint8_t> expected(static_cast<std::size_t>(size));
            std::vector<std::uint8_t> actual(expected.size(), 0x5A);
            Pattern::fill(++seed, 0, expected.data(), expected.size());
            std::string error;
            if (!backend.upload(*first, 0, expected.data(), size, error) || !backend.download(*first, 0, actual.data(), size, error) ||
                !BackendHarness::verify(context, api + " direct " + std::to_string(size), expected.data(), actual.data(), size, error)) {
                problems.add(error);
                return;
            }
            std::fill(actual.begin(), actual.end(), 0x5A);
            if (!backend.upload(*first, 0, expected.data(), size, error) || !backend.copy(*first, 0, *second, 0, size, error) ||
                !backend.download(*second, 0, actual.data(), size, error) ||
                !BackendHarness::verify(context, api + " via copy " + std::to_string(size), expected.data(), actual.data(), size, error)) {
                problems.add(error);
                return;
            }
        }
    });
    return problems.outcome();
}

Outcome SequenceChecks::many_contexts(Context& context) {
    constexpr int kContexts = 64;
    constexpr std::uint64_t kRegion = 64 * 1024;
    Problems problems;
    std::mutex problems_mutex;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        std::unique_ptr<GpuBuffer> shared_buffer = make(backend, kRegion * kContexts, problems);
        if (shared_buffer == nullptr) {
            return;
        }
        std::vector<std::unique_ptr<GpuBackend>> contexts;
        for (int i = 0; i < kContexts; ++i) {
            std::unique_ptr<GpuBackend> next = backend.new_context();
            if (next == nullptr) {
                problems.add(api + ": creating context " + std::to_string(i) + " failed");
                return;
            }
            contexts.push_back(std::move(next));
        }
        std::vector<std::thread> threads;
        for (int i = 0; i < kContexts; ++i) {
            threads.emplace_back([&, i] {
                GpuBackend& ctx = *contexts[static_cast<std::size_t>(i)];
                AllocStatus status = AllocStatus::Ok;
                std::string detail;
                std::unique_ptr<GpuBuffer> own = ctx.create(kRegion, BufferKind::Device, status, detail);
                std::vector<std::uint8_t> expected(kRegion);
                std::vector<std::uint8_t> actual(kRegion);
                Pattern::fill(10000 + static_cast<std::uint64_t>(i), 0, expected.data(), kRegion);
                std::string error;
                bool ok = own != nullptr;
                for (int round = 0; ok && round < 4; ++round) {
                    ok = ctx.upload(*own, 0, expected.data(), kRegion, error) && ctx.finish(error) &&
                         ctx.download(*own, 0, actual.data(), kRegion, error) &&
                         BackendHarness::verify(context, api + " context " + std::to_string(i), expected.data(), actual.data(), kRegion, error);
                }
                if (!ok) {
                    std::lock_guard lock(problems_mutex);
                    problems.add(api + " context " + std::to_string(i) + ": " + (own == nullptr ? detail : error));
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
    });
    return problems.outcome();
}

Outcome SequenceChecks::many_devices_one_process(Context& context) {
    constexpr int kDevices = 16;
    constexpr std::uint64_t kSize = 1 << 20;
    Problems problems;
    std::mutex problems_mutex;
    for (const std::string& api : GpuBackends::names()) {
        std::vector<std::unique_ptr<GpuBackend>> backends;
        std::vector<std::unique_ptr<GpuBuffer>> buffers;
        bool ok = true;
        for (int i = 0; i < kDevices && ok; ++i) {
            AvAdapter adapter;
            std::string error;
            std::unique_ptr<GpuBackend> backend = BackendHarness::open(api, adapter, error);
            if (backend == nullptr) {
                problems.add("device " + std::to_string(i) + ": " + error);
                ok = false;
                break;
            }
            buffers.push_back(make(*backend, kSize, problems));
            ok = buffers.back() != nullptr;
            backends.push_back(std::move(backend));
        }
        if (!ok) {
            continue;
        }
        std::vector<std::thread> threads;
        for (int i = 0; i < kDevices; ++i) {
            threads.emplace_back([&, i] {
                std::vector<std::uint8_t> expected(kSize);
                Pattern::fill(20000 + static_cast<std::uint64_t>(i), 0, expected.data(), kSize);
                std::string error;
                std::vector<std::uint8_t> actual(kSize);
                const std::size_t at = static_cast<std::size_t>(i);
                const bool good = backends[at]->upload(*buffers[at], 0, expected.data(), kSize, error) &&
                                  backends[at]->download(*buffers[at], 0, actual.data(), kSize, error) &&
                                  BackendHarness::verify(context, api + " device " + std::to_string(i), expected.data(), actual.data(), kSize, error);
                if (!good) {
                    std::lock_guard lock(problems_mutex);
                    problems.add(api + " device " + std::to_string(i) + ": " + error);
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
    }
    return problems.outcome();
}

Outcome SequenceChecks::mixed_api_chain(Context& context) {
    constexpr std::uint64_t kSize = 1 << 20;
    AvAdapter adapter;
    std::string error;
    D3d11Backend d3d11;
    D3d12Backend d3d12;
    Problems problems;
    std::unique_ptr<GpuBackend> opencl = BackendHarness::open("opencl", adapter, error);
    std::unique_ptr<GpuBackend> vulkan = BackendHarness::open("vulkan", adapter, error);
    context.measure("link.opencl", opencl != nullptr ? "runtime present" : "runtime missing");
    context.measure("link.vulkan", vulkan != nullptr ? "runtime present" : "runtime missing");
    if (!d3d11.init(adapter.luid, error) || !d3d12.init(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    HANDLE handle = nullptr;
    std::unique_ptr<GpuBuffer> shared = d3d11.create_shared(kSize, handle, error);
    std::vector<std::uint8_t> expected(kSize);
    Pattern::fill(31337, 0, expected.data(), kSize);
    std::vector<std::uint8_t> actual(kSize);
    if (shared == nullptr) {
        context.measure("link.d3d11_to_d3d12", "unsupported: " + error);
        context.note("D3D11 reports shared buffers unsupported (" + error + "); the D3D12 link is not exercised");
    } else {
        context.measure("link.d3d11_to_d3d12", "supported");
        std::string detail;
        if (!d3d11.upload(*shared, 0, expected.data(), kSize, detail) || !d3d11.finish(detail)) {
            problems.add("D3D11 write: " + detail);
        } else {
            std::unique_ptr<GpuBuffer> opened = d3d12.open_shared(handle, kSize, detail);
            if (opened == nullptr) {
                problems.add("D3D11 created a shared buffer but D3D12 cannot open it: " + detail);
            } else if (!d3d12.download(*opened, 0, actual.data(), kSize, detail) ||
                       !BackendHarness::verify(context, "D3D11 to D3D12", expected.data(), actual.data(), kSize, detail)) {
                problems.add(detail);
            }
        }
        ::CloseHandle(handle);
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::data
