#include "render/d3d12/textures.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::render::d3d12 {

namespace {

rr::Image words_image(const std::vector<std::uint32_t>& words, unsigned width) {
    const unsigned pixels = static_cast<unsigned>((words.size() + 3) / 4);
    rr::Image image(width, (pixels + width - 1) / width, rr::PixelKind::Rgba32Uint);
    std::memcpy(image.bytes().data(), words.data(), words.size() * 4);
    return image;
}

D3D12_TEXTURE_COPY_LOCATION sub_location(ID3D12Resource* resource, UINT subresource) {
    D3D12_TEXTURE_COPY_LOCATION location{};
    location.pResource = resource;
    location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    location.SubresourceIndex = subresource;
    return location;
}

D3D12_TEXTURE_COPY_LOCATION buffer_location(ID3D12Resource* resource, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint) {
    D3D12_TEXTURE_COPY_LOCATION location{};
    location.pResource = resource;
    location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    location.PlacedFootprint = footprint;
    return location;
}

ID3D12Resource* source_texture(Kit& kit, DXGI_FORMAT format, UINT size, UINT mips, UINT array, unsigned content, std::string& error, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    TextureSpec spec;
    spec.format = format;
    spec.width = size;
    spec.height = size;
    spec.mips = mips;
    spec.depth_or_array = array;
    spec.content = content;
    spec.flags = flags;
    return TextureFactory::create(kit, spec, true, error);
}

ID3D12Resource* blank_texture(Kit& kit, DXGI_FORMAT format, UINT size, UINT mips, UINT array, std::string& error, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    TextureSpec spec;
    spec.format = format;
    spec.width = size;
    spec.height = size;
    spec.mips = mips;
    spec.depth_or_array = array;
    spec.flags = flags;
    return TextureFactory::create(kit, spec, false, error);
}

void register_copies() {
    Cases::single(rr::CaseSpec("copy", "buffer_region_copies_with_offsets").tolerance(rr::Tolerance::exact()).tag("copy"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        std::vector<std::uint32_t> data(256);
        for (UINT k = 0; k < 256; ++k) {
            data[k] = k * 2654435761u;
        }
        ID3D12Resource* source = kit.make_upload(data.data(), 1024, error);
        HRESULT result = S_OK;
        ID3D12Resource* destination = D3d12Tools::device_buffer(kit.rig, 1024, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE, &result);
        if (source == nullptr || destination == nullptr) {
            return fail_with(error, error.empty() ? "buffer: " + D3d12Tools::hr(result) : error);
        }
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                list->CopyBufferRegion(destination, 0, source, 512, 256);
                list->CopyBufferRegion(destination, 256, source, 0, 128);
                list->CopyBufferRegion(destination, 400, source, 700, 12);
                list->CopyBufferRegion(destination, 512, destination, 0, 128);
            }, error)) {
            return false;
        }
        std::vector<std::uint32_t> words;
        if (!D3d12Tools::read_words(kit.rig, destination, D3D12_RESOURCE_STATE_COPY_DEST, 256, words, error)) {
            return false;
        }
        out.add("", words_image(words, 8));
        return true;
    });
    Cases::single(rr::CaseSpec("copy", "texture_region_box_to_offset").tolerance(rr::Tolerance::exact()).tag("copy"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        ID3D12Resource* source = source_texture(kit, DXGI_FORMAT_R8G8B8A8_UNORM, 64, 1, 1, 2, error);
        ID3D12Resource* destination = source_texture(kit, DXGI_FORMAT_R8G8B8A8_UNORM, 64, 1, 1, 3, error);
        if (source == nullptr || destination == nullptr) {
            return false;
        }
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                D3d12Tools::move(list, source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3d12Tools::move(list, destination, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                const D3D12_TEXTURE_COPY_LOCATION s = sub_location(source, 0);
                const D3D12_TEXTURE_COPY_LOCATION d = sub_location(destination, 0);
                const D3D12_BOX box{5, 7, 0, 41, 29, 1};
                list->CopyTextureRegion(&d, 13, 19, 0, &s, &box);
                const D3D12_BOX small{0, 0, 0, 9, 9, 1};
                list->CopyTextureRegion(&d, 50, 2, 0, &s, &small);
            }, error)) {
            return false;
        }
        rr::Image image;
        if (!kit.read_image(destination, D3D12_RESOURCE_STATE_COPY_DEST, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
            return false;
        }
        out.add("", std::move(image));
        return true;
    });
    Cases::single(rr::CaseSpec("copy", "texture_to_buffer_footprint_offset_and_padded_pitch").tolerance(rr::Tolerance::exact()).tag("copy"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        ID3D12Resource* texture = source_texture(kit, DXGI_FORMAT_R8G8B8A8_UNORM, 40, 1, 1, 2, error);
        if (texture == nullptr) {
            return false;
        }
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        footprint.Offset = 1024;
        footprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 40, 40, 1, 512};
        const UINT64 size = footprint.Offset + 512ull * 40;
        HRESULT result = S_OK;
        ID3D12Resource* readback = D3d12Tools::readback(kit.rig, size, &result);
        if (readback == nullptr) {
            return fail_with(error, "readback buffer: " + D3d12Tools::hr(result));
        }
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
                const D3D12_TEXTURE_COPY_LOCATION d = buffer_location(readback, footprint);
                const D3D12_TEXTURE_COPY_LOCATION s = sub_location(texture, 0);
                list->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
            }, error)) {
            return false;
        }
        Kit::Bytes bytes;
        if (!D3d12Tools::map_bytes(readback, footprint.Offset, 512ull * 40, bytes, error)) {
            return false;
        }
        rr::Image image(40, 40, rr::PixelKind::Rgba8Unorm);
        for (UINT y = 0; y < 40; ++y) {
            std::memcpy(image.row(y), bytes.data() + y * 512, 160);
        }
        out.add("", std::move(image));
        return true;
    });
    Cases::single(rr::CaseSpec("copy", "buffer_to_texture_footprint_extra_row_pitch").tolerance(rr::Tolerance::exact()).tag("copy"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        ID3D12Resource* texture = blank_texture(kit, DXGI_FORMAT_R8G8B8A8_UNORM, 48, 1, 1, error);
        if (texture == nullptr) {
            return false;
        }
        const UINT pitch = 512;
        const Texels::Bytes pixels = Texels::content(1, 48, 48);
        std::vector<std::uint8_t> staged(512ull * 48 + 512, 0xAB);
        for (UINT y = 0; y < 48; ++y) {
            std::memcpy(staged.data() + 512 + y * pitch, pixels.data() + y * 48 * 4, 48 * 4);
        }
        ID3D12Resource* upload = kit.make_upload(staged.data(), staged.size(), error);
        if (upload == nullptr) {
            return false;
        }
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        footprint.Offset = 512;
        footprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 48, 48, 1, pitch};
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                const D3D12_TEXTURE_COPY_LOCATION s = buffer_location(upload, footprint);
                const D3D12_TEXTURE_COPY_LOCATION d = sub_location(texture, 0);
                list->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
            }, error)) {
            return false;
        }
        rr::Image image;
        if (!kit.read_image(texture, D3D12_RESOURCE_STATE_COPY_DEST, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
            return false;
        }
        out.add("", std::move(image));
        return true;
    });
    Cases::single(rr::CaseSpec("copy", "bc1_aligned_block_box_copy").tolerance(rr::Tolerance::exact()).tag("copy"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        ID3D12Resource* source = source_texture(kit, DXGI_FORMAT_BC1_UNORM, 64, 1, 1, 0, error);
        ID3D12Resource* destination = source_texture(kit, DXGI_FORMAT_BC1_UNORM, 64, 1, 1, 0, error);
        if (source == nullptr || destination == nullptr) {
            return false;
        }
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                D3d12Tools::move(list, source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3d12Tools::move(list, destination, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                const D3D12_TEXTURE_COPY_LOCATION s = sub_location(source, 0);
                const D3D12_TEXTURE_COPY_LOCATION d = sub_location(destination, 0);
                const D3D12_BOX box{8, 12, 0, 40, 36, 1};
                list->CopyTextureRegion(&d, 20, 4, 0, &s, &box);
            }, error)) {
            return false;
        }
        TexelImage texels;
        if (!D3d12Tools::read_texture(kit.rig, destination, D3D12_RESOURCE_STATE_COPY_DEST, 0, texels, error)) {
            return false;
        }
        std::vector<std::uint32_t> words(texels.data.size() / 4);
        std::memcpy(words.data(), texels.data.data(), words.size() * 4);
        out.add("", words_image(words, 8));
        return true;
    });
    Cases::single(rr::CaseSpec("copy", "texture3d_box_copy").tolerance(rr::Tolerance::exact()).tag("copy"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        TextureSpec spec;
        spec.dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        spec.width = 16;
        spec.height = 16;
        spec.depth_or_array = 8;
        ID3D12Resource* source = TextureFactory::create(kit, spec, true, error);
        spec.content = 2;
        ID3D12Resource* destination = TextureFactory::create(kit, spec, true, error);
        if (source == nullptr || destination == nullptr) {
            return false;
        }
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                D3d12Tools::move(list, source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3d12Tools::move(list, destination, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                const D3D12_TEXTURE_COPY_LOCATION s = sub_location(source, 0);
                const D3D12_TEXTURE_COPY_LOCATION d = sub_location(destination, 0);
                const D3D12_BOX box{2, 3, 1, 12, 13, 6};
                list->CopyTextureRegion(&d, 1, 2, 2, &s, &box);
            }, error)) {
            return false;
        }
        rr::Image image;
        if (!kit.read_image(destination, D3D12_RESOURCE_STATE_COPY_DEST, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
            return false;
        }
        out.add("", std::move(image));
        return true;
    });
    Cases::single(rr::CaseSpec("copy", "copy_resource_mip_chain_and_array").tolerance(rr::Tolerance::exact()).tag("copy"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        ID3D12Resource* source = source_texture(kit, DXGI_FORMAT_R8G8B8A8_UNORM, 32, 4, 3, 0, error);
        ID3D12Resource* destination = blank_texture(kit, DXGI_FORMAT_R8G8B8A8_UNORM, 32, 4, 3, error);
        if (source == nullptr || destination == nullptr) {
            return false;
        }
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                D3d12Tools::move(list, source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3d12Tools::move(list, destination, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                list->CopyResource(destination, source);
            }, error)) {
            return false;
        }
        const UINT picks[3] = {0, 1 + 4 * 1, 2 + 4 * 2};
        for (UINT pick : picks) {
            rr::Image image;
            if (!kit.read_image(destination, D3D12_RESOURCE_STATE_COPY_DEST, pick, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
                return false;
            }
            out.add("sub" + std::to_string(pick), std::move(image));
        }
        return true;
    });
    Cases::single(rr::CaseSpec("copy", "copy_between_format_family_unorm_to_srgb").tolerance(rr::Tolerance::exact()).tag("copy"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        ID3D12Resource* source = source_texture(kit, DXGI_FORMAT_R8G8B8A8_UNORM, 32, 1, 1, 1, error);
        ID3D12Resource* destination = blank_texture(kit, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 32, 1, 1, error);
        if (source == nullptr || destination == nullptr) {
            return false;
        }
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                D3d12Tools::move(list, source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3d12Tools::move(list, destination, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                list->CopyResource(destination, source);
            }, error)) {
            return false;
        }
        rr::Image image;
        if (!kit.read_image(destination, D3D12_RESOURCE_STATE_COPY_DEST, 0, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, image, error)) {
            return false;
        }
        out.add("", std::move(image));
        return true;
    });
}

