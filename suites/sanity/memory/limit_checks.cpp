#include "sanity/memory/limit_checks.hpp"

#include "support/api_loop.hpp"
#include "support/api_policy.hpp"
#include "support/d3d11_textures.hpp"
#include "support/memory_tools.hpp"
#include "support/pattern.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>

namespace lsw::suites::sanity::memory {

namespace {

constexpr std::uint64_t kChunk = 128ull << 20;

std::string status_text(AllocStatus status) {
    return GpuBackends::describe(status);
}

}  // namespace

Outcome LimitChecks::allocate_up_to_reported(Context& context) {
    Problems problems;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        const std::uint64_t reported = backend.available_memory();
        const std::uint64_t target = std::min<std::uint64_t>(reported / 10 * 9, 24ull << 30);
        context.measure(api + ".reported_available", reported);
        std::uint64_t chunk = kChunk;
        if (backend.max_allocation() != 0) {
            chunk = std::min(chunk, backend.max_allocation());
        }
        std::vector<std::unique_ptr<GpuBuffer>> buffers;
        std::uint64_t total = 0;
        std::uint64_t seed = 3000;
        while (total < target) {
            AllocStatus status = AllocStatus::Ok;
            std::string detail;
            std::unique_ptr<GpuBuffer> buffer = backend.create(chunk, BufferKind::Device, status, detail);
            if (buffer == nullptr) {
                problems.add(api + ": allocation " + std::to_string(buffers.size()) + " (" + std::to_string(total >> 20) +
                             " MiB held of " + std::to_string(target >> 20) + " MiB reported available) was refused: " +
                             status_text(status) + " " + detail);
                break;
            }
            std::string error;
            const bool full = total < (1ull << 30);
            bool ok = true;
            if (full) {
                ok = BackendHarness::fill_and_verify(backend, *buffer, ++seed, chunk, context, error);
            } else {
                ok = MemoryTools::touch(backend, *buffer, ++seed, error) && MemoryTools::verify_ends(backend, *buffer, seed, context, error);
            }
            if (!ok) {
                problems.add(api + ": chunk " + std::to_string(buffers.size()) + ": " + error);
                break;
            }
            total += chunk;
            buffers.push_back(std::move(buffer));
        }
        context.measure(api + ".allocated", total);
    });
    return problems.outcome();
}

Outcome LimitChecks::fail_beyond_reported(Context& context) {
    Problems problems;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        AllocStatus status = AllocStatus::Ok;
        std::string detail;
        if (api == "opencl") {
            const std::uint64_t over = backend.max_allocation() + 1;
            std::unique_ptr<GpuBuffer> buffer = backend.create(over, BufferKind::Device, status, detail);
            context.measure(api + ".request_bytes", over);
            context.measure(api + ".status", status_text(status) + " " + detail);
            if (buffer != nullptr) {
                problems.add(api + ": a buffer of CL_DEVICE_MAX_MEM_ALLOC_SIZE + 1 = " + std::to_string(over) + " bytes was created");
            } else if (status != AllocStatus::TooLarge) {
                problems.add(api + ": the request above the maximum failed with '" + detail + "' instead of CL_INVALID_BUFFER_SIZE");
            }
            return;
        }
        const std::uint64_t request = backend.oversize_request();
        if (request == 0) {
            context.note(api + ": no request is defined that exceeds the memory it reports");
            return;
        }
        std::unique_ptr<GpuBuffer> buffer = backend.create(request, BufferKind::Device, status, detail);
        context.measure(api + ".request_mib", request >> 20);
        context.measure(api + ".status", status_text(status) + " " + detail);
        if (buffer == nullptr) {
            if (status != AllocStatus::OutOfMemory) {
                problems.add(api + ": the " + std::to_string(request >> 20) + " MiB request failed with '" + detail +
                             "' instead of an out-of-memory code");
            }
            return;
        }
        std::string error;
        if (!MemoryTools::touch(backend, *buffer, 43, error) || !MemoryTools::verify_ends(backend, *buffer, 43, context, error)) {
            problems.add(api + ": the " + std::to_string(request >> 20) + " MiB request succeeded but its first and last pages do not work: " + error);
        }
        static_cast<void>(backend.finish(error));
    });
    return problems.outcome();
}

