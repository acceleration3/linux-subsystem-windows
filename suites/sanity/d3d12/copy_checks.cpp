#include "sanity/d3d12/copy_checks.hpp"

#include "sanity/d3d12/kit.hpp"
#include "support/format_codec.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::sanity::d3d12 {

namespace {

struct FootprintCase {
    std::string name;
    D3D12_RESOURCE_DESC desc;
};

UINT mip_extent(UINT value, UINT mip) {
    return std::max<UINT>(1, value >> mip);
}

bool footprint_case(Context& context, D3d12Rig& rig, Problems& problems, const FootprintCase& item) {
    ID3D12Device* d = rig.device();
    const D3D12_RESOURCE_DESC& desc = item.desc;
    const bool volume = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    const UINT layers = volume ? 1 : desc.DepthOrArraySize;
    const UINT count = desc.MipLevels * layers;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(count);
    std::vector<UINT> rows(count);
    std::vector<UINT64> row_sizes(count);
    UINT64 total = 0;
    d->GetCopyableFootprints(&desc, 0, count, 0, layouts.data(), rows.data(), row_sizes.data(), &total);
    const bool block = FormatCodec::is_block(desc.Format);
    const UINT block_edge = block ? 4 : 1;
    const UINT64 unit = static_cast<UINT64>(block ? FormatCodec::block_bytes(desc.Format) : FormatCodec::element_bytes(desc.Format));
    UINT64 previous_end = 0;
    for (UINT i = 0; i < count; ++i) {
        const UINT mip = i % desc.MipLevels;
        const UINT width = mip_extent(static_cast<UINT>(desc.Width), mip);
        const UINT height = mip_extent(desc.Height, mip);
        const UINT depth = volume ? mip_extent(desc.DepthOrArraySize, mip) : 1;
        const std::string label = item.name + " subresource " + std::to_string(i);
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout = layouts[i];
        if (layout.Offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT != 0) {
            problems.add(label + ": offset " + std::to_string(layout.Offset) + " is not 512 aligned");
        }
        if (layout.Footprint.RowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT != 0 || layout.Footprint.RowPitch < row_sizes[i]) {
            problems.add(label + ": row pitch " + std::to_string(layout.Footprint.RowPitch) + " is not a 256 multiple covering a row of " + std::to_string(row_sizes[i]));
        }
        if (layout.Footprint.Format != desc.Format || layout.Footprint.Width < width || layout.Footprint.Height < height || layout.Footprint.Depth != depth) {
            problems.add(label + ": the footprint extent or format does not match the mip level");
        }
        if (unit != 0) {
            const UINT64 expected_row = static_cast<UINT64>((width + block_edge - 1) / block_edge) * unit;
            if (row_sizes[i] != expected_row) {
                problems.add(label + ": row size " + std::to_string(row_sizes[i]) + ", expected " + std::to_string(expected_row));
            }
        }
        if (rows[i] != (height + block_edge - 1) / block_edge) {
            problems.add(label + ": " + std::to_string(rows[i]) + " rows, expected " + std::to_string((height + block_edge - 1) / block_edge));
        }
        if (i != 0 && layout.Offset < previous_end) {
            problems.add(label + ": overlaps the previous subresource");
        }
        previous_end = layout.Offset + static_cast<UINT64>(layout.Footprint.RowPitch) * rows[i] * depth - (layout.Footprint.RowPitch - row_sizes[i]);
    }
    if (total < previous_end) {
        problems.add(item.name + ": the total size is smaller than the last subresource's end");
    }
    HRESULT made = S_OK;
    ID3D12Resource* texture = D3d12Tools::texture(rig, desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, &made);
    if (texture == nullptr) {
        problems.add(item.name + ": creating the texture failed: " + Kit::hr(made));
        return false;
    }
    ID3D12Resource* upload = D3d12Tools::upload(rig, total);
    ID3D12Resource* readback = D3d12Tools::readback(rig, total);
    if (upload == nullptr || readback == nullptr) {
        problems.add(item.name + ": creating the staging buffers failed");
        return false;
    }
    std::vector<std::uint8_t> expected(static_cast<std::size_t>(total), 0);
    for (UINT i = 0; i < count; ++i) {
        const UINT depth = layouts[i].Footprint.Depth;
        for (UINT z = 0; z < depth; ++z) {
            for (UINT r = 0; r < rows[i]; ++r) {
                std::uint8_t* row = expected.data() + layouts[i].Offset + (static_cast<UINT64>(z) * rows[i] + r) * layouts[i].Footprint.RowPitch;
                Pattern::fill(i * 977u + z * 61u + r + 5000, 0, row, static_cast<std::size_t>(row_sizes[i]));
            }
        }
    }
    void* mapped = nullptr;
    const D3D12_RANGE none{0, 0};
    if (!Kit::succeeds(problems, item.name + " Map(upload)", upload->Map(0, &none, &mapped))) {
        return false;
    }
    std::memcpy(mapped, expected.data(), expected.size());
    upload->Unmap(0, nullptr);
    std::string error;
    const bool done = rig.direct().run_wait(
        [&](ID3D12GraphicsCommandList* list) {
            for (UINT i = 0; i < count; ++i) {
                D3D12_TEXTURE_COPY_LOCATION destination{};
                destination.pResource = texture;
                destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                destination.SubresourceIndex = i;
                D3D12_TEXTURE_COPY_LOCATION source{};
                source.pResource = upload;
                source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                source.PlacedFootprint = layouts[i];
                list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            }
            D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
            for (UINT i = 0; i < count; ++i) {
                D3D12_TEXTURE_COPY_LOCATION destination{};
                destination.pResource = readback;
                destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                destination.PlacedFootprint = layouts[i];
                D3D12_TEXTURE_COPY_LOCATION source{};
                source.pResource = texture;
                source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                source.SubresourceIndex = i;
                list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            }
        },
        error);
    if (!done) {
        problems.add(item.name + ": " + error);
        return false;
    }
    std::vector<std::uint8_t> actual;
    if (!D3d12Tools::map_bytes(readback, 0, total, actual, error)) {
        problems.add(item.name + ": " + error);
        return false;
    }
    for (UINT i = 0; i < count; ++i) {
        for (UINT z = 0; z < layouts[i].Footprint.Depth; ++z) {
            for (UINT r = 0; r < rows[i]; ++r) {
                const UINT64 at = layouts[i].Offset + (static_cast<UINT64>(z) * rows[i] + r) * layouts[i].Footprint.RowPitch;
                if (std::memcmp(expected.data() + at, actual.data() + at, static_cast<std::size_t>(row_sizes[i])) != 0) {
                    problems.add(item.name + " subresource " + std::to_string(i) + " slice " + std::to_string(z) + " row " + std::to_string(r) + " differs after the round trip");
                    ArtifactWriter writer(context);
                    writer.bytes(Kit::sanitize(item.name) + ".expected", expected.data(), expected.size());
                    writer.bytes(Kit::sanitize(item.name) + ".actual", actual.data(), actual.size());
                    return false;
                }
            }
        }
    }
    rig.release(readback);
    rig.release(upload);
    rig.release(texture);
    return true;
}

}  // namespace

