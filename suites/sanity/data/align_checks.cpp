#include "sanity/data/align_checks.hpp"

#include "core/text.hpp"
#include "support/api_loop.hpp"
#include "support/compare.hpp"
#include "support/d3d11_textures.hpp"
#include "support/pattern.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::sanity::data {

namespace {

std::unique_ptr<GpuBuffer> make(GpuBackend& backend, std::uint64_t size, BufferKind kind, Problems& problems) {
    AllocStatus status = AllocStatus::Ok;
    std::string detail;
    std::unique_ptr<GpuBuffer> buffer = backend.create(size, kind, status, detail);
    if (buffer == nullptr) {
        problems.add(backend.name() + " create " + std::to_string(size) + ": " + detail);
    }
    return buffer;
}

}  // namespace

Outcome AlignChecks::offset_every_byte(Context& context) {
    constexpr std::uint64_t kBuffer = 8192;
    constexpr std::uint64_t kWrite = 4096;
    Problems problems;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        std::unique_ptr<GpuBuffer> buffer = make(backend, kBuffer, BufferKind::Device, problems);
        if (buffer == nullptr) {
            return;
        }
        std::vector<std::uint8_t> base(kBuffer);
        std::vector<std::uint8_t> payload(kWrite);
        std::vector<std::uint8_t> expected;
        std::vector<std::uint8_t> actual(kBuffer);
        Pattern::fill(11, 0, base.data(), base.size());
        for (std::uint64_t offset = 0; offset < 256; ++offset) {
            Pattern::fill(1000 + offset, 0, payload.data(), payload.size());
            expected = base;
            std::memcpy(expected.data() + offset, payload.data(), payload.size());
            std::string error;
            if (!backend.upload(*buffer, 0, base.data(), kBuffer, error) ||
                !backend.upload(*buffer, offset, payload.data(), kWrite, error) ||
                !backend.download(*buffer, 0, actual.data(), kBuffer, error) ||
                !BackendHarness::verify(context, api + " write at offset " + std::to_string(offset), expected.data(),
                                        actual.data(), kBuffer, error)) {
                problems.add(error);
                return;
            }
        }
    });
    return problems.outcome();
}

Outcome AlignChecks::unaligned_copy_regions(Context& context) {
    constexpr std::uint64_t kBuffer = 16384;
    const std::uint64_t offsets[] = {0, 1, 3, 7, 13, 255};
    const std::uint64_t lengths[] = {1, 3, 17, 255, 4097};
    Problems problems;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        std::unique_ptr<GpuBuffer> source = make(backend, kBuffer, BufferKind::Device, problems);
        std::unique_ptr<GpuBuffer> destination = make(backend, kBuffer, BufferKind::Device, problems);
        if (source == nullptr || destination == nullptr) {
            return;
        }
        std::vector<std::uint8_t> source_data(kBuffer);
        std::vector<std::uint8_t> poison(kBuffer, 0xEE);
        std::vector<std::uint8_t> expected;
        std::vector<std::uint8_t> actual(kBuffer);
        Pattern::fill(21, 0, source_data.data(), kBuffer);
        std::string error;
        if (!backend.upload(*source, 0, source_data.data(), kBuffer, error)) {
            problems.add(api + ": " + error);
            return;
        }
        for (std::uint64_t so : offsets) {
            for (std::uint64_t doff : offsets) {
                for (std::uint64_t length : lengths) {
                    expected = poison;
                    std::memcpy(expected.data() + doff, source_data.data() + so, static_cast<std::size_t>(length));
                    if (!backend.upload(*destination, 0, poison.data(), kBuffer, error) ||
                        !backend.copy(*source, so, *destination, doff, length, error) ||
                        !backend.download(*destination, 0, actual.data(), kBuffer, error) ||
                        !BackendHarness::verify(context,
                                                api + " copy " + std::to_string(so) + "->" + std::to_string(doff) + " len " +
                                                    std::to_string(length),
                                                expected.data(), actual.data(), kBuffer, error)) {
                        problems.add(error);
                        return;
                    }
                }
            }
        }
    });
    return problems.outcome();
}

