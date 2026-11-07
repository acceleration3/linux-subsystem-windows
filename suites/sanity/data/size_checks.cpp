#include "sanity/data/size_checks.hpp"

#include "core/text.hpp"
#include "support/api_loop.hpp"
#include "support/artifact_writer.hpp"
#include "support/compare.hpp"
#include "support/d3d11_textures.hpp"
#include "support/dxgi_budget.hpp"
#include "support/pattern.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::sanity::data {

namespace {

const std::vector<std::string>& four_apis() {
    static const std::vector<std::string> apis = GpuBackends::names();
    return apis;
}

std::string size_text(std::uint64_t size) {
    return std::to_string(size) + " bytes";
}

void roundtrip_sizes(Context& context, Problems& problems, GpuBackend& backend, const std::vector<std::uint64_t>& sizes,
                     std::uint64_t seed_base, std::size_t& skipped) {
    std::uint64_t seed = seed_base;
    for (std::uint64_t size : sizes) {
        AllocStatus status = AllocStatus::Ok;
        std::string error;
        if (!BackendHarness::roundtrip(backend, size, ++seed, status, context, error)) {
            problems.add(backend.name() + " " + size_text(size) + ": " + error);
            return;
        }
        if (status != AllocStatus::Ok) {
            ++skipped;
            context.note(backend.name() + " " + size_text(size) + " not allocated: " + error);
        }
    }
}

}  // namespace

Outcome SizeChecks::buffer_roundtrip_small(Context& context) {
    std::vector<std::uint64_t> sizes;
    for (std::uint64_t size = 1; size <= 600; ++size) {
        sizes.push_back(size);
    }
    for (std::uint64_t size : {1024ull, 2047ull, 4095ull, 4096ull, 4097ull, 8192ull, 65535ull, 65536ull, 65537ull, 262144ull, 1048575ull, 1048576ull}) {
        sizes.push_back(size);
    }
    Problems problems;
    ApiLoop::run(problems, four_apis(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        std::size_t skipped = 0;
        roundtrip_sizes(context, problems, backend, sizes, 100, skipped);
        context.measure(api + ".skipped", skipped);
    });
    return problems.outcome();
}

Outcome SizeChecks::buffer_roundtrip_boundaries(Context& context) {
    std::vector<std::uint64_t> sizes;
    for (std::uint64_t mib : {4ull, 16ull, 32ull, 64ull, 256ull}) {
        for (std::int64_t delta : {-1, 0, 1}) {
            sizes.push_back(static_cast<std::uint64_t>(static_cast<std::int64_t>(mib << 20) + delta));
        }
    }
    Problems problems;
    ApiLoop::run(problems, four_apis(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        std::size_t skipped = 0;
        const std::uint64_t limit = backend.max_allocation();
        std::vector<std::uint64_t> usable;
        for (std::uint64_t size : sizes) {
            if (limit == 0 || size <= limit) {
                usable.push_back(size);
            }
        }
        roundtrip_sizes(context, problems, backend, usable, 200, skipped);
        context.measure(api + ".skipped", skipped);
    });
    return problems.outcome();
}

Outcome SizeChecks::buffer_roundtrip_hundreds_mib(Context& context) {
    Problems problems;
    ApiLoop::run(problems, four_apis(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        std::uint64_t seed = 300;
        for (std::uint64_t mib : {300ull, 400ull, 512ull, 600ull, 700ull}) {
            const std::uint64_t size = mib << 20;
            if ((backend.max_allocation() != 0 && size > backend.max_allocation()) || size > backend.available_memory()) {
                context.note(api + " " + std::to_string(mib) + " MiB exceeds what the API reports available");
                continue;
            }
            AllocStatus status = AllocStatus::Ok;
            std::string error;
            if (!BackendHarness::roundtrip(backend, size, ++seed, status, context, error)) {
                problems.add(api + " " + std::to_string(mib) + " MiB: " + error);
            } else if (status != AllocStatus::Ok) {
                context.note(api + " " + std::to_string(mib) + " MiB refused cleanly: " + error);
            }
        }
    });
    return problems.outcome();
}

Outcome SizeChecks::texture_upload_sizes(Context& context) {
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    struct Format {
        DXGI_FORMAT format;
        UINT bpp;
    };
    const Format formats[] = {{DXGI_FORMAT_R8_UNORM, 1}, {DXGI_FORMAT_R8G8B8A8_UNORM, 4},
                              {DXGI_FORMAT_R16G16B16A16_UNORM, 8}, {DXGI_FORMAT_R32G32B32A32_UINT, 16}};
    const UINT dims[] = {1, 2, 3, 5, 7, 17, 63, 255, 257, 1023, 4097, 16384};
    Problems problems;
    std::size_t tested = 0;
    for (const Format& format : formats) {
        for (UINT w : dims) {
            for (UINT h : dims) {
                const std::uint64_t bytes = static_cast<std::uint64_t>(w) * h * format.bpp;
                if (bytes > (96ull << 20) || (w > 257 && h > 257)) {
                    continue;
                }
                HRESULT result = S_OK;
                ID3D11Texture2D* texture = D3d11Textures::make2d(device, w, h, format.format, D3D11_USAGE_DEFAULT,
                                                                 D3D11_BIND_SHADER_RESOURCE, 0, result);
                if (texture == nullptr) {
                    if (result == E_OUTOFMEMORY) {
                        continue;
                    }
                    problems.add("CreateTexture2D " + std::to_string(w) + "x" + std::to_string(h) + " fmt " +
                                 std::to_string(format.format) + " failed: 0x" + Text::hex(static_cast<std::uint32_t>(result), 8));
                    continue;
                }
                std::vector<std::uint8_t> source(static_cast<std::size_t>(bytes));
                Pattern::fill(w * 131ull + h, 0, source.data(), source.size());
                device.context->UpdateSubresource(texture, 0, nullptr, source.data(), w * format.bpp, 0);
                std::vector<std::uint8_t> back;
                if (!D3d11Textures::read2d(device, texture, w, h, format.bpp, back, error)) {
                    problems.add(error);
                } else {
                    std::string detail;
                    if (!BackendHarness::verify(context, "tex " + std::to_string(w) + "x" + std::to_string(h) + " bpp" +
                                                             std::to_string(format.bpp),
                                                source.data(), back.data(), source.size(), detail)) {
                        problems.add(detail);
                    }
                }
                texture->Release();
                ++tested;
                if (!problems.empty()) {
                    return problems.outcome();
                }
            }
        }
    }
    context.measure("textures", tested);
    return problems.outcome();
}

