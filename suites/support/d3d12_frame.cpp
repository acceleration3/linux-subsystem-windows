#include "support/d3d12_frame.hpp"

namespace lsw::suites {

bool D3d12Frame::open(D3d12Rig& owner, UINT w, UINT h, std::string& error, DXGI_FORMAT fmt, bool with_depth, UINT samples) {
    rig = &owner;
    width = w;
    height = h;
    format = fmt;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = fmt;
    HRESULT result = S_OK;
    target = D3d12Tools::texture(owner, D3d12Tools::texture_desc(w, h, fmt, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 1, 1, samples),
                                 D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, &result);
    if (target == nullptr) {
        error = "creating the render target failed: " + D3d12Tools::hr(result);
        return false;
    }
    ID3D12DescriptorHeap* rtv_heap = D3d12Tools::descriptor_heap(owner, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false, &result);
    if (rtv_heap == nullptr) {
        error = "creating the RTV heap failed: " + D3d12Tools::hr(result);
        return false;
    }
    rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    owner.device()->CreateRenderTargetView(target, nullptr, rtv);
    if (with_depth) {
        D3D12_CLEAR_VALUE depth_clear{};
        depth_clear.Format = DXGI_FORMAT_D32_FLOAT;
        depth_clear.DepthStencil.Depth = 1.0f;
        depth = D3d12Tools::texture(owner,
                                    D3d12Tools::texture_desc(w, h, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, 1, 1, samples),
                                    D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth_clear, &result);
        ID3D12DescriptorHeap* dsv_heap = D3d12Tools::descriptor_heap(owner, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
        if (depth == nullptr || dsv_heap == nullptr) {
            error = "creating the depth buffer failed: " + D3d12Tools::hr(result);
            return false;
        }
        dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
        owner.device()->CreateDepthStencilView(depth, nullptr, dsv);
    }
    return true;
}

void D3d12Frame::bind(ID3D12GraphicsCommandList* list) const {
    list->OMSetRenderTargets(1, &rtv, FALSE, depth != nullptr ? &dsv : nullptr);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
}

void D3d12Frame::clear(ID3D12GraphicsCommandList* list, float r, float g, float b, float a) const {
    const float color[4] = {r, g, b, a};
    list->ClearRenderTargetView(rtv, color, 0, nullptr);
}

void D3d12Frame::clear_depth(ID3D12GraphicsCommandList* list, float value) const {
    list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, value, 0, 0, nullptr);
}

bool D3d12Frame::run(const D3d12Queue::Recorder& body, std::string& error) {
    return rig->direct().run_wait(
        [&](ID3D12GraphicsCommandList* list) {
            bind(list);
            body(list);
        },
        error);
}

bool D3d12Frame::read(TexelImage& out, std::string& error) {
    return D3d12Tools::read_texture(*rig, target, D3D12_RESOURCE_STATE_RENDER_TARGET, 0, out, error);
}

bool D3d12Frame::scene(SceneImage& out, std::string& error) {
    TexelImage image;
    if (!read(image, error)) {
        return false;
    }
    out.width = width;
    out.height = height;
    out.rgba = format == DXGI_FORMAT_R8G8B8A8_UNORM;
    out.data = std::move(image.data);
    return true;
}

}  // namespace lsw::suites