Outcome AlignChecks::mapped_pointer_alignment(Context& context) {
    constexpr std::uint64_t kSize = 65536;
    Problems problems;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        if (!backend.supports_map()) {
            return;
        }
        std::unique_ptr<GpuBuffer> first = make(backend, kSize, BufferKind::Upload, problems);
        std::unique_ptr<GpuBuffer> second = make(backend, kSize, BufferKind::Upload, problems);
        if (first == nullptr || second == nullptr) {
            return;
        }
        std::vector<std::uint8_t> a(kSize);
        std::vector<std::uint8_t> b(kSize);
        std::vector<std::uint8_t> check(kSize);
        Pattern::fill(31, 0, a.data(), kSize);
        Pattern::fill(32, 0, b.data(), kSize);
        std::string error;
        if (!backend.upload(*second, 0, a.data(), kSize, error) || !backend.upload(*first, 0, a.data(), kSize, error)) {
            problems.add(api + ": " + error);
            return;
        }
        void* pointer = nullptr;
        if (!backend.map(*first, &pointer, error)) {
            problems.add(api + ": " + error);
            return;
        }
        const std::uint64_t required = std::max<std::uint64_t>(backend.map_alignment(), 1);
        context.measure(api + ".required_alignment", required);
        context.measure(api + ".pointer_modulo_4096", reinterpret_cast<std::uintptr_t>(pointer) % 4096);
        if (reinterpret_cast<std::uintptr_t>(pointer) % required != 0) {
            problems.add(api + ": mapped pointer " + Text::hex(reinterpret_cast<std::uintptr_t>(pointer), 16) +
                         " is not aligned to the documented " + std::to_string(required));
        }
        std::memcpy(pointer, b.data(), kSize);
        if (!backend.unmap(*first, error) || !backend.download(*first, 0, check.data(), kSize, error) ||
            !BackendHarness::verify(context, api + " mapped write", b.data(), check.data(), kSize, error)) {
            problems.add(error);
            return;
        }
        if (!backend.download(*second, 0, check.data(), kSize, error) ||
            !BackendHarness::verify(context, api + " neighbour of a mapped buffer", a.data(), check.data(), kSize, error)) {
            problems.add(error);
        }
    });
    return problems.outcome();
}

Outcome AlignChecks::row_pitch_variants(Context& context) {
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    const UINT extras[] = {0, 1, 4, 16, 100};
    for (UINT width : {7u, 64u, 100u}) {
        for (UINT extra : extras) {
            const UINT height = 13;
            const UINT pitch = width * 4 + extra;
            std::vector<std::uint8_t> padded(static_cast<std::size_t>(pitch) * height, 0xFE);
            std::vector<std::uint8_t> expected(static_cast<std::size_t>(width) * 4 * height);
            Pattern::fill(width + extra, 0, expected.data(), expected.size());
            for (UINT y = 0; y < height; ++y) {
                std::memcpy(padded.data() + static_cast<std::size_t>(y) * pitch, expected.data() + static_cast<std::size_t>(y) * width * 4,
                            width * 4);
            }
            HRESULT result = S_OK;
            ID3D11Texture2D* texture = D3d11Textures::make2d(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT,
                                                             D3D11_BIND_SHADER_RESOURCE, 0, result);
            if (texture == nullptr) {
                problems.add("CreateTexture2D failed");
                continue;
            }
            device.context->UpdateSubresource(texture, 0, nullptr, padded.data(), pitch, 0);
            std::vector<std::uint8_t> back;
            std::string detail;
            if (!D3d11Textures::read2d(device, texture, width, height, 4, back, detail) ||
                !BackendHarness::verify(context, "pitch +" + std::to_string(extra) + " width " + std::to_string(width), expected.data(),
                                        back.data(), expected.size(), detail)) {
                problems.add(detail);
            }
            texture->Release();
        }
    }
    for (UINT extra_depth : {0u, 3u, 64u}) {
        constexpr UINT kWidth = 8;
        constexpr UINT kHeight = 6;
        constexpr UINT kDepth = 5;
        const UINT row = kWidth * 4 + 8;
        const UINT slice = row * kHeight + extra_depth * 4;
        std::vector<std::uint8_t> padded(static_cast<std::size_t>(slice) * kDepth, 0xFE);
        std::vector<std::uint8_t> expected(static_cast<std::size_t>(kWidth) * 4 * kHeight * kDepth);
        Pattern::fill(77 + extra_depth, 0, expected.data(), expected.size());
        for (UINT z = 0; z < kDepth; ++z) {
            for (UINT y = 0; y < kHeight; ++y) {
                std::memcpy(padded.data() + static_cast<std::size_t>(z) * slice + static_cast<std::size_t>(y) * row,
                            expected.data() + (static_cast<std::size_t>(z) * kHeight + y) * kWidth * 4, kWidth * 4);
            }
        }
        HRESULT result = S_OK;
        ID3D11Texture3D* volume = D3d11Textures::make3d(device, kWidth, kHeight, kDepth, DXGI_FORMAT_R8G8B8A8_UNORM, result);
        if (volume == nullptr) {
            problems.add("CreateTexture3D failed");
            continue;
        }
        device.context->UpdateSubresource(volume, 0, nullptr, padded.data(), row, slice);
        std::vector<std::uint8_t> back;
        std::string detail;
        if (!D3d11Textures::read3d(device, volume, kWidth, kHeight, kDepth, 4, back, detail) ||
            !BackendHarness::verify(context, "depth pitch +" + std::to_string(extra_depth * 4), expected.data(), back.data(),
                                    expected.size(), detail)) {
            problems.add(detail);
        }
        volume->Release();
    }
    return problems.outcome();
}

