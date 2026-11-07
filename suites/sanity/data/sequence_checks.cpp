#include "sanity/data/sequence_checks.hpp"

#include "support/api_loop.hpp"
#include "support/compare.hpp"
#include "support/d3d11_backend.hpp"
#include "support/api_policy.hpp"
#include "support/d3d12_backend.hpp"
#include "support/opencl_backend.hpp"
#include "support/target_adapter.hpp"
#include "support/vulkan_backend.hpp"
#include "core/text.hpp"
#include "support/pattern.hpp"

#include <d3d11_1.h>
#include <dxgi1_2.h>

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
    constexpr UINT kSize = 256;
    AvAdapter adapter;
    std::string error;
    D3d11Backend d3d11;
    D3d12Backend d3d12;
    VulkanBackend vulkan;
    OpenClBackend opencl;
    if (!TargetAdapter::pick(adapter, error) || !d3d11.init(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    const bool have_d3d12 = !ApiPolicy::is_skipped("d3d12") && d3d12.init(adapter.luid, error);
    std::string vulkan_error;
    const bool have_vulkan = !ApiPolicy::is_skipped("vulkan") && vulkan.init(adapter.luid, vulkan_error);
    std::string opencl_error;
    const bool have_opencl = !ApiPolicy::is_skipped("opencl") && opencl.init(adapter.luid, opencl_error);
    context.measure("runtime.d3d12", have_d3d12);
    context.measure("runtime.vulkan", have_vulkan);
    context.measure("runtime.opencl", have_opencl);
    struct Carrier {
        const char* name;
        UINT misc;
        bool nt;
        bool keyed;
    };
    const Carrier carriers[] = {{"nthandle_keyedmutex", D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX, true, true},
                                {"nthandle_shared", D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED, true, false},
                                {"shared_legacy", D3D11_RESOURCE_MISC_SHARED, false, false}};
    std::vector<std::uint8_t> expected(static_cast<std::size_t>(kSize) * kSize * 4);
    Pattern::fill_pixels(31337, kSize, kSize, kSize * 4, expected.data());
    Problems problems;
    int carriers_created = 0;
    for (const Carrier& carrier : carriers) {
        const std::string tag = std::string("carrier.") + carrier.name;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = kSize;
        desc.Height = kSize;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags = carrier.misc;
        ID3D11Texture2D* texture = nullptr;
        const HRESULT created = d3d11.shared().device->CreateTexture2D(&desc, nullptr, &texture);
        if (FAILED(created)) {
            context.measure(tag, "unsupported: CreateTexture2D 0x" + Text::hex(static_cast<std::uint32_t>(created), 8));
            continue;
        }
        ++carriers_created;
        context.measure(tag, "created");
        IDXGIKeyedMutex* mutex = nullptr;
        if (carrier.keyed) {
            texture->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void**>(&mutex));
            if (mutex != nullptr) {
                mutex->AcquireSync(0, 5000);
            }
        }
        d3d11.shared().context->UpdateSubresource(texture, 0, nullptr, expected.data(), kSize * 4, 0);
        std::string wait_error;
        static_cast<void>(d3d11.finish(wait_error));
        if (mutex != nullptr) {
            mutex->ReleaseSync(0);
            mutex->Release();
        }
        HANDLE handle = nullptr;
        if (carrier.nt) {
            IDXGIResource1* resource = nullptr;
            texture->QueryInterface(__uuidof(IDXGIResource1), reinterpret_cast<void**>(&resource));
            if (resource == nullptr || FAILED(resource->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle))) {
                problems.add(std::string(carrier.name) + ": CreateSharedHandle failed");
            }
            if (resource != nullptr) {
                resource->Release();
            }
        } else {
            IDXGIResource* resource = nullptr;
            texture->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(&resource));
            if (resource == nullptr || FAILED(resource->GetSharedHandle(&handle))) {
                problems.add(std::string(carrier.name) + ": GetSharedHandle failed");
            }
            if (resource != nullptr) {
                resource->Release();
            }
        }
        const auto compare = [&](const std::string& link, const std::vector<std::uint8_t>& actual) {
            std::string detail;
            if (!BackendHarness::verify(context, std::string(carrier.name) + " " + link, expected.data(), actual.data(), expected.size(), detail)) {
                problems.add(detail);
            }
        };
        std::vector<std::uint8_t> actual;
        const std::string d3d12_key = "link.d3d12." + std::string(carrier.name);
        if (!have_d3d12) {
            context.measure(d3d12_key, "runtime unavailable");
        } else if (!carrier.nt || handle == nullptr) {
            context.measure(d3d12_key, "not applicable: D3D12 opens NT handles only");
        } else {
            ID3D12Resource* opened = d3d12.open_shared_raw(handle, error);
            if (opened == nullptr) {
                context.measure(d3d12_key, "unsupported: " + error);
                if (!carrier.keyed) {
                    problems.add(std::string(carrier.name) + ": D3D12 cannot open the NT-shared texture: " + error);
                }
            } else {
                context.measure(d3d12_key, "supported");
                if (!d3d12.read_texture(opened, kSize, kSize, 4, actual, error)) {
                    problems.add(std::string(carrier.name) + " D3D11 to D3D12: " + error);
                } else {
                    compare("D3D11 to D3D12", actual);
                }
                opened->Release();
            }
        }
        const std::string vulkan_key = "link.vulkan." + std::string(carrier.name);
        const std::uint32_t vulkan_type = carrier.nt ? 0x8u : 0x10u;
        std::string vulkan_detail;
        if (!have_vulkan) {
            context.measure(vulkan_key, "runtime unavailable: " + vulkan_error);
        } else if (handle == nullptr || !vulkan.texture_import_supported(vulkan_type, vulkan_detail)) {
            context.measure(vulkan_key, "unsupported: " + vulkan_detail);
        } else {
            context.measure(vulkan_key, "supported");
            if (!vulkan.read_imported_texture(handle, vulkan_type, kSize, kSize, actual, error)) {
                problems.add(std::string(carrier.name) + " D3D11 to Vulkan is reported importable but fails: " + error);
            } else {
                compare("D3D11 to Vulkan", actual);
            }
        }
        const std::string opencl_key = "link.opencl." + std::string(carrier.name);
        if (!have_opencl) {
            context.measure(opencl_key, "runtime unavailable: " + opencl_error);
        } else if (!opencl.d3d11_sharing_advertised()) {
            context.measure(opencl_key, "unsupported: cl_khr_d3d11_sharing is not advertised");
        } else {
            context.measure(opencl_key, "supported");
            if (!opencl.read_d3d11_texture(d3d11.shared().device, texture, kSize, kSize, actual, error)) {
                problems.add(std::string(carrier.name) + " D3D11 to OpenCL is advertised but fails: " + error);
            } else {
                compare("D3D11 to OpenCL", actual);
            }
        }
        if (carrier.nt && handle != nullptr) {
            ::CloseHandle(handle);
        }
        texture->Release();
    }
    if (carriers_created == 0) {
        problems.add("D3D11 creates no shared texture with any of the three sharing flag sets");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::data
