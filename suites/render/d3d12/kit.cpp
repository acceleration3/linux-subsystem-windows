#include "render/d3d12/kit.hpp"

#include "render/d3d/convert.hpp"
#include "support/target_adapter.hpp"

#include <cstring>

namespace lsw::suites::render::d3d12 {

OpenStatus Kit::open(bool use_warp, const rr::CaseSpec& spec, std::string& error) {
    warp = use_warp;
    dxc = spec.variant() == 1;
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
    if (dxc) {
        const wchar_t* target = stage == 'v' ? L"vs_6_0" : L"ps_6_0";
        std::string log;
        if (!compiler.compile(source, L"main", target, code, log)) {
            error = "DXC compile failed: " + log;
            return false;
        }
        return true;
    }
    return D3d12Tools::compile(source, "main", stage == 'v' ? "vs_5_1" : "ps_5_1", code, error);
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

bool Kit::run(const D3d12Queue::Recorder& record, std::string& error) { return rig.direct().run_wait(record, error); }

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