Outcome AlignChecks::sub_region_updates(Context& context) {
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    constexpr UINT kSize = 64;
    struct Box {
        const char* name;
        UINT left, top, right, bottom;
    };
    const Box boxes[] = {{"left edge", 0, 5, 9, 20},       {"right edge", 55, 5, 64, 20}, {"top edge", 7, 0, 30, 6},
                         {"bottom edge", 7, 58, 30, 64},   {"top-left corner", 0, 0, 3, 3}, {"bottom-right corner", 61, 61, 64, 64},
                         {"interior", 10, 10, 11, 11},     {"one row", 0, 31, 64, 32},      {"one column", 31, 0, 32, 64}};
    Problems problems;
    for (const Box& box : boxes) {
        std::vector<std::uint8_t> base(kSize * kSize * 4);
        Pattern::fill_pixels(5, kSize, kSize, kSize * 4, base.data());
        const UINT w = box.right - box.left;
        const UINT h = box.bottom - box.top;
        std::vector<std::uint8_t> patch(static_cast<std::size_t>(w) * h * 4);
        Pattern::fill_pixels(6, w, h, w * 4, patch.data());
        std::vector<std::uint8_t> expected = base;
        for (UINT y = 0; y < h; ++y) {
            std::memcpy(expected.data() + (static_cast<std::size_t>(box.top + y) * kSize + box.left) * 4,
                        patch.data() + static_cast<std::size_t>(y) * w * 4, static_cast<std::size_t>(w) * 4);
        }
        HRESULT result = S_OK;
        ID3D11Texture2D* texture = D3d11Textures::make2d(device, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT,
                                                         D3D11_BIND_SHADER_RESOURCE, 0, result);
        if (texture == nullptr) {
            problems.add("CreateTexture2D failed");
            continue;
        }
        device.context->UpdateSubresource(texture, 0, nullptr, base.data(), kSize * 4, 0);
        const D3D11_BOX d3d_box{box.left, box.top, 0, box.right, box.bottom, 1};
        device.context->UpdateSubresource(texture, 0, &d3d_box, patch.data(), w * 4, 0);
        std::vector<std::uint8_t> back;
        std::string detail;
        if (!D3d11Textures::read2d(device, texture, kSize, kSize, 4, back, detail) ||
            !BackendHarness::verify(context, std::string("sub-region ") + box.name, expected.data(), back.data(), expected.size(),
                                    detail)) {
            problems.add(detail);
        }
        texture->Release();
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::data
