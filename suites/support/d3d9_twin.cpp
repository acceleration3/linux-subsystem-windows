#include "support/d3d9_twin.hpp"

#include "support/com_ptr.hpp"
#include "support/d3d11_tools.hpp"

namespace lsw::suites {

bool D3d9Twin::open(std::string& error) {
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    if (!warp.create_warp(&level, 1, 0, error)) {
        error = "the WARP oracle is unavailable: " + error;
        return false;
    }
    return true;
}

bool D3d9Twin::render(const TwinScene& scene, std::vector<SceneImage>& out, std::string& error) {
    ID3D11Device* device = warp.device;
    ID3D11DeviceContext* context = warp.context;
    out.clear();
    Com<ID3D11VertexShader> vs = D3d11Tools::vs(warp, scene.vs, error);
    Com<ID3D11PixelShader> ps = D3d11Tools::ps(warp, scene.ps, error);
    if (!vs || !ps) {
        return false;
    }
    std::vector<D3D11_INPUT_ELEMENT_DESC> layout;
    for (const TwinElement& element : scene.elements) {
        layout.push_back({element.semantic, 0, element.format, 0, element.offset, D3D11_INPUT_PER_VERTEX_DATA, 0});
    }
    Com<ID3D11InputLayout> input;
    const std::vector<std::uint8_t> code = D3d11Tools::vs_code(warp, scene.vs, error);
    if (code.empty() || FAILED(device->CreateInputLayout(layout.data(), static_cast<UINT>(layout.size()), code.data(), code.size(), input.put()))) {
        error += " (input layout)";
        return false;
    }
    const UINT vertex_bytes = static_cast<UINT>(scene.vertices.size());
    Com<ID3D11Buffer> vb = D3d11Tools::buffer(warp, vertex_bytes, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0, scene.vertices.data());
    Com<ID3D11Buffer> ib;
    if (!scene.indices.empty()) {
        ib = D3d11Tools::buffer(warp, static_cast<UINT>(scene.indices.size() * 2), D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER, 0, 0, 0, scene.indices.data());
    }
    std::vector<float> constants = scene.constants;
    constants.resize((constants.size() + 3) / 4 * 4 + 4, 0.0f);
    Com<ID3D11Buffer> cb = D3d11Tools::buffer(warp, static_cast<UINT>(constants.size() * 4), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, constants.data());
    if (!vb || !cb || (!scene.indices.empty() && !ib)) {
        error = "buffers for the scene cannot be created";
        return false;
    }
    std::vector<Com<ID3D11Texture2D>> targets;
    std::vector<Com<ID3D11RenderTargetView>> views;
    ID3D11RenderTargetView* raw_views[2] = {};
    Com<ID3D11Texture2D> multisampled;
    for (UINT i = 0; i < scene.targets; ++i) {
        targets.push_back(D3d11Tools::target(warp, scene.width, scene.height, DXGI_FORMAT_B8G8R8A8_UNORM, 0));
        if (!targets.back()) {
            error = "the render target cannot be created";
            return false;
        }
        if (scene.samples > 1 && i == 0) {
            multisampled = D3d11Tools::texture(warp, D3d11Tools::desc2d(scene.width, scene.height, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, 1, 1, scene.samples));
            if (!multisampled) {
                error = "the multisampled WARP target cannot be created";
                return false;
            }
            views.push_back(D3d11Tools::rtv(warp, multisampled.get()));
        } else {
            views.push_back(D3d11Tools::rtv(warp, targets.back().get()));
        }
        raw_views[i] = views.back().get();
        D3d11Tools::clear(warp, views.back().get(), scene.clear[0], scene.clear[1], scene.clear[2], scene.clear[3]);
    }
    std::vector<Com<ID3D11ShaderResourceView>> srvs;
    ID3D11ShaderResourceView* raw_srvs[8] = {};
    for (std::size_t i = 0; i < scene.textures.size() && i < 8; ++i) {
        const TwinTexture& source = scene.textures[i];
        D3D11_SUBRESOURCE_DATA data{source.bgra.data(), source.width * 4, 0};
        Com<ID3D11Texture2D> texture = D3d11Tools::texture(warp, D3d11Tools::desc2d(source.width, source.height, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_BIND_SHADER_RESOURCE), &data);
        srvs.push_back(D3d11Tools::srv(warp, texture.get()));
        raw_srvs[i] = srvs.back().get();
    }
    D3D11_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = scene.linear ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = sampler_desc.AddressV = sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    Com<ID3D11SamplerState> sampler;
    device->CreateSamplerState(&sampler_desc, sampler.put());
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = FALSE;
    raster.MultisampleEnable = scene.samples > 1 ? TRUE : FALSE;
    Com<ID3D11RasterizerState> raster_state;
    device->CreateRasterizerState(&raster, raster_state.put());
    D3D11_DEPTH_STENCIL_DESC depth{};
    Com<ID3D11DepthStencilState> depth_state;
    device->CreateDepthStencilState(&depth, depth_state.put());
    context->OMSetRenderTargets(scene.targets, raw_views, nullptr);
    context->OMSetDepthStencilState(depth_state.get(), 0);
    context->RSSetState(raster_state.get());
    D3d11Tools::set_viewport(warp, scene.width, scene.height);
    context->IASetInputLayout(input.get());
    ID3D11Buffer* vertex_buffer = vb.get();
    const UINT stride = scene.stride;
    const UINT offset = 0;
    context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
    if (ib) {
        context->IASetIndexBuffer(ib.get(), DXGI_FORMAT_R16_UINT, 0);
    }
    context->IASetPrimitiveTopology(scene.topology);
    context->VSSetShader(vs.get(), nullptr, 0);
    context->PSSetShader(ps.get(), nullptr, 0);
    ID3D11Buffer* constant_buffer = cb.get();
    context->VSSetConstantBuffers(0, 1, &constant_buffer);
    context->PSSetConstantBuffers(0, 1, &constant_buffer);
    ID3D11SamplerState* sampler_state = sampler.get();
    context->PSSetSamplers(0, 1, &sampler_state);
    context->PSSetShaderResources(0, 8, raw_srvs);
    if (ib) {
        context->DrawIndexed(static_cast<UINT>(scene.indices.size()), 0, 0);
    } else {
        context->Draw(scene.vertex_count, 0);
    }
    if (multisampled) {
        context->ResolveSubresource(targets[0].get(), 0, multisampled.get(), 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    }
    if (!warp.wait_idle(error)) {
        return false;
    }
    for (UINT i = 0; i < scene.targets; ++i) {
        SceneImage image;
        image.width = scene.width;
        image.height = scene.height;
        image.rgba = false;
        if (!D3d11Tools::read_pixels(warp, targets[i].get(), image.data, error)) {
            return false;
        }
        out.push_back(std::move(image));
    }
    D3d11Tools::reset_state(warp);
    return true;
}

void D3d9Twin::compare(Context& context, Problems& problems, const std::string& label, const SceneImage& actual,
                       const TwinScene& scene, const Tolerance& tolerance, UINT target) {
    std::vector<SceneImage> expected;
    std::string error;
    if (!render(scene, expected, error)) {
        problems.add(label + ": the WARP twin failed: " + error);
        return;
    }
    std::string detail;
    if (!OracleRig::judge(context, label, expected[target], actual, tolerance, detail)) {
        problems.add(detail + " (against WARP)");
    }
}

}  // namespace lsw::suites