Outcome SizeChecks::texture_full_screen_upload(Context& context) {
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    constexpr UINT kWidth = 2560;
    constexpr UINT kHeight = 1440;
    std::vector<std::uint8_t> source(static_cast<std::size_t>(kWidth) * kHeight * 4);
    Pattern::fill_pixels(7, kWidth, kHeight, kWidth * 4, source.data());
    Problems problems;
    const auto verify = [&](ID3D11Texture2D* texture, const char* path) {
        std::vector<std::uint8_t> back;
        std::string detail;
        if (!D3d11Textures::read2d(device, texture, kWidth, kHeight, 4, back, detail)) {
            problems.add(std::string(path) + ": " + detail);
            return;
        }
        const std::optional<Mismatch> mismatch = Compare::pixels(source.data(), back.data(), kWidth, kHeight, kWidth * 4, kWidth * 4);
        if (mismatch) {
            problems.add(std::string(path) + ": " + mismatch->describe());
            ArtifactWriter writer(context);
            writer.image(std::string(path) + ".expected", source.data(), kWidth, kHeight, kWidth * 4);
            writer.image(std::string(path) + ".actual", back.data(), kWidth, kHeight, kWidth * 4);
        }
    };
    HRESULT result = S_OK;
    ID3D11Texture2D* target = D3d11Textures::make2d(device, kWidth, kHeight, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT,
                                                    D3D11_BIND_SHADER_RESOURCE, 0, result);
    if (target == nullptr) {
        return Outcome::fail("CreateTexture2D 2560x1440 failed");
    }
    device.context->UpdateSubresource(target, 0, nullptr, source.data(), kWidth * 4, 0);
    verify(target, "UpdateSubresource");
    target->Release();

    ID3D11Texture2D* dynamic = D3d11Textures::make2d(device, kWidth, kHeight, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DYNAMIC,
                                                     D3D11_BIND_SHADER_RESOURCE, D3D11_CPU_ACCESS_WRITE, result);
    if (dynamic == nullptr) {
        problems.add("CreateTexture2D (dynamic) failed");
    } else {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(device.context->Map(dynamic, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            problems.add("Map WRITE_DISCARD failed");
        } else {
            for (UINT y = 0; y < kHeight; ++y) {
                std::memcpy(static_cast<char*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
                            source.data() + static_cast<std::size_t>(y) * kWidth * 4, kWidth * 4);
            }
            device.context->Unmap(dynamic, 0);
            verify(dynamic, "Map-write");
        }
        dynamic->Release();
    }

    ID3D11Texture2D* upload = D3d11Textures::make2d(device, kWidth, kHeight, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_STAGING, 0,
                                                    D3D11_CPU_ACCESS_WRITE, result);
    ID3D11Texture2D* copy = D3d11Textures::make2d(device, kWidth, kHeight, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT,
                                                  D3D11_BIND_SHADER_RESOURCE, 0, result);
    if (upload == nullptr || copy == nullptr) {
        problems.add("creating the staging-copy textures failed");
    } else {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(device.context->Map(upload, 0, D3D11_MAP_WRITE, 0, &mapped))) {
            problems.add("Map WRITE on the staging texture failed");
        } else {
            for (UINT y = 0; y < kHeight; ++y) {
                std::memcpy(static_cast<char*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
                            source.data() + static_cast<std::size_t>(y) * kWidth * 4, kWidth * 4);
            }
            device.context->Unmap(upload, 0);
            device.context->CopyResource(copy, upload);
            verify(copy, "staging-copy");
        }
    }
    if (upload != nullptr) {
        upload->Release();
    }
    if (copy != nullptr) {
        copy->Release();
    }
    return problems.outcome();
}

Outcome SizeChecks::readback_every_size(Context& context) {
    std::vector<std::uint64_t> sizes;
    for (std::uint64_t size : BackendHarness::boundary_sizes()) {
        if (size <= (65ull << 20)) {
            sizes.push_back(size);
        }
    }
    Problems problems;
    ApiLoop::run(problems, four_apis(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        std::uint64_t seed = 400;
        for (std::uint64_t size : sizes) {
            AllocStatus status = AllocStatus::Ok;
            std::string detail;
            std::unique_ptr<GpuBuffer> buffer = backend.create(size, BufferKind::Device, status, detail);
            if (buffer == nullptr) {
                if (status == AllocStatus::Failed) {
                    problems.add(api + " " + size_text(size) + ": " + detail);
                    return;
                }
                continue;
            }
            std::string error;
            if (!BackendHarness::fill_and_verify(backend, *buffer, ++seed, size, context, error)) {
                problems.add(api + ": " + error);
                return;
            }
        }
    });
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::data