Outcome LimitChecks::single_allocation_max(Context& context) {
    Problems problems;
    ApiLoop::run(problems, {"vulkan", "opencl"}, [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        const std::uint64_t maximum = backend.max_allocation();
        context.measure(api + ".max_allocation", maximum);
        if (maximum == 0) {
            problems.add(api + ": reports no maximum single allocation");
            return;
        }
        AllocStatus status = AllocStatus::Ok;
        std::string detail;
        {
            std::unique_ptr<GpuBuffer> buffer = backend.create(maximum, BufferKind::Device, status, detail);
            if (buffer == nullptr) {
                problems.add(api + ": the reported maximum of " + std::to_string(maximum) + " bytes was refused: " + detail);
            } else {
                std::string error;
                if (!MemoryTools::touch(backend, *buffer, 41, error) || !MemoryTools::verify_ends(backend, *buffer, 41, context, error)) {
                    problems.add(api + ": the maximum allocation cannot be used: " + error);
                }
            }
        }
        std::unique_ptr<GpuBuffer> over = backend.create(maximum + 1, BufferKind::Device, status, detail);
        context.measure(api + ".over_max_status", status_text(status) + " " + detail);
        if (api == "opencl") {
            if (over != nullptr) {
                problems.add(api + ": an allocation one byte above CL_DEVICE_MAX_MEM_ALLOC_SIZE succeeded");
            } else if (status != AllocStatus::TooLarge) {
                problems.add(api + ": the allocation one byte above the maximum failed with '" + detail + "' instead of CL_INVALID_BUFFER_SIZE");
            }
        } else if (over == nullptr) {
            if (status != AllocStatus::OutOfMemory) {
                problems.add(api + ": the allocation one byte above the maximum failed with '" + detail + "' instead of an out-of-memory code");
            }
        } else {
            std::string error;
            if (!MemoryTools::touch(backend, *over, 42, error) || !MemoryTools::verify_ends(backend, *over, 42, context, error)) {
                problems.add(api + ": the allocation above the maximum succeeded but does not work: " + error);
            }
        }
        std::string error;
        static_cast<void>(backend.finish(error));
    });
    context.note("D3D12 reports no maximum single allocation, so the row is checked for Vulkan and OpenCL");
    return problems.outcome();
}

Outcome LimitChecks::zero_size(Context& context) {
    Problems problems;
    ApiLoop::run(problems, {"d3d11", "d3d12", "vulkan", "opencl", "gl", "d3d9"}, [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        AllocStatus status = AllocStatus::Ok;
        std::string detail;
        std::unique_ptr<GpuBuffer> buffer = backend.create(0, BufferKind::Device, status, detail);
        context.measure(api + ".zero_size", buffer != nullptr ? std::string("valid empty object") : status_text(status) + ": " + detail);
        if (status == AllocStatus::OutOfMemory) {
            problems.add(api + ": a zero-sized request reports out of memory");
        }
        buffer.reset();
        AllocStatus later = AllocStatus::Ok;
        std::string error;
        if (!BackendHarness::roundtrip(backend, 4096, 51, later, context, error) || later != AllocStatus::Ok) {
            problems.add(api + ": the device is not usable after the zero-sized request: " + error);
        }
    });
    return problems.outcome();
}

Outcome LimitChecks::after_failure_usable(Context& context) {
    Problems problems;
    ApiLoop::run(problems, {"d3d11", "d3d12", "vulkan", "opencl", "gl", "d3d9"}, [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        AllocStatus status = AllocStatus::Ok;
        std::string detail;
        std::string provoked = "nothing";
        if (api == "opencl") {
            std::unique_ptr<GpuBuffer> big = backend.create(backend.max_allocation() + 1, BufferKind::Device, status, detail);
            provoked = big == nullptr ? "request above the maximum allocation: " + detail : "request above the maximum allocation succeeded";
        } else if (api == "vulkan") {
            std::unique_ptr<GpuBuffer> big = backend.create(backend.oversize_request(), BufferKind::Device, status, detail);
            provoked = big == nullptr ? "oversize request: " + detail : "oversize request succeeded";
        } else {
            std::unique_ptr<GpuBuffer> empty = backend.create(0, BufferKind::Device, status, detail);
            provoked = empty == nullptr ? "zero-sized creation: " + detail : "zero-sized creation returned an empty object";
            if (api == "d3d12") {
                const std::uint64_t request = backend.oversize_request();
                std::unique_ptr<GpuBuffer> big = request != 0 ? backend.create(request, BufferKind::Device, status, detail) : nullptr;
                provoked += big == nullptr ? "; oversize request: " + detail : "; oversize request succeeded";
            }
        }
        context.measure(api + ".provoked", provoked);
        std::string error;
        AllocStatus later = AllocStatus::Ok;
        if (!BackendHarness::roundtrip(backend, 1 << 20, 61, later, context, error) || later != AllocStatus::Ok) {
            problems.add(api + ": a 1 MiB roundtrip fails after the failure (" + provoked + "): " + error);
            return;
        }
        std::unique_ptr<GpuBuffer> source = backend.create(65536, BufferKind::Device, later, error);
        std::unique_ptr<GpuBuffer> destination = backend.create(65536, BufferKind::Device, later, error);
        if (source == nullptr || destination == nullptr) {
            problems.add(api + ": allocation fails after the failure: " + error);
            return;
        }
        std::vector<std::uint8_t> expected(65536);
        std::vector<std::uint8_t> actual(65536);
        Pattern::fill(62, 0, expected.data(), expected.size());
        if (!backend.upload(*source, 0, expected.data(), expected.size(), error) ||
            !backend.copy(*source, 0, *destination, 0, expected.size(), error) ||
            !backend.download(*destination, 0, actual.data(), actual.size(), error) ||
            !BackendHarness::verify(context, api + " copy after failure", expected.data(), actual.data(), expected.size(), error)) {
            problems.add(error);
        }
    });
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!ApiPolicy::is_skipped("d3d11") && TargetAdapter::pick(adapter, error) && device.create(adapter.luid, error)) {
        HRESULT result = S_OK;
        ID3D11Texture2D* target = D3d11Textures::make2d(device, 64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT,
                                                        D3D11_BIND_RENDER_TARGET, 0, result);
        ID3D11RenderTargetView* view = nullptr;
        if (target != nullptr && SUCCEEDED(device.device->CreateRenderTargetView(target, nullptr, &view))) {
            const float color[4] = {1.0f, 0.0f, 0.5f, 1.0f};
            device.context->ClearRenderTargetView(view, color);
            std::vector<std::uint8_t> pixels;
            if (D3d11Textures::read2d(device, target, 64, 64, 4, pixels, error)) {
                if (pixels[0] != 255 || pixels[1] != 0 || (pixels[2] != 127 && pixels[2] != 128) || pixels[3] != 255) {
                    problems.add("D3D11 render produced the wrong color");
                }
            } else {
                problems.add(error);
            }
            view->Release();
        } else {
            problems.add("D3D11 render target creation failed");
        }
        if (target != nullptr) {
            target->Release();
        }
    }
    return problems.outcome();
}

