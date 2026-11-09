#include "render/d3d12/kit.hpp"

#include "render/d3d/convert.hpp"
#include "support/target_adapter.hpp"

#include <cstdlib>
#include <cstring>

namespace lsw::suites::render::d3d12 {

OpenStatus Kit::open(bool use_warp, const rr::CaseSpec& spec, std::string& error) {
    warp = use_warp;
    if (std::getenv("LSW_D3D12_DEBUG") != nullptr) {
        Com<ID3D12Debug> debug;
        if (SUCCEEDED(::D3D12GetDebugInterface(__uuidof(ID3D12Debug), debug.put_void()))) {
            debug->EnableDebugLayer();
        }
    }
    spec_variant = spec.variant();
    dxc = spec_variant >= 1;
    bool opened = false;
    if (warp) {
        name = "WARP12";
        opened = rig.open_warp(D3D_FEATURE_LEVEL_11_0, error);
    } else {
        AvAdapter adapter;
        if (!TargetAdapter::pick(adapter, error)) {
            return OpenStatus::Failed;
        }
        name = adapter.name;
        opened = rig.open(adapter, D3D_FEATURE_LEVEL_11_0, error);
    }
    if (!opened) {
        return OpenStatus::Failed;
    }
    if (!rig.open_queue(D3D12_COMMAND_LIST_TYPE_DIRECT, error)) {
        return OpenStatus::Failed;
    }
    if (spec.multiframe() && !swap.open(rig.direct().queue(), 128, 128, 3, DXGI_FORMAT_B8G8R8A8_UNORM, false, error)) {
        return OpenStatus::Failed;
    }
    if (dxc) {
        std::string dxc_error;
        compiler_ready = compiler.open(dxc_error);
        if (!compiler_ready) {
            error = "the DXC compiler is not available (" + dxc_error + ")";
            return OpenStatus::Unsupported;
        }
    }
    return OpenStatus::Ok;
}

bool Kit::end_frame(std::string& error) {
    if (!swap.present(error)) {
        return false;
    }
    D3d12Queue& queue = rig.direct();
    ring[ring_index] = queue.signal();
    ring_index = (ring_index + 1) % 3;
    return ring[ring_index] == 0 || queue.wait(ring[ring_index], error);
}

bool Kit::compile(const std::string& source, char stage, Bytes& code, std::string& error) {
    return compile_as(source, "main", stage == 'v' ? "vs" : "ps", code, error);
}

bool Kit::compile_as(const std::string& source, const char* entry, const char* stage, Bytes& code, std::string& error,
                     const std::vector<const wchar_t*>& arguments) {
    if (dxc) {
        const std::string target = std::string(stage) + "_6_" + std::to_string(shader_minor());
        const std::wstring wide_target(target.begin(), target.end());
        const std::string name_text(entry);
        const std::wstring wide_entry(name_text.begin(), name_text.end());
        std::string log;
        if (!compiler.compile(source, std::string(stage) == "lib" ? nullptr : wide_entry.c_str(), wide_target.c_str(), arguments, code, log)) {
            error = "DXC compile failed (" + target + "): " + log;
            return false;
        }
        return true;
    }
    return D3d12Tools::compile(source, entry, (std::string(stage) + "_5_1").c_str(), code, error);
}

bool Kit::submit(const D3d12Queue::Recorder& record, std::string& error) {
    std::uint64_t value = 0;
    return rig.direct().run(record, value, error);
}

bool Kit::kind_for(DXGI_FORMAT format, rr::PixelKind& kind) {
    switch (format) {
        case DXGI_FORMAT_R8_UNORM:
            kind = rr::PixelKind::R8Unorm;
            return true;
        case DXGI_FORMAT_R8G8_UNORM:
            kind = rr::PixelKind::Rg8Unorm;
            return true;
        case DXGI_FORMAT_R16_FLOAT:
            kind = rr::PixelKind::R16Float;
            return true;
        case DXGI_FORMAT_R16G16_FLOAT:
            kind = rr::PixelKind::Rg16Float;
            return true;
        case DXGI_FORMAT_R16_UNORM:
            kind = rr::PixelKind::R16Unorm;
            return true;
        case DXGI_FORMAT_R16_UINT:
            kind = rr::PixelKind::R16Uint;
            return true;
        case DXGI_FORMAT_R32G32_FLOAT:
            kind = rr::PixelKind::Rg32Float;
            return true;
        case DXGI_FORMAT_R16G16B16A16_UNORM:
            kind = rr::PixelKind::Rgba16Unorm;
            return true;
        case DXGI_FORMAT_R8G8B8A8_UINT:
            kind = rr::PixelKind::Rgba8Uint;
            return true;
        case DXGI_FORMAT_R16G16B16A16_UINT:
            kind = rr::PixelKind::Rgba16Uint;
            return true;
        case DXGI_FORMAT_R32_TYPELESS:
            kind = rr::PixelKind::R32Float;
            return true;
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R11G11B10_FLOAT:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_SNORM:
            kind = rr::PixelKind::Rgba8Unorm;
            return true;
        default:
            return Convert::kind_of(format, kind);
    }
}

bool Kit::read_image(ID3D12Resource* resource, D3D12_RESOURCE_STATES state, UINT subresource, DXGI_FORMAT format, rr::Image& out,
                     std::string& error) {
    TexelImage texels;
    if (!D3d12Tools::read_texture(rig, resource, state, subresource, texels, error)) {
        return false;
    }
    rr::PixelKind kind;
    if (!kind_for(format, kind)) {
        error = "no image kind for DXGI format " + std::to_string(static_cast<int>(format));
        return false;
    }
    rr::Image image(texels.width, texels.rows * texels.depth, kind);
    if (texels.row_bytes != image.stride() || texels.data.size() != image.bytes().size()) {
        error = "texel readback size " + std::to_string(texels.data.size()) + " does not match the image kind size " + std::to_string(image.bytes().size());
        return false;
    }
    std::memcpy(image.bytes().data(), texels.data.data(), image.bytes().size());
    out = std::move(image);
    return true;
}

bool Kit::make_target(Target& out, UINT width, UINT height, DXGI_FORMAT format, std::string& error) {
    D3D12_CLEAR_VALUE clear{};
    clear.Format = format;
    HRESULT result = S_OK;
    out.texture = D3d12Tools::texture(rig, D3d12Tools::texture_desc(width, height, format, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
                                      D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, &result);
    if (out.texture == nullptr) {
        error = "render target creation: " + D3d12Tools::hr(result);
        return false;
    }
    out.heap = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false, &result);
    if (out.heap == nullptr) {
        error = "RTV heap creation: " + D3d12Tools::hr(result);
        return false;
    }
    out.view = out.heap->GetCPUDescriptorHandleForHeapStart();
    device()->CreateRenderTargetView(out.texture, nullptr, out.view);
    out.format = format;
    out.width = width;
    out.height = height;
    return true;
}

bool Kit::make_depth(Depth& out, UINT width, UINT height, std::string& error) {
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;
    HRESULT result = S_OK;
    out.texture = D3d12Tools::texture(rig, D3d12Tools::texture_desc(width, height, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
                                      D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, &result);
    if (out.texture == nullptr) {
        error = "depth texture creation: " + D3d12Tools::hr(result);
        return false;
    }
    out.heap = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false, &result);
    if (out.heap == nullptr) {
        error = "DSV heap creation: " + D3d12Tools::hr(result);
        return false;
    }
    out.view = out.heap->GetCPUDescriptorHandleForHeapStart();
    device()->CreateDepthStencilView(out.texture, nullptr, out.view);
    out.format = DXGI_FORMAT_D32_FLOAT;
    out.width = width;
    out.height = height;
    return true;
}

ID3D12Resource* Kit::make_upload(const void* data, UINT64 size, std::string& error) {
    HRESULT result = S_OK;
    ID3D12Resource* buffer = D3d12Tools::upload(rig, size, &result);
    if (buffer == nullptr) {
        error = "upload buffer creation: " + D3d12Tools::hr(result);
        return nullptr;
    }
    if (data != nullptr) {
        void* mapped = nullptr;
        const D3D12_RANGE none{0, 0};
        result = buffer->Map(0, &none, &mapped);
        if (FAILED(result)) {
            error = "Map of the upload buffer: " + D3d12Tools::hr(result);
            return nullptr;
        }
        std::memcpy(mapped, data, static_cast<std::size_t>(size));
        buffer->Unmap(0, nullptr);
    }
    return buffer;
}

bool Kit::run(const D3d12Queue::Recorder& record, std::string& error) {
    if (rig.direct().run_wait(record, error)) {
        return true;
    }
    if (std::getenv("LSW_D3D12_DEBUG") != nullptr) {
        Com<ID3D12InfoQueue> queue = rig.as<ID3D12InfoQueue>();
        if (queue) {
            const UINT64 count = queue->GetNumStoredMessages();
            for (UINT64 i = 0; i < count && i < 8; ++i) {
                SIZE_T length = 0;
                queue->GetMessage(i, nullptr, &length);
                std::vector<char> storage(length);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                if (SUCCEEDED(queue->GetMessage(i, message, &length))) {
                    error += std::string(" | ") + message->pDescription;
                }
            }
        }
    }
    return false;
}

bool Kit::read_color(Target& target, rr::Image& out, std::string& error) {
    TexelImage texels;
    return D3d12Tools::read_texture(rig, target.texture, D3D12_RESOURCE_STATE_RENDER_TARGET, 0, texels, error) &&
           Convert::image_of(texels, target.format, out, error);
}

bool Kit::read_depth(Depth& depth, rr::Image& out, std::string& error) {
    TexelImage texels;
    return D3d12Tools::read_texture(rig, depth.texture, D3D12_RESOURCE_STATE_DEPTH_WRITE, 0, texels, error) &&
           Convert::image_of(texels, depth.format, out, error);
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC Kit::pipeline_desc(ID3D12RootSignature* signature, const Bytes& vs, const Bytes& ps,
                                                      const std::vector<DXGI_FORMAT>& targets, DXGI_FORMAT depth) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = D3d12Tools::graphics_desc(signature, vs, ps, targets.empty() ? DXGI_FORMAT_UNKNOWN : targets[0]);
    desc.NumRenderTargets = static_cast<UINT>(targets.size());
    for (std::size_t i = 0; i < targets.size(); ++i) {
        desc.RTVFormats[i] = targets[i];
        desc.BlendState.RenderTarget[i] = desc.BlendState.RenderTarget[0];
    }
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    desc.RasterizerState.FrontCounterClockwise = TRUE;
    if (depth != DXGI_FORMAT_UNKNOWN) {
        desc.DepthStencilState.DepthEnable = TRUE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        desc.DSVFormat = depth;
    }
    return desc;
}

}  // namespace lsw::suites::render::d3d12