void register_clears() {
    Cases::single(rr::CaseSpec("copy", "clear_render_target_with_rects").tolerance(rr::Tolerance::tight(1)).tag("copy"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Surface target;
        if (!target.color(kit, 64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, error)) {
            return false;
        }
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                const float base[4] = {0.1f, 0.2f, 0.3f, 1};
                list->ClearRenderTargetView(target.view, base, 0, nullptr);
                const D3D12_RECT rects[3] = {{4, 4, 30, 20}, {20, 30, 60, 60}, {0, 50, 10, 64}};
                const float a[4] = {0.9f, 0.3f, 0.2f, 1};
                list->ClearRenderTargetView(target.view, a, 2, rects);
                const float b[4] = {0.2f, 0.8f, 0.4f, 0.5f};
                list->ClearRenderTargetView(target.view, b, 1, rects + 2);
            }, error)) {
            return false;
        }
        rr::Image image;
        if (!kit.read_image(target.resource, target.state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
            return false;
        }
        out.add("", std::move(image));
        return true;
    });
    Cases::single(rr::CaseSpec("copy", "clear_depth_stencil_with_rects").tolerance(rr::Tolerance::exact()).tag("copy"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Surface depth;
        if (!depth.depth(kit, 64, 64, DXGI_FORMAT_D32_FLOAT, error)) {
            return false;
        }
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                list->ClearDepthStencilView(depth.view, D3D12_CLEAR_FLAG_DEPTH, 0.75f, 0, 0, nullptr);
                const D3D12_RECT rects[2] = {{8, 8, 40, 24}, {30, 40, 64, 64}};
                list->ClearDepthStencilView(depth.view, D3D12_CLEAR_FLAG_DEPTH, 0.25f, 0, 2, rects);
                list->ClearDepthStencilView(depth.view, D3D12_CLEAR_FLAG_DEPTH, 0.5f, 0, 1, rects + 1);
            }, error)) {
            return false;
        }
        rr::Image image;
        if (!kit.read_image(depth.resource, depth.state, 0, DXGI_FORMAT_R32_TYPELESS, image, error)) {
            return false;
        }
        out.add("", std::move(image));
        return true;
    });
    for (int integer = 0; integer < 2; ++integer) {
        Cases::single(rr::CaseSpec("copy", integer != 0 ? "clear_uav_uint_with_rects" : "clear_uav_float_with_rects").tolerance(rr::Tolerance::exact()).tag("copy"),
                      [integer](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          const DXGI_FORMAT format = integer != 0 ? DXGI_FORMAT_R32G32B32A32_UINT : DXGI_FORMAT_R32G32B32A32_FLOAT;
                          ID3D12Resource* texture = blank_texture(kit, format, 32, 1, 1, error, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
                          HRESULT result = S_OK;
                          ID3D12DescriptorHeap* visible = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true, &result);
                          ID3D12DescriptorHeap* staging = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, false, &result);
                          if (texture == nullptr || visible == nullptr || staging == nullptr) {
                              return false;
                          }
                          D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
                          desc.Format = format;
                          desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                          kit.device()->CreateUnorderedAccessView(texture, nullptr, &desc, staging->GetCPUDescriptorHandleForHeapStart());
                          kit.device()->CreateUnorderedAccessView(texture, nullptr, &desc, visible->GetCPUDescriptorHandleForHeapStart());
                          if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                                  D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                                  list->SetDescriptorHeaps(1, &visible);
                                  const D3D12_RECT rects[2] = {{2, 2, 20, 12}, {10, 16, 30, 30}};
                                  if (integer != 0) {
                                      const UINT whole[4] = {1, 2, 3, 4};
                                      const UINT part[4] = {900, 800, 700, 600};
                                      list->ClearUnorderedAccessViewUint(visible->GetGPUDescriptorHandleForHeapStart(), staging->GetCPUDescriptorHandleForHeapStart(), texture, whole, 0, nullptr);
                                      const D3D12_RESOURCE_BARRIER between = D3d12Tools::uav_barrier(texture);
                                      list->ResourceBarrier(1, &between);
                                      list->ClearUnorderedAccessViewUint(visible->GetGPUDescriptorHandleForHeapStart(), staging->GetCPUDescriptorHandleForHeapStart(), texture, part, 2, rects);
                                  } else {
                                      const float whole[4] = {0.1f, 0.2f, 0.3f, 0.4f};
                                      const float part[4] = {0.9f, 0.8f, 0.7f, 0.6f};
                                      list->ClearUnorderedAccessViewFloat(visible->GetGPUDescriptorHandleForHeapStart(), staging->GetCPUDescriptorHandleForHeapStart(), texture, whole, 0, nullptr);
                                      const D3D12_RESOURCE_BARRIER between = D3d12Tools::uav_barrier(texture);
                                      list->ResourceBarrier(1, &between);
                                      list->ClearUnorderedAccessViewFloat(visible->GetGPUDescriptorHandleForHeapStart(), staging->GetCPUDescriptorHandleForHeapStart(), texture, part, 2, rects);
                                  }
                              }, error)) {
                              return false;
                          }
                          rr::Image image;
                          if (!kit.read_image(texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, format, image, error)) {
                              return false;
                          }
                          out.add("", std::move(image));
                          return true;
                      });
    }
}

const struct Registrar {
    Registrar() {
        register_copies();
        register_clears();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