Outcome CopyChecks::footprints(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::vector<FootprintCase> cases;
        struct Entry {
            DXGI_FORMAT format;
            const char* name;
            UINT width;
            UINT height;
        };
        const Entry entries[] = {
            {DXGI_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8", 37, 21}, {DXGI_FORMAT_B8G8R8A8_UNORM, "B8G8R8A8", 37, 21},
            {DXGI_FORMAT_R8_UNORM, "R8", 37, 21},            {DXGI_FORMAT_R16G16_FLOAT, "R16G16F", 37, 21},
            {DXGI_FORMAT_R32G32B32A32_UINT, "R32G32B32A32", 37, 21}, {DXGI_FORMAT_R10G10B10A2_UNORM, "R10G10B10A2", 37, 21},
            {DXGI_FORMAT_BC1_UNORM, "BC1", 36, 20},          {DXGI_FORMAT_BC3_UNORM, "BC3", 36, 20},
            {DXGI_FORMAT_BC5_UNORM, "BC5", 36, 20},          {DXGI_FORMAT_BC7_UNORM, "BC7", 36, 20},
        };
        for (const Entry& entry : entries) {
            D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
            support.Format = entry.format;
            if (FAILED(rig.device()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) ||
                (support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) == 0) {
                context.note(std::string(entry.name) + " has no 2D texture support; skipped");
                continue;
            }
            cases.push_back({std::string(entry.name) + " 2D mips", D3d12Tools::texture_desc(entry.width, entry.height, entry.format, D3D12_RESOURCE_FLAG_NONE, 4, 1)});
            cases.push_back({std::string(entry.name) + " array of 3 with mips", D3d12Tools::texture_desc(entry.width, entry.height, entry.format, D3D12_RESOURCE_FLAG_NONE, 4, 3)});
        }
        cases.push_back({"R8G8B8A8 3D with mips", D3d12Tools::volume_desc(16, 8, 6, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE, 3)});
        cases.push_back({"R8 3D", D3d12Tools::volume_desc(17, 9, 5, DXGI_FORMAT_R8_UNORM, D3D12_RESOURCE_FLAG_NONE, 1)});
        for (const FootprintCase& item : cases) {
            static_cast<void>(footprint_case(context, rig, problems, item));
        }
        context.measure("d3d12.footprint_cases", static_cast<long long>(cases.size()));
        return {};
    });
}