Outcome LimitChecks::freed_becomes_usable(Context& context) {
    Problems problems;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        constexpr std::uint64_t kBlock = 64ull << 20;
        std::uint64_t total = std::min<std::uint64_t>(backend.available_memory() / 2, 2ull << 30);
        if (backend.max_allocation() != 0 && backend.max_allocation() < kBlock) {
            total = 0;
        }
        const std::size_t count = static_cast<std::size_t>(total / kBlock);
        if (count == 0) {
            problems.add(api + ": less than 64 MiB reported available");
            return;
        }
        for (int round = 0; round < 2; ++round) {
            std::vector<std::unique_ptr<GpuBuffer>> buffers;
            for (std::size_t i = 0; i < count; ++i) {
                AllocStatus status = AllocStatus::Ok;
                std::string detail;
                std::unique_ptr<GpuBuffer> buffer = backend.create(kBlock, BufferKind::Device, status, detail);
                std::string error;
                if (buffer == nullptr || !MemoryTools::touch(backend, *buffer, 70 + i, error)) {
                    problems.add(api + ": round " + std::to_string(round) + " block " + std::to_string(i) + ": " + (buffer == nullptr ? detail : error));
                    return;
                }
                buffers.push_back(std::move(buffer));
            }
            std::string error;
            if (!backend.finish(error)) {
                problems.add(api + ": " + error);
                return;
            }
            buffers.clear();
            if (!backend.finish(error)) {
                problems.add(api + ": " + error);
                return;
            }
        }
        context.measure(api + ".blocks", count);
    });
    return problems.outcome();
}

Outcome LimitChecks::fragmented_pattern(Context& context) {
    Problems problems;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        std::vector<std::unique_ptr<GpuBuffer>> small;
        std::vector<std::unique_ptr<GpuBuffer>> large;
        std::uint64_t freed = 0;
        for (int i = 0; i < 32; ++i) {
            AllocStatus status = AllocStatus::Ok;
            std::string detail;
            std::string error;
            std::unique_ptr<GpuBuffer> a = backend.create(1 << 20, BufferKind::Device, status, detail);
            std::unique_ptr<GpuBuffer> b = backend.create(3 << 20, BufferKind::Device, status, detail);
            if (a == nullptr || b == nullptr || !MemoryTools::touch(backend, *a, 90 + static_cast<std::uint64_t>(i), error) || !MemoryTools::touch(backend, *b, 190 + static_cast<std::uint64_t>(i), error)) {
                problems.add(api + ": pair " + std::to_string(i) + ": " + (a == nullptr || b == nullptr ? detail : error));
                return;
            }
            freed += 1 << 20;
            small.push_back(std::move(a));
            large.push_back(std::move(b));
        }
        std::string error;
        if (!backend.finish(error)) {
            problems.add(api + ": " + error);
            return;
        }
        small.clear();
        AllocStatus status = AllocStatus::Ok;
        if (!BackendHarness::roundtrip(backend, freed, 99, status, context, error) || status != AllocStatus::Ok) {
            problems.add(api + ": a " + std::to_string(freed >> 20) + " MiB block does not fit after freeing every other buffer: " + error);
        }
    });
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::memory
