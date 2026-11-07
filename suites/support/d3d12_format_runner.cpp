#include "support/d3d12_format_runner.hpp"

#include "support/d3d11_tools.hpp"
#include "support/d3d12_frame.hpp"
#include "support/d3d12_root.hpp"
#include "support/d3d12_tools.hpp"

#include <cstring>

namespace lsw::suites {

bool D3d12FormatRunner::support(DXGI_FORMAT format, UINT& support1) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT data{};
    data.Format = format;
    support1 = 0;
    if (FAILED(rig_.device()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &data, sizeof(data)))) {
        return false;
    }
    support1 = static_cast<UINT>(data.Support1);
    return true;
}

bool D3d12FormatRunner::roundtrip(DXGI_FORMAT format, UINT width, UINT height, const Bytes& in, Bytes& out, std::string& error) {
    HRESULT result = S_OK;
    ID3D12Resource* texture =
        D3d12Tools::texture(rig_, D3d12Tools::texture_desc(width, height, format), D3D12_RESOURCE_STATE_COPY_DEST, nullptr, &result);
    if (texture == nullptr) {
        error = "creating the texture failed: " + D3d12Tools::hr(result);
        return false;
    }
    TexelImage image;
    if (!D3d12Tools::write_texture(rig_, texture, D3D12_RESOURCE_STATE_COPY_DEST, 0, in.data(), error) ||
        !D3d12Tools::read_texture(rig_, texture, D3D12_RESOURCE_STATE_COPY_DEST, 0, image, error)) {
        return false;
    }
    out = std::move(image.data);
    return true;
}

bool D3d12FormatRunner::clear(DXGI_FORMAT format, UINT width, UINT height, const float color[4], Bytes& out, std::string& error) {
    D3d12Frame frame;
    if (!frame.open(rig_, width, height, error, format)) {
        return false;
    }
    TexelImage image;
    if (!frame.run([&](ID3D12GraphicsCommandList* list) { frame.clear(list, color[0], color[1], color[2], color[3]); }, error) ||
        !frame.read(image, error)) {
        return false;
    }
    out = std::move(image.data);
    return true;
}

bool D3d12FormatRunner::draw(DXGI_FORMAT format, UINT width, UINT height, const std::string& type, const std::string& expression,
                             const float* clear_color, bool alpha_blend, Bytes& out, std::string& error,
                             const std::string& prelude) {
    D3d12Frame frame;
    D3d12Root root;
    ID3D12RootSignature* signature = root.build(rig_, error);
    D3d12Tools::Bytes vs;
    D3d12Tools::Bytes ps;
    if (signature == nullptr || !frame.open(rig_, width, height, error, format) ||
        !D3d12Tools::compile(D3d11Tools::kFullscreenVs, "main", "vs_5_1", vs, error) ||
        !D3d12Tools::compile(pixel_source(type, expression, prelude), "main", "ps_5_1", ps, error)) {
        return false;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = D3d12Tools::graphics_desc(signature, vs, ps, format);
    if (alpha_blend) {
        D3D12_RENDER_TARGET_BLEND_DESC& blend = desc.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTargetWriteMask = 0xf;
    }
    ID3D12PipelineState* state = D3d12Tools::graphics_pso(rig_, desc, error);
    if (state == nullptr) {
        return false;
    }
    TexelImage image;
    const bool drawn = frame.run(
        [&](ID3D12GraphicsCommandList* list) {
            if (clear_color != nullptr) {
                frame.clear(list, clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
            }
            list->SetGraphicsRootSignature(signature);
            list->SetPipelineState(state);
            list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            list->DrawInstanced(3, 1, 0, 0);
        },
        error);
    if (!drawn || !frame.read(image, error)) {
        return false;
    }
    out = std::move(image.data);
    return true;
}

bool D3d12FormatRunner::sample(DXGI_FORMAT texture_format, DXGI_FORMAT view_format, const std::string& type, UINT width, UINT height,
                               const Bytes& in, std::vector<std::uint32_t>& words, std::string& error) {
    HRESULT result = S_OK;
    ID3D12Resource* texture = D3d12Tools::texture(rig_, D3d12Tools::texture_desc(width, height, texture_format),
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr, &result);
    if (texture == nullptr) {
        error = "creating the texture failed: " + D3d12Tools::hr(result);
        return false;
    }
    const UINT64 bytes = static_cast<UINT64>(width) * height * 16;
    ID3D12Resource* output =
        D3d12Tools::device_buffer(rig_, bytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &result);
    ID3D12DescriptorHeap* heap = D3d12Tools::descriptor_heap(rig_, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    D3d12Root root;
    root.table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    root.uav(0);
    ID3D12RootSignature* signature = root.build(rig_, error);
    if (output == nullptr || heap == nullptr || signature == nullptr) {
        error = "creating the sampling resources failed: " + error + D3d12Tools::hr(result);
        return false;
    }
    ID3D12PipelineState* state = D3d12Tools::compute_from(rig_, signature, sample_source(type, width, height), error);
    if (state == nullptr || !D3d12Tools::write_texture(rig_, texture, D3D12_RESOURCE_STATE_COPY_DEST, 0, in.data(), error)) {
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = view_format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1;
    rig_.device()->CreateShaderResourceView(texture, &view, heap->GetCPUDescriptorHandleForHeapStart());
    const bool ran = rig_.direct().run_wait(
        [&](ID3D12GraphicsCommandList* list) {
            D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            ID3D12DescriptorHeap* heaps[] = {heap};
            list->SetDescriptorHeaps(1, heaps);
            list->SetComputeRootSignature(signature);
            list->SetPipelineState(state);
            list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
            list->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
            list->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
        },
        error);
    Bytes raw;
    if (!ran || !D3d12Tools::read_buffer(rig_, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, bytes, raw, error)) {
        return false;
    }
    words.resize(raw.size() / 4);
    std::memcpy(words.data(), raw.data(), words.size() * 4);
    return true;
}

}  // namespace lsw::suites