Outcome CopyChecks::region_variants(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::string error;
        {
            const UINT64 size = 8192;
            ID3D12Resource* source = D3d12Tools::device_buffer(rig, size, D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12Resource* destination = D3d12Tools::device_buffer(rig, size, D3D12_RESOURCE_STATE_COPY_DEST);
            const std::vector<std::uint8_t> a = Kit::bytes(401, static_cast<std::size_t>(size));
            const std::vector<std::uint8_t> b = Kit::bytes(402, static_cast<std::size_t>(size));
            std::vector<std::uint8_t> back;
            if (source == nullptr || destination == nullptr || !D3d12Tools::write_buffer(rig, source, D3D12_RESOURCE_STATE_COPY_DEST, 0, a.data(), size, error) ||
                !D3d12Tools::write_buffer(rig, destination, D3D12_RESOURCE_STATE_COPY_DEST, 0, b.data(), size, error) ||
                !rig.direct().run_wait(
                    [&](ID3D12GraphicsCommandList* list) {
                        D3d12Tools::move(list, source, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
                        list->CopyBufferRegion(destination, 13, source, 1001, 3001);
                    },
                    error) ||
                !D3d12Tools::read_buffer(rig, destination, D3D12_RESOURCE_STATE_COPY_DEST, 0, size, back, error)) {
                problems.add("CopyBufferRegion: " + error);
            } else {
                std::vector<std::uint8_t> expected = b;
                for (std::size_t i = 0; i < 3001 && 13 + i < expected.size(); ++i) {
                    expected[13 + i] = a[1001 + i];
                }
                static_cast<void>(Kit::same_bytes(context, problems, "CopyBufferRegion with odd offsets", expected.data(), back.data(), expected.size()));
            }
            ID3D12Resource* whole = D3d12Tools::device_buffer(rig, size, D3D12_RESOURCE_STATE_COPY_DEST);
            if (whole == nullptr || !rig.direct().run_wait([&](ID3D12GraphicsCommandList* list) { list->CopyResource(whole, source); }, error) ||
                !D3d12Tools::read_buffer(rig, whole, D3D12_RESOURCE_STATE_COPY_DEST, 0, size, back, error)) {
                problems.add("CopyResource of a buffer: " + error);
            } else {
                static_cast<void>(Kit::same_bytes(context, problems, "CopyResource of a buffer", a.data(), back.data(), a.size()));
            }
        }
        const UINT size = 32;
        const D3D12_RESOURCE_DESC rgba = D3d12Tools::texture_desc(size, size, DXGI_FORMAT_R8G8B8A8_UNORM);
        auto pixels = [&](std::uint64_t seed) {
            std::vector<std::uint8_t> out(size * size * 4);
            Pattern::fill_pixels(seed, size, size, size * 4, out.data());
            return out;
        };
        {
            ID3D12Resource* source = D3d12Tools::texture(rig, rgba, D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12Resource* destination = D3d12Tools::texture(rig, rgba, D3D12_RESOURCE_STATE_COPY_DEST);
            const std::vector<std::uint8_t> a = pixels(410);
            const std::vector<std::uint8_t> b = pixels(411);
            TexelImage back;
            if (source == nullptr || destination == nullptr || !D3d12Tools::write_texture(rig, source, D3D12_RESOURCE_STATE_COPY_DEST, 0, a.data(), error) ||
                !D3d12Tools::write_texture(rig, destination, D3D12_RESOURCE_STATE_COPY_DEST, 0, b.data(), error) ||
                !rig.direct().run_wait(
                    [&](ID3D12GraphicsCommandList* list) {
                        D3d12Tools::move(list, source, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
                        D3D12_TEXTURE_COPY_LOCATION to{};
                        to.pResource = destination;
                        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                        D3D12_TEXTURE_COPY_LOCATION from{};
                        from.pResource = source;
                        from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                        const D3D12_BOX box{5, 3, 0, 17, 14, 1};
                        list->CopyTextureRegion(&to, 9, 11, 0, &from, &box);
                    },
                    error) ||
                !D3d12Tools::read_texture(rig, destination, D3D12_RESOURCE_STATE_COPY_DEST, 0, back, error)) {
                problems.add("CopyTextureRegion with a box: " + error);
            } else {
                std::vector<std::uint8_t> expected = b;
                for (UINT y = 0; y < 11; ++y) {
                    std::memcpy(expected.data() + ((11 + y) * size + 9) * 4, a.data() + ((3 + y) * size + 5) * 4, 12 * 4);
                }
                static_cast<void>(Kit::same_bytes(context, problems, "CopyTextureRegion with a box", expected.data(), back.data.data(), expected.size()));
            }
        }
        {
            ID3D12Resource* destination = D3d12Tools::texture(rig, rgba, D3D12_RESOURCE_STATE_COPY_DEST);
            const std::vector<std::uint8_t> b = pixels(412);
            const UINT patch_width = 16;
            const UINT patch_height = 8;
            const UINT pitch = 256;
            ID3D12Resource* upload = D3d12Tools::upload(rig, static_cast<UINT64>(pitch) * patch_height);
            TexelImage back;
            if (destination == nullptr || upload == nullptr || !D3d12Tools::write_texture(rig, destination, D3D12_RESOURCE_STATE_COPY_DEST, 0, b.data(), error)) {
                problems.add("buffer to texture box copy setup: " + error);
            } else {
                void* mapped = nullptr;
                const D3D12_RANGE none{0, 0};
                upload->Map(0, &none, &mapped);
                std::vector<std::uint8_t> patch(static_cast<std::size_t>(pitch) * patch_height, 0);
                for (UINT y = 0; y < patch_height; ++y) {
                    Pattern::fill(430 + y, 0, patch.data() + static_cast<std::size_t>(y) * pitch, patch_width * 4);
                }
                std::memcpy(mapped, patch.data(), patch.size());
                upload->Unmap(0, nullptr);
                const bool done = rig.direct().run_wait(
                    [&](ID3D12GraphicsCommandList* list) {
                        D3D12_TEXTURE_COPY_LOCATION to{};
                        to.pResource = destination;
                        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                        D3D12_TEXTURE_COPY_LOCATION from{};
                        from.pResource = upload;
                        from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                        from.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, patch_width, patch_height, 1, pitch};
                        const D3D12_BOX box{2, 1, 0, 14, 7, 1};
                        list->CopyTextureRegion(&to, 4, 6, 0, &from, &box);
                    },
                    error);
                if (!done || !D3d12Tools::read_texture(rig, destination, D3D12_RESOURCE_STATE_COPY_DEST, 0, back, error)) {
                    problems.add("buffer to texture box copy: " + error);
                } else {
                    std::vector<std::uint8_t> expected = b;
                    for (UINT y = 0; y < 6; ++y) {
                        std::memcpy(expected.data() + ((6 + y) * size + 4) * 4, patch.data() + (static_cast<std::size_t>(1 + y) * pitch) + 2 * 4, 12 * 4);
                    }
                    static_cast<void>(Kit::same_bytes(context, problems, "buffer to texture copy with a source box", expected.data(), back.data.data(), expected.size()));
                }
            }
        }
        {
            const D3D12_RESOURCE_DESC chain = D3d12Tools::texture_desc(size, size, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE, 3, 2);
            ID3D12Resource* source = D3d12Tools::texture(rig, chain, D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12Resource* destination = D3d12Tools::texture(rig, chain, D3D12_RESOURCE_STATE_COPY_DEST);
            if (source == nullptr || destination == nullptr) {
                problems.add("creating the mip and array textures for CopyResource failed");
            } else {
                std::vector<std::vector<std::uint8_t>> data;
                bool ok = true;
                for (UINT sub = 0; sub < 6 && ok; ++sub) {
                    const UINT mip = sub % 3;
                    const UINT edge = size >> mip;
                    std::vector<std::uint8_t> level(static_cast<std::size_t>(edge) * edge * 4);
                    Pattern::fill_pixels(450 + sub, edge, edge, edge * 4, level.data());
                    ok = D3d12Tools::write_texture(rig, source, D3D12_RESOURCE_STATE_COPY_DEST, sub, level.data(), error);
                    data.push_back(std::move(level));
                }
                ok = ok && rig.direct().run_wait(
                                   [&](ID3D12GraphicsCommandList* list) {
                                       D3d12Tools::move(list, source, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
                                       list->CopyResource(destination, source);
                                   },
                                   error);
                for (UINT sub = 0; sub < 6 && ok; ++sub) {
                    TexelImage back;
                    if (!D3d12Tools::read_texture(rig, destination, D3D12_RESOURCE_STATE_COPY_DEST, sub, back, error)) {
                        ok = false;
                        break;
                    }
                    static_cast<void>(Kit::same_bytes(context, problems, "CopyResource subresource " + std::to_string(sub), data[sub].data(), back.data.data(), data[sub].size()));
                }
                if (!ok) {
                    problems.add("CopyResource of mips and array slices: " + error);
                }
            }
        }
        {
            const std::vector<std::uint8_t> a = pixels(460);
            struct Family {
                DXGI_FORMAT from;
                DXGI_FORMAT to;
                const char* name;
            };
            const Family families[] = {
                {DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM, "TYPELESS to UNORM"},
                {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UINT, "UNORM to UINT"},
                {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_TYPELESS, "UNORM_SRGB to TYPELESS"},
            };
            for (const Family& family : families) {
                ID3D12Resource* source = D3d12Tools::texture(rig, D3d12Tools::texture_desc(size, size, family.from), D3D12_RESOURCE_STATE_COPY_DEST);
                ID3D12Resource* destination = D3d12Tools::texture(rig, D3d12Tools::texture_desc(size, size, family.to), D3D12_RESOURCE_STATE_COPY_DEST);
                TexelImage back;
                if (source == nullptr || destination == nullptr || !D3d12Tools::write_texture(rig, source, D3D12_RESOURCE_STATE_COPY_DEST, 0, a.data(), error) ||
                    !rig.direct().run_wait(
                        [&](ID3D12GraphicsCommandList* list) {
                            D3d12Tools::move(list, source, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
                            list->CopyResource(destination, source);
                        },
                        error) ||
                    !D3d12Tools::read_texture(rig, destination, D3D12_RESOURCE_STATE_COPY_DEST, 0, back, error)) {
                    problems.add(std::string("cast copy ") + family.name + ": " + error);
                    continue;
                }
                static_cast<void>(Kit::same_bytes(context, problems, std::string("cast copy ") + family.name, a.data(), back.data.data(), a.size()));
            }
        }
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{};
        levels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        levels.SampleCount = 4;
        if (FAILED(rig.device()->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels))) || levels.NumQualityLevels == 0) {
            context.note("4x MSAA is unsupported for R8G8B8A8_UNORM; the resolve variants are skipped");
            return {};
        }
        const D3D12_RESOURCE_DESC msaa = D3d12Tools::texture_desc(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 1, 1, 4);
        ID3D12Resource* multisampled = D3d12Tools::texture(rig, msaa, D3D12_RESOURCE_STATE_RENDER_TARGET);
        ID3D12DescriptorHeap* rtv = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
        if (multisampled == nullptr || rtv == nullptr) {
            problems.add("creating the multisampled target failed");
            return {};
        }
        rig.device()->CreateRenderTargetView(multisampled, nullptr, rtv->GetCPUDescriptorHandleForHeapStart());
        const float color[4] = {0.2f, 0.6f, 1.0f, 0.0f};
        const std::uint32_t resolved = Kit::rgba(51, 153, 255, 0);
        if (!rig.direct().run_wait(
                [&](ID3D12GraphicsCommandList* list) {
                    list->ClearRenderTargetView(rtv->GetCPUDescriptorHandleForHeapStart(), color, 0, nullptr);
                    D3d12Tools::move(list, multisampled, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
                },
                error)) {
            problems.add("clearing the multisampled target failed: " + error);
            return {};
        }
        ID3D12Resource* single = D3d12Tools::texture(rig, D3d12Tools::texture_desc(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM), D3D12_RESOURCE_STATE_RESOLVE_DEST);
        TexelImage image;
        if (single == nullptr ||
            !rig.direct().run_wait([&](ID3D12GraphicsCommandList* list) { list->ResolveSubresource(single, 0, multisampled, 0, DXGI_FORMAT_R8G8B8A8_UNORM); }, error) ||
            !D3d12Tools::read_texture(rig, single, D3D12_RESOURCE_STATE_RESOLVE_DEST, 0, image, error)) {
            problems.add("ResolveSubresource: " + error);
        } else {
            std::string where;
            if (!Kit::uniform(image, resolved, where)) {
                problems.add("ResolveSubresource of a uniform clear: " + where);
            }
        }
        {
            ID3D12Resource* base = D3d12Tools::texture(rig, D3d12Tools::texture_desc(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM), D3D12_RESOURCE_STATE_COPY_DEST);
            std::vector<std::uint8_t> fill(16 * 16 * 4);
            Pattern::fill_pixels(470, 16, 16, 64, fill.data());
            bool region_done = false;
            if (base != nullptr && D3d12Tools::write_texture(rig, base, D3D12_RESOURCE_STATE_COPY_DEST, 0, fill.data(), error)) {
                bool available = true;
                region_done = rig.direct().run_wait(
                    [&](ID3D12GraphicsCommandList* list) {
                        Com<ID3D12GraphicsCommandList1> list1;
                        list->QueryInterface(__uuidof(ID3D12GraphicsCommandList1), list1.put_void());
                        if (!list1) {
                            available = false;
                            return;
                        }
                        D3d12Tools::move(list, base, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RESOLVE_DEST);
                        D3D12_RECT source_rect{0, 0, 8, 8};
                        list1->ResolveSubresourceRegion(base, 0, 8, 8, multisampled, 0, &source_rect, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_AVERAGE);
                    },
                    error);
                if (!available) {
                    context.note("ID3D12GraphicsCommandList1 is not exposed; ResolveSubresourceRegion is skipped");
                } else if (!region_done) {
                    problems.add("ResolveSubresourceRegion: " + error);
                } else if (!D3d12Tools::read_texture(rig, base, D3D12_RESOURCE_STATE_RESOLVE_DEST, 0, image, error)) {
                    problems.add("ResolveSubresourceRegion readback: " + error);
                } else {
                    for (UINT y = 0; y < 16; ++y) {
                        for (UINT x = 0; x < 16; ++x) {
                            const bool inside = x >= 8 && x < 16 && y >= 8 && y < 16;
                            std::uint32_t want = 0;
                            std::memcpy(&want, fill.data() + (static_cast<std::size_t>(y) * 16 + x) * 4, 4);
                            if (inside) {
                                want = resolved;
                            }
                            if (image.pixel(x, y) != want) {
                                problems.add("ResolveSubresourceRegion: pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is 0x" + Text::hex(image.pixel(x, y), 8) +
                                             ", expected 0x" + Text::hex(want, 8));
                                x = 16;
                                y = 16;
                            }
                        }
                    }
                }
            } else {
                problems.add("preparing the resolve destination failed: " + error);
            }
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::d3d12
