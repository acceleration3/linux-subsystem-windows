#include "sanity/d3d11/render_checks.hpp"

#include "sanity/d3d11/kit.hpp"
#include "support/format_codec.hpp"
#include "support/oracle_rig.hpp"

#include <algorithm>
#include <cmath>

namespace lsw::suites::sanity::d3d11 {

namespace {

constexpr const char* kVertexSource =
    "cbuffer C : register(b0) { float z; float3 pad; }\n"
    "float4 main(float2 p : POSITION, uint inst : SV_InstanceID) : SV_Position { return float4(p + float2(inst * 0.5, 0), z, 1); }\n";

struct Geometry {
    Com<ID3D11VertexShader> vs;
    Com<ID3D11InputLayout> layout;
    Com<ID3D11Buffer> constants;

    [[nodiscard]] bool init(D3d11Device& d, std::string& error) {
        vs = D3d11Tools::vs(d, kVertexSource, error);
        const std::vector<std::uint8_t> code = D3d11Tools::vs_code(d, kVertexSource, error);
        const D3D11_INPUT_ELEMENT_DESC element = {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
        constants = D3d11Tools::buffer(d, 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, nullptr);
        if (!vs || code.empty() || !constants || FAILED(d.device->CreateInputLayout(&element, 1, code.data(), code.size(), layout.put()))) {
            return false;
        }
        ID3D11Buffer* raw = constants.get();
        d.context->VSSetConstantBuffers(0, 1, &raw);
        d.context->IASetInputLayout(layout.get());
        d.context->VSSetShader(vs.get(), nullptr, 0);
        set_depth(d, 0.5f);
        return true;
    }

    void set_depth(D3d11Device& d, float z) const {
        const float data[4] = {z, 0, 0, 0};
        d.context->UpdateSubresource(constants.get(), 0, nullptr, data, 0, 0);
    }
};

bool finish_image(D3d11Device& d, ID3D11Texture2D* target, UINT w, UINT h, SceneImage& out, std::string& error) {
    out.width = w;
    out.height = h;
    out.rgba = true;
    return D3d11Tools::read_pixels(d, target, out.data, error);
}

}  // namespace

Outcome RenderChecks::basic_stages(Context& context) {
    OracleRig rig;
    std::string error;
    if (!rig.open(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    OracleRig::Scene scene = [](D3d11Device& d, SceneImage& out, std::string& why) {
        Geometry geometry;
        Com<ID3D11PixelShader> white = Kit::color_ps(d, "1, 1, 1, 1", why);
        if (!white || !geometry.init(d, why)) {
            why += " (scene setup)";
            return false;
        }
        Com<ID3D11Texture2D> target = D3d11Tools::target(d, 160, 32);
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(d, target.get());
        D3d11Tools::clear(d, view.get(), 0, 0, 0, 1);
        ID3D11RenderTargetView* list[1] = {view.get()};
        d.context->OMSetRenderTargets(1, list, nullptr);
        d.context->PSSetShader(white.get(), nullptr, 0);
        const float points[] = {-0.6f, -0.6f, 0.6f, 0.6f, 0.0f, 0.0f};
        const float lines[] = {-0.7f, -0.5f, 0.7f, 0.5f, -0.7f, 0.5f, 0.7f, -0.5f};
        const float strip[] = {-0.7f, -0.7f, -0.7f, 0.7f, 0.0f, -0.7f, 0.7f, 0.7f, 0.7f, -0.7f};
        const float triangles[] = {-0.8f, -0.8f, -0.8f, 0.8f, 0.0f, -0.8f, 0.0f, -0.8f, 0.8f, 0.8f, 0.8f, -0.8f};
        const float quad[] = {-0.8f, -0.8f, -0.8f, 0.8f, 0.8f, -0.8f, 0.8f, 0.8f};
        const std::uint16_t quad_indices[] = {0, 1, 2, 2, 1, 3};
        struct Tile {
            D3D11_PRIMITIVE_TOPOLOGY topology;
            const float* data;
            UINT count;
            int mode;
        };
        const Tile tiles[] = {{D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, points, 3, 0},
                              {D3D11_PRIMITIVE_TOPOLOGY_LINELIST, lines, 4, 0},
                              {D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP, lines, 4, 0},
                              {D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, triangles, 6, 0},
                              {D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, strip, 5, 0},
                              {D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, quad, 6, 1},
                              {D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, quad, 6, 2},
                              {D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, quad, 6, 3},
                              {D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, quad, 6, 4},
                              {D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, triangles, 6, 5}};
        int index = 0;
        for (const Tile& tile : tiles) {
            D3D11_VIEWPORT vp{static_cast<float>(index % 5) * 32.0f, static_cast<float>(index / 5) * 16.0f, 32.0f, 16.0f, 0, 1};
            vp.Height = 16.0f;
            d.context->RSSetViewports(1, &vp);
            const UINT vertex_count = tile.mode == 1 || tile.mode == 2 || tile.mode == 3 || tile.mode == 4 ? 4 : tile.count;
            Com<ID3D11Buffer> vb = D3d11Tools::buffer(d, vertex_count * 8, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0, tile.data);
            UINT stride = 8;
            UINT offset = 0;
            ID3D11Buffer* raw = vb.get();
            d.context->IASetVertexBuffers(0, 1, &raw, &stride, &offset);
            d.context->IASetPrimitiveTopology(tile.topology);
            if (tile.mode == 0) {
                d.context->Draw(tile.count, 0);
            } else if (tile.mode == 1) {
                Com<ID3D11Buffer> ib = D3d11Tools::buffer(d, 12, D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER, 0, 0, 0, quad_indices);
                d.context->IASetIndexBuffer(ib.get(), DXGI_FORMAT_R16_UINT, 0);
                d.context->DrawIndexed(6, 0, 0);
            } else if (tile.mode == 2) {
                Com<ID3D11Buffer> ib = D3d11Tools::buffer(d, 12, D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER, 0, 0, 0, quad_indices);
                d.context->IASetIndexBuffer(ib.get(), DXGI_FORMAT_R16_UINT, 0);
                d.context->DrawIndexedInstanced(6, 2, 0, 0, 0);
            } else if (tile.mode == 3) {
                const std::uint32_t args[4] = {6, 1, 0, 0};
                Com<ID3D11Buffer> ib = D3d11Tools::buffer(d, 12, D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER, 0, 0, 0, quad_indices);
                Com<ID3D11Buffer> indirect = D3d11Tools::buffer(d, 20, D3D11_USAGE_DEFAULT, 0, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, 0, nullptr);
                const std::uint32_t indexed_args[5] = {6, 1, 0, 0, 0};
                d.context->UpdateSubresource(indirect.get(), 0, nullptr, indexed_args, 0, 0);
                d.context->IASetIndexBuffer(ib.get(), DXGI_FORMAT_R16_UINT, 0);
                d.context->DrawIndexedInstancedIndirect(indirect.get(), 0);
                static_cast<void>(args);
            } else if (tile.mode == 4) {
                const std::uint32_t args[4] = {4, 2, 0, 0};
                Com<ID3D11Buffer> indirect = D3d11Tools::buffer(d, 16, D3D11_USAGE_DEFAULT, 0, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, 0, nullptr);
                d.context->UpdateSubresource(indirect.get(), 0, nullptr, args, 0, 0);
                d.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                d.context->DrawInstancedIndirect(indirect.get(), 0);
            } else {
                const char* gs_source =
                    "struct O { float2 p : POSITION; };\n"
                    "float4 dummy() { return 0; }\n";
                static_cast<void>(gs_source);
                Com<ID3D11Buffer> captured = D3d11Tools::buffer(d, 6 * 8, D3D11_USAGE_DEFAULT, D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_VERTEX_BUFFER, 0, 0, 0, nullptr);
                const std::vector<std::uint8_t> code = D3d11Tools::vs_code(d, "float4 main(float2 p : POSITION) : POSITION { return float4(p, 0, 1); }", why);
                const D3D11_SO_DECLARATION_ENTRY entry = {0, "POSITION", 0, 0, 2, 0};
                const UINT stride_so = 8;
                Com<ID3D11GeometryShader> so;
                if (code.empty() || !captured || FAILED(d.device->CreateGeometryShaderWithStreamOutput(code.data(), code.size(), &entry, 1, &stride_so, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, so.put()))) {
                    why = "stream output shader for DrawAuto cannot be created: " + why;
                    return false;
                }
                Com<ID3D11VertexShader> pass = D3d11Tools::vs(d, "float4 main(float2 p : POSITION) : POSITION { return float4(p, 0, 1); }", why);
                (void)pass;
                ID3D11Buffer* targets[1] = {captured.get()};
                const UINT zero = 0;
                d.context->GSSetShader(so.get(), nullptr, 0);
                d.context->SOSetTargets(1, targets, &zero);
                d.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                d.context->Draw(6, 0);
                ID3D11Buffer* none[1] = {nullptr};
                d.context->SOSetTargets(1, none, &zero);
                d.context->GSSetShader(nullptr, nullptr, 0);
                d.context->VSSetShader(geometry.vs.get(), nullptr, 0);
                ID3D11Buffer* source = captured.get();
                d.context->IASetVertexBuffers(0, 1, &source, &stride, &offset);
                d.context->DrawAuto();
            }
        }
        return finish_image(d, target.get(), 160, 32, out, why);
    };
    rig.compare(context, problems, "basic_stages", scene, Tolerance::bytes(2, 0.01));
    return problems.outcome();
}

Outcome RenderChecks::states(Context& context) {
    OracleRig rig;
    std::string error;
    if (!rig.open(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
    rig.av.device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options));
    const bool logic_ops = options.OutputMergerLogicOp != FALSE;
    OracleRig::Scene blend_scene = [&](D3d11Device& d, SceneImage& out, std::string& why) {
        Geometry geometry;
        Com<ID3D11PixelShader> half = Kit::color_ps(d, "0.8, 0.4, 0.2, 0.5", why);
        Com<ID3D11PixelShader> dual = D3d11Tools::ps(d, "struct O { float4 a : SV_Target0; float4 b : SV_Target1; };\nO main() { O o; o.a = float4(0.9, 0.2, 0.1, 1); o.b = float4(0.25, 0.5, 0.75, 0.5); return o; }", why);
        if (!half || !geometry.init(d, why)) {
            return false;
        }
        const D3D11_BLEND_OP ops[] = {D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_SUBTRACT, D3D11_BLEND_OP_REV_SUBTRACT, D3D11_BLEND_OP_MIN, D3D11_BLEND_OP_MAX};
        const D3D11_BLEND factors[][2] = {{D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA}, {D3D11_BLEND_ONE, D3D11_BLEND_ONE}, {D3D11_BLEND_ZERO, D3D11_BLEND_SRC_COLOR},
                                          {D3D11_BLEND_DEST_COLOR, D3D11_BLEND_INV_DEST_ALPHA}, {D3D11_BLEND_BLEND_FACTOR, D3D11_BLEND_INV_BLEND_FACTOR}, {D3D11_BLEND_SRC_ALPHA_SAT, D3D11_BLEND_ZERO}};
        Com<ID3D11Texture2D> target = D3d11Tools::target(d, 128, 64);
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(d, target.get());
        D3d11Tools::clear(d, view.get(), 0.1f, 0.3f, 0.5f, 0.75f);
        ID3D11RenderTargetView* list[1] = {view.get()};
        d.context->OMSetRenderTargets(1, list, nullptr);
        d.context->PSSetShader(half.get(), nullptr, 0);
        d.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        const float quad[] = {-1, 1, -1, -1, 1, 1, 1, -1};
        Com<ID3D11Buffer> vb = D3d11Tools::buffer(d, 32, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0, quad);
        UINT stride = 8;
        UINT offset = 0;
        ID3D11Buffer* raw = vb.get();
        d.context->IASetVertexBuffers(0, 1, &raw, &stride, &offset);
        const float factor[4] = {0.3f, 0.6f, 0.9f, 0.2f};
        int tile = 0;
        for (D3D11_BLEND_OP op : ops) {
            for (const auto& f : factors) {
                D3D11_BLEND_DESC desc{};
                desc.RenderTarget[0].BlendEnable = TRUE;
                desc.RenderTarget[0].SrcBlend = f[0];
                desc.RenderTarget[0].DestBlend = f[1];
                desc.RenderTarget[0].BlendOp = op;
                desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
                desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
                desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MAX;
                desc.RenderTarget[0].RenderTargetWriteMask = 0xf;
                Com<ID3D11BlendState> state;
                if (FAILED(d.device->CreateBlendState(&desc, state.put()))) {
                    why = "a blend state cannot be created";
                    return false;
                }
                D3D11_VIEWPORT vp{static_cast<float>(tile % 8) * 16.0f, static_cast<float>(tile / 8) * 16.0f, 16.0f, 16.0f, 0, 1};
                d.context->RSSetViewports(1, &vp);
                d.context->OMSetBlendState(state.get(), factor, 0xffffffff);
                d.context->Draw(4, 0);
                ++tile;
            }
        }
        if (dual) {
            D3D11_BLEND_DESC desc{};
            desc.RenderTarget[0].BlendEnable = TRUE;
            desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC1_COLOR;
            desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC1_COLOR;
            desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC1_ALPHA;
            desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC1_ALPHA;
            desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            desc.RenderTarget[0].RenderTargetWriteMask = 0xf;
            Com<ID3D11BlendState> state;
            if (SUCCEEDED(d.device->CreateBlendState(&desc, state.put()))) {
                D3D11_VIEWPORT vp{0, 48, 16, 16, 0, 1};
                d.context->RSSetViewports(1, &vp);
                d.context->OMSetBlendState(state.get(), factor, 0xffffffff);
                d.context->PSSetShader(dual.get(), nullptr, 0);
                d.context->Draw(4, 0);
            }
        }
        if (logic_ops) {
            Com<ID3D11Device1> device1 = Kit::query<ID3D11Device1>(d.device);
            D3D11_BLEND_DESC1 desc{};
            desc.RenderTarget[0].LogicOpEnable = TRUE;
            desc.RenderTarget[0].LogicOp = D3D11_LOGIC_OP_XOR;
            desc.RenderTarget[0].RenderTargetWriteMask = 0xf;
            Com<ID3D11BlendState1> state;
            if (device1 && SUCCEEDED(device1->CreateBlendState1(&desc, state.put()))) {
                Com<ID3D11DeviceContext1> ctx1 = Kit::query<ID3D11DeviceContext1>(d.context);
                D3D11_VIEWPORT vp{16, 48, 16, 16, 0, 1};
                d.context->RSSetViewports(1, &vp);
                ctx1->OMSetBlendState(state.get(), factor, 0xffffffff);
                d.context->PSSetShader(half.get(), nullptr, 0);
                d.context->Draw(4, 0);
            }
        }
        return finish_image(d, target.get(), 128, 64, out, why);
    };
    rig.compare(context, problems, "states_blend", blend_scene, Tolerance::bytes(3));
    OracleRig::Scene depth_raster_scene = [](D3d11Device& d, SceneImage& out, std::string& why) {
        Geometry geometry;
        Com<ID3D11PixelShader> red = Kit::color_ps(d, "1, 0, 0, 1", why);
        Com<ID3D11PixelShader> green = Kit::color_ps(d, "0, 1, 0, 1", why);
        if (!red || !green || !geometry.init(d, why)) {
            return false;
        }
        Com<ID3D11Texture2D> target = D3d11Tools::target(d, 128, 64);
        Com<ID3D11Texture2D> depth = D3d11Tools::texture(d, D3d11Tools::desc2d(16, 16, DXGI_FORMAT_D24_UNORM_S8_UINT, D3D11_BIND_DEPTH_STENCIL));
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(d, target.get());
        D3d11Tools::clear(d, view.get(), 0, 0, 0, 1);
        d.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const float tri[] = {-0.9f, 0.9f, 0.9f, 0.9f, -0.9f, -0.9f};
        const float tri_cw[] = {-0.9f, 0.9f, -0.9f, -0.9f, 0.9f, 0.9f};
        Com<ID3D11Buffer> vb = D3d11Tools::buffer(d, 24, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0, tri);
        Com<ID3D11Buffer> vb_cw = D3d11Tools::buffer(d, 24, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0, tri_cw);
        UINT stride = 8;
        UINT offset = 0;
        int tile = 0;
        auto viewport = [&](int index) {
            D3D11_VIEWPORT vp{static_cast<float>(index % 8) * 16.0f, static_cast<float>(index / 8) * 16.0f, 16.0f, 16.0f, 0, 1};
            d.context->RSSetViewports(1, &vp);
        };
        for (D3D11_COMPARISON_FUNC func : {D3D11_COMPARISON_NEVER, D3D11_COMPARISON_LESS, D3D11_COMPARISON_EQUAL, D3D11_COMPARISON_LESS_EQUAL, D3D11_COMPARISON_GREATER, D3D11_COMPARISON_NOT_EQUAL, D3D11_COMPARISON_GREATER_EQUAL, D3D11_COMPARISON_ALWAYS}) {
            for (int write : {0, 1}) {
                D3D11_DEPTH_STENCIL_DESC desc{};
                desc.DepthEnable = TRUE;
                desc.DepthFunc = func;
                desc.DepthWriteMask = write != 0 ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
                desc.StencilEnable = TRUE;
                desc.StencilReadMask = 0xff;
                desc.StencilWriteMask = 0xff;
                desc.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_INCR, D3D11_COMPARISON_ALWAYS};
                desc.BackFace = desc.FrontFace;
                Com<ID3D11DepthStencilState> state;
                if (FAILED(d.device->CreateDepthStencilState(&desc, state.put()))) {
                    why = "a depth stencil state cannot be created";
                    return false;
                }
                Com<ID3D11DepthStencilView> dsv;
                d.device->CreateDepthStencilView(depth.get(), nullptr, dsv.put());
                d.context->ClearDepthStencilView(dsv.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
                ID3D11RenderTargetView* list[1] = {view.get()};
                d.context->OMSetRenderTargets(1, list, dsv.get());
                ID3D11Buffer* raw = vb.get();
                d.context->IASetVertexBuffers(0, 1, &raw, &stride, &offset);
                viewport(tile);
                D3D11_DEPTH_STENCIL_DESC always{};
                always.DepthEnable = TRUE;
                always.DepthFunc = D3D11_COMPARISON_ALWAYS;
                always.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
                Com<ID3D11DepthStencilState> setup;
                d.device->CreateDepthStencilState(&always, setup.put());
                geometry.set_depth(d, 0.5f);
                d.context->OMSetDepthStencilState(setup.get(), 0);
                d.context->PSSetShader(green.get(), nullptr, 0);
                d.context->Draw(3, 0);
                geometry.set_depth(d, 0.4f + 0.1f * static_cast<float>(write));
                d.context->OMSetDepthStencilState(state.get(), 1);
                d.context->PSSetShader(red.get(), nullptr, 0);
                d.context->Draw(3, 0);
                ++tile;
            }
        }
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.DepthClipEnable = TRUE;
        ID3D11RenderTargetView* list[1] = {view.get()};
        d.context->OMSetRenderTargets(1, list, nullptr);
        d.context->OMSetDepthStencilState(nullptr, 0);
        geometry.set_depth(d, 0.5f);
        d.context->PSSetShader(green.get(), nullptr, 0);
        for (D3D11_CULL_MODE cull : {D3D11_CULL_NONE, D3D11_CULL_FRONT, D3D11_CULL_BACK}) {
            for (BOOL ccw : {FALSE, TRUE}) {
                for (BOOL wire : {FALSE, TRUE}) {
                    raster.CullMode = cull;
                    raster.FrontCounterClockwise = ccw;
                    raster.FillMode = wire != FALSE ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
                    Com<ID3D11RasterizerState> state;
                    d.device->CreateRasterizerState(&raster, state.put());
                    d.context->RSSetState(state.get());
                    viewport(tile);
                    ID3D11Buffer* raw = vb_cw.get();
                    d.context->IASetVertexBuffers(0, 1, &raw, &stride, &offset);
                    d.context->Draw(3, 0);
                    ++tile;
                }
            }
        }
        raster = {};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.ScissorEnable = TRUE;
        raster.DepthClipEnable = TRUE;
        Com<ID3D11RasterizerState> scissor;
        d.device->CreateRasterizerState(&raster, scissor.put());
        d.context->RSSetState(scissor.get());
        const D3D11_RECT rect = {0, 0, 0, 0};
        static_cast<void>(rect);
        const D3D11_RECT cut = {static_cast<LONG>(tile % 8) * 16 + 4, static_cast<LONG>(tile / 8) * 16 + 4, static_cast<LONG>(tile % 8) * 16 + 12, static_cast<LONG>(tile / 8) * 16 + 12};
        d.context->RSSetScissorRects(1, &cut);
        viewport(tile);
        d.context->Draw(3, 0);
        return finish_image(d, target.get(), 128, 64, out, why);
    };
    rig.compare(context, problems, "states_depth_raster", depth_raster_scene, Tolerance::bytes(2, 0.01));
    OracleRig::Scene sampler_scene = [](D3d11Device& d, SceneImage& out, std::string& why) {
        std::vector<std::uint32_t> texels(16);
        for (int i = 0; i < 16; ++i) {
            texels[static_cast<std::size_t>(i)] = Kit::rgba(40 + (i % 4) * 60, 30 + (i / 4) * 60, (i * 37) & 255, 255);
        }
        D3D11_SUBRESOURCE_DATA init{texels.data(), 16, 0};
        Com<ID3D11Texture2D> source = D3d11Tools::texture(d, D3d11Tools::desc2d(4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE), &init);
        Com<ID3D11ShaderResourceView> srv = D3d11Tools::srv(d, source.get());
        Com<ID3D11PixelShader> ps = D3d11Tools::ps(d,
            "Texture2D t : register(t0); SamplerState s : register(s0);\n"
            "float4 main(float4 p : SV_Position) : SV_Target { float2 uv = (frac(p.xy / 16.0) * 2.0 - 0.5); return t.Sample(s, uv); }", why);
        Com<ID3D11VertexShader> vs = D3d11Tools::vs(d, D3d11Tools::kFullscreenVs, why);
        if (!srv || !ps || !vs) {
            return false;
        }
        Com<ID3D11Texture2D> target = D3d11Tools::target(d, 128, 64);
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(d, target.get());
        D3d11Tools::clear(d, view.get(), 0, 0, 0, 1);
        ID3D11RenderTargetView* list[1] = {view.get()};
        d.context->OMSetRenderTargets(1, list, nullptr);
        ID3D11ShaderResourceView* views[1] = {srv.get()};
        d.context->PSSetShaderResources(0, 1, views);
        d.context->VSSetShader(vs.get(), nullptr, 0);
        d.context->PSSetShader(ps.get(), nullptr, 0);
        d.context->IASetInputLayout(nullptr);
        d.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const D3D11_FILTER filters[] = {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT, D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT, D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_FILTER_ANISOTROPIC};
        const D3D11_TEXTURE_ADDRESS_MODE modes[] = {D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_MIRROR, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_BORDER, D3D11_TEXTURE_ADDRESS_MIRROR_ONCE};
        int tile = 0;
        for (D3D11_FILTER filter : filters) {
            for (D3D11_TEXTURE_ADDRESS_MODE mode : modes) {
                D3D11_SAMPLER_DESC desc{};
                desc.Filter = filter;
                desc.AddressU = desc.AddressV = desc.AddressW = mode;
                desc.MaxAnisotropy = filter == D3D11_FILTER_ANISOTROPIC ? 1 : 1;
                desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
                desc.BorderColor[0] = 0.9f;
                desc.BorderColor[1] = 0.1f;
                desc.BorderColor[2] = 0.5f;
                desc.BorderColor[3] = 1.0f;
                desc.MaxLOD = D3D11_FLOAT32_MAX;
                Com<ID3D11SamplerState> sampler;
                if (FAILED(d.device->CreateSamplerState(&desc, sampler.put()))) {
                    why = "a sampler state cannot be created";
                    return false;
                }
                ID3D11SamplerState* raw = sampler.get();
                d.context->PSSetSamplers(0, 1, &raw);
                D3D11_VIEWPORT vp{static_cast<float>(tile % 8) * 16.0f, static_cast<float>(tile / 8) * 16.0f, 16.0f, 16.0f, 0, 1};
                d.context->RSSetViewports(1, &vp);
                d.context->Draw(3, 0);
                ++tile;
            }
        }
        return finish_image(d, target.get(), 128, 64, out, why);
    };
    rig.compare(context, problems, "states_samplers", sampler_scene, Tolerance::bytes(4, 0.01));
    return problems.outcome();
}

Outcome RenderChecks::msaa(Context& context) {
    OracleRig rig;
    std::string error;
    if (!rig.open(error)) {
        return Outcome::fail(error);
    }
    if (rig.av.level < D3D_FEATURE_LEVEL_11_0) {
        return Context::skip("coverage and sample-index shading need shader model 5");
    }
    Problems problems;
    int ran = 0;
    for (UINT samples : {2u, 4u, 8u}) {
        UINT levels = 0;
        if (FAILED(rig.av.device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, samples, &levels)) || levels == 0) {
            continue;
        }
        ++ran;
        for (int mode = 0; mode < 4; ++mode) {
            const std::string label = "x" + std::to_string(samples) + " mode " + std::to_string(mode);
            auto scene = [&](D3d11Device& d, SceneImage& out, std::string& why) {
                const char* sources[] = {
                    "float4 main(float4 p : SV_Position, uint c : SV_Coverage) : SV_Target { return float4(countbits(c) / 8.0, 0.5, 0.25, 1); }",
                    "float4 main(float4 p : SV_Position, uint s : SV_SampleIndex) : SV_Target { return float4((s + 1) / 16.0, 0, 1, 1); }",
                    "float4 main() : SV_Target { return float4(1, 1, 1, 1); }",
                    "float4 main() : SV_Target { return float4(1, 0, 0, 0.5); }"};
                Com<ID3D11PixelShader> ps = D3d11Tools::ps(d, sources[mode], why);
                Com<ID3D11Texture2D> multi = D3d11Tools::texture(d, D3d11Tools::desc2d(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, 1, 1, samples));
                Com<ID3D11Texture2D> single = D3d11Tools::target(d, 16, 16);
                Com<ID3D11VertexShader> vs = D3d11Tools::vs(d, D3d11Tools::kFullscreenVs, why);
                if (!ps || !multi || !single || !vs) {
                    return false;
                }
                Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(d, multi.get());
                D3d11Tools::clear(d, view.get(), 0, 0, 0, 0);
                ID3D11RenderTargetView* list[1] = {view.get()};
                d.context->OMSetRenderTargets(1, list, nullptr);
                D3D11_BLEND_DESC blend{};
                blend.AlphaToCoverageEnable = mode == 3 ? TRUE : FALSE;
                blend.RenderTarget[0].RenderTargetWriteMask = 0xf;
                Com<ID3D11BlendState> state;
                d.device->CreateBlendState(&blend, state.put());
                const UINT mask = mode == 2 ? (samples == 8 ? 0x55u : (samples == 4 ? 0x5u : 0x1u)) : 0xffffffffu;
                d.context->OMSetBlendState(state.get(), nullptr, mask);
                D3D11_RASTERIZER_DESC raster{};
                raster.FillMode = D3D11_FILL_SOLID;
                raster.CullMode = D3D11_CULL_NONE;
                raster.MultisampleEnable = TRUE;
                raster.DepthClipEnable = TRUE;
                Com<ID3D11RasterizerState> rs;
                d.device->CreateRasterizerState(&raster, rs.put());
                d.context->RSSetState(rs.get());
                D3d11Tools::set_viewport(d, 16, 16);
                d.context->IASetInputLayout(nullptr);
                d.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                d.context->VSSetShader(vs.get(), nullptr, 0);
                d.context->PSSetShader(ps.get(), nullptr, 0);
                d.context->Draw(3, 0);
                d.context->ResolveSubresource(single.get(), 0, multi.get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
                return finish_image(d, single.get(), 16, 16, out, why);
            };
            SceneImage on_av;
            SceneImage on_warp;
            if (!scene(rig.warp, on_warp, error) || !scene(rig.av, on_av, error)) {
                problems.add(label + ": " + error);
                continue;
            }
            const double n = samples;
            double expected[4] = {0, 0, 0, 0};
            if (mode == 0) {
                expected[0] = 1.0 * n / 8.0;
                expected[1] = 0.5;
                expected[2] = 0.25;
                expected[3] = 1.0;
            } else if (mode == 1) {
                double sum = 0;
                for (UINT s = 0; s < samples; ++s) {
                    sum += (s + 1) / 16.0;
                }
                expected[0] = sum / n;
                expected[2] = 1.0;
                expected[3] = 1.0;
            } else if (mode == 2) {
                expected[0] = expected[1] = expected[2] = expected[3] = 0.5;
            }
            std::string why;
            const FormatSpec spec = FormatCodec::spec(DXGI_FORMAT_R8G8B8A8_UNORM);
            if (mode != 3) {
                if (!FormatCodec::matches(spec, on_av.data.data() + 4 * 20, expected, why)) {
                    problems.add(label + ": " + why);
                }
                std::string detail;
                if (!OracleRig::judge(context, "msaa_" + label, on_warp, on_av, Tolerance::bytes(2), detail)) {
                    problems.add(detail + " (against WARP)");
                }
            } else {
                const double red = on_av.data[4 * 20] / 255.0;
                if (red < 0.2 || red > 0.8) {
                    problems.add(label + ": alpha-to-coverage with alpha 0.5 resolved to " + std::to_string(red));
                }
            }
        }
    }
    if (ran == 0) {
        problems.add("no multisample count is supported");
    }
    return problems.outcome();
}

Outcome RenderChecks::tessellation(Context& context) {
    OracleRig rig;
    std::string error;
    if (!rig.open(error)) {
        return Outcome::fail(error);
    }
    if (rig.av.level < D3D_FEATURE_LEVEL_11_0) {
        return Context::skip("tessellation needs feature level 11_0");
    }
    Problems problems;
    struct Mode {
        const char* domain;
        const char* partitioning;
        const char* topology;
        int points;
        D3D11_PRIMITIVE_TOPOLOGY patch;
    };
    std::vector<Mode> modes;
    for (const char* partitioning : {"integer", "fractional_odd", "fractional_even", "pow2"}) {
        modes.push_back({"tri", partitioning, "triangle_cw", 3, D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST});
        modes.push_back({"quad", partitioning, "triangle_cw", 4, D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST});
        modes.push_back({"isoline", partitioning, "line", 4, D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST});
    }
    auto scene = [&](D3d11Device& d, SceneImage& out, std::string& why) {
        Com<ID3D11Texture2D> target = D3d11Tools::target(d, 192, 64);
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(d, target.get());
        D3d11Tools::clear(d, view.get(), 0, 0, 0, 1);
        ID3D11RenderTargetView* list[1] = {view.get()};
        d.context->OMSetRenderTargets(1, list, nullptr);
        Com<ID3D11VertexShader> vs = D3d11Tools::vs(d, "float2 main(float2 p : POSITION) : POSITION { return p; }", why);
        const std::vector<std::uint8_t> code = D3d11Tools::vs_code(d, "float2 main(float2 p : POSITION) : POSITION { return p; }", why);
        const D3D11_INPUT_ELEMENT_DESC element = {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
        Com<ID3D11InputLayout> layout;
        Com<ID3D11PixelShader> ps = Kit::color_ps(d, "1, 0.8, 0.2, 1", why);
        if (!vs || code.empty() || !ps || FAILED(d.device->CreateInputLayout(&element, 1, code.data(), code.size(), layout.put()))) {
            return false;
        }
        d.context->IASetInputLayout(layout.get());
        d.context->VSSetShader(vs.get(), nullptr, 0);
        d.context->PSSetShader(ps.get(), nullptr, 0);
        const float corners[] = {-0.8f, -0.8f, 0.8f, -0.8f, 0.0f, 0.8f, -0.8f, 0.8f};
        const float quad_corners[] = {-0.8f, -0.8f, 0.8f, -0.8f, -0.8f, 0.8f, 0.8f, 0.8f};
        int tile = 0;
        for (const Mode& m : modes) {
            const std::string hs_source = std::string("struct P { float2 p : POSITION; };\n") +
                "struct C { float e[" + (std::string(m.domain) == "tri" ? "3" : (std::string(m.domain) == "quad" ? "4" : "2")) + "] : SV_TessFactor; " +
                (std::string(m.domain) == "tri" ? "float i : SV_InsideTessFactor;" : (std::string(m.domain) == "quad" ? "float i[2] : SV_InsideTessFactor;" : "")) + " };\n" +
                "C constants(InputPatch<P, " + std::to_string(m.points) + "> patch) { C c; " +
                (std::string(m.domain) == "tri" ? "c.e[0] = 3.7; c.e[1] = 2.3; c.e[2] = 5.1; c.i = 4.4;" :
                 (std::string(m.domain) == "quad" ? "c.e[0] = 3.7; c.e[1] = 2.3; c.e[2] = 5.1; c.e[3] = 1.5; c.i[0] = 4.4; c.i[1] = 2.6;" : "c.e[0] = 3.0; c.e[1] = 6.4;")) + " return c; }\n" +
                "[domain(\"" + m.domain + "\")] [partitioning(\"" + m.partitioning + "\")] [outputtopology(\"" + m.topology + "\")] [outputcontrolpoints(" + std::to_string(m.points) +
                ")] [patchconstantfunc(\"constants\")]\nP main(InputPatch<P, " + std::to_string(m.points) + "> patch, uint id : SV_OutputControlPointID) { return patch[id]; }\n";
            const std::string domain_input = std::string(m.domain) == "tri" ? "float3 uvw : SV_DomainLocation" : "float2 uv : SV_DomainLocation";
            std::string position;
            if (std::string(m.domain) == "tri") {
                position = "patch[0].p * uvw.x + patch[1].p * uvw.y + patch[2].p * uvw.z";
            } else if (std::string(m.domain) == "quad") {
                position = "lerp(lerp(patch[0].p, patch[1].p, uv.x), lerp(patch[2].p, patch[3].p, uv.x), uv.y)";
            } else {
                position = "lerp(lerp(patch[0].p, patch[1].p, uv.x), lerp(patch[2].p, patch[3].p, uv.x), uv.y)";
            }
            const std::string ds_source = std::string("struct P { float2 p : POSITION; };\nstruct C { float e[") +
                (std::string(m.domain) == "tri" ? "3" : (std::string(m.domain) == "quad" ? "4" : "2")) + "] : SV_TessFactor; " +
                (std::string(m.domain) == "tri" ? "float i : SV_InsideTessFactor;" : (std::string(m.domain) == "quad" ? "float i[2] : SV_InsideTessFactor;" : "")) + " };\n" +
                "[domain(\"" + m.domain + "\")] float4 main(C c, " + domain_input + ", const OutputPatch<P, " + std::to_string(m.points) + "> patch) : SV_Position { return float4(" + position + ", 0, 1); }\n";
            Com<ID3D11HullShader> hs = D3d11Tools::hs(d, hs_source, why);
            Com<ID3D11DomainShader> ds = D3d11Tools::ds(d, ds_source, why);
            if (!hs || !ds) {
                why = std::string(m.domain) + "/" + m.partitioning + ": " + why;
                return false;
            }
            const bool tri = std::string(m.domain) == "tri";
            Com<ID3D11Buffer> vb = D3d11Tools::buffer(d, tri ? 24 : 32, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0, tri ? corners : quad_corners);
            UINT stride = 8;
            UINT offset = 0;
            ID3D11Buffer* raw = vb.get();
            d.context->IASetVertexBuffers(0, 1, &raw, &stride, &offset);
            d.context->IASetPrimitiveTopology(m.patch);
            d.context->HSSetShader(hs.get(), nullptr, 0);
            d.context->DSSetShader(ds.get(), nullptr, 0);
            D3D11_VIEWPORT vp{static_cast<float>(tile % 6) * 32.0f, static_cast<float>(tile / 6) * 32.0f, 32.0f, 32.0f, 0, 1};
            d.context->RSSetViewports(1, &vp);
            d.context->Draw(m.points, 0);
            ++tile;
        }
        return finish_image(d, target.get(), 192, 64, out, why);
    };
    rig.compare(context, problems, "tessellation", scene, Tolerance::bytes(8, 0.03));
    return problems.outcome();
}

Outcome RenderChecks::geometry_streamout(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    ID3D11DeviceContext* ctx = device.context;
    Problems problems;
    const char* vs_source = "struct O { uint id : ID; }; O main(uint v : SV_VertexID) { O o; o.id = v; return o; }";
    const char* gs_source =
        "struct I { uint id : ID; }; struct O { float4 pos : POSITION; float4 col : COLOR; };\n"
        "[maxvertexcount(3)] void main(point I i[1], inout PointStream<O> s) {\n"
        "  for (uint k = 0; k < 3; ++k) { O o; o.pos = float4(i[0].id, k, 0, 1); o.col = float4(i[0].id * 2, k + 10, 7, 1); s.Append(o); }\n"
        "}\n";
    Com<ID3D11VertexShader> vs = D3d11Tools::vs(device, vs_source, error);
    std::vector<std::uint8_t> code;
    if (!D3d11Tools::compile(gs_source, "main", D3d11Tools::profile(device, "gs"), code, error)) {
        return Outcome::fail(error);
    }
    const D3D11_SO_DECLARATION_ENTRY entries[2] = {{0, "POSITION", 0, 0, 4, 0}, {0, "COLOR", 0, 0, 4, 1}};
    const UINT strides[2] = {16, 16};
    Com<ID3D11GeometryShader> gs;
    HRESULT result = device.device->CreateGeometryShaderWithStreamOutput(code.data(), code.size(), entries, 2, strides, 2, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, gs.put());
    if (!vs || FAILED(result)) {
        return Outcome::fail("stream output shader creation failed: " + D3d11Tools::hr(result) + " " + error);
    }
    constexpr UINT kPoints = 16;
    {
        D3D11_QUERY_DESC probe{};
        probe.Query = D3D11_QUERY_SO_STATISTICS;
        Com<ID3D11Query> check;
        if (FAILED(device.device->CreateQuery(&probe, check.put()))) {
            return Outcome::fail("the stream output statistics query cannot be created");
        }
        probe.Query = D3D11_QUERY_SO_OVERFLOW_PREDICATE;
        if (FAILED(device.device->CreateQuery(&probe, check.put()))) {
            return Outcome::fail("the stream output overflow predicate query cannot be created");
        }
    }
    constexpr UINT kVertices = kPoints * 3;
    auto run = [&](UINT capacity_vertices, bool& overflowed, UINT64& written, UINT64& needed, std::vector<std::uint8_t>& pos, std::vector<std::uint8_t>& col) {
        Com<ID3D11Buffer> b0 = D3d11Tools::buffer(device, capacity_vertices * 16, D3D11_USAGE_DEFAULT, D3D11_BIND_STREAM_OUTPUT, 0, 0, 0, nullptr);
        Com<ID3D11Buffer> b1 = D3d11Tools::buffer(device, capacity_vertices * 16, D3D11_USAGE_DEFAULT, D3D11_BIND_STREAM_OUTPUT, 0, 0, 0, nullptr);
        D3D11_QUERY_DESC qd{};
        qd.Query = D3D11_QUERY_SO_STATISTICS;
        Com<ID3D11Query> stats;
        device.device->CreateQuery(&qd, stats.put());
        qd.Query = D3D11_QUERY_SO_OVERFLOW_PREDICATE;
        Com<ID3D11Query> predicate;
        device.device->CreateQuery(&qd, predicate.put());
        ID3D11Buffer* targets[2] = {b0.get(), b1.get()};
        const UINT offsets[2] = {0, 0};
        ctx->VSSetShader(vs.get(), nullptr, 0);
        ctx->GSSetShader(gs.get(), nullptr, 0);
        ctx->SOSetTargets(2, targets, offsets);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        ctx->Begin(stats.get());
        ctx->Begin(predicate.get());
        ctx->Draw(kPoints, 0);
        ctx->End(predicate.get());
        ctx->End(stats.get());
        ID3D11Buffer* none[2] = {nullptr, nullptr};
        ctx->SOSetTargets(2, none, offsets);
        D3D11_QUERY_DATA_SO_STATISTICS data{};
        BOOL over = FALSE;
        ctx->Flush();
        while (ctx->GetData(stats.get(), &data, sizeof(data), 0) == S_FALSE) {
            ::SwitchToThread();
        }
        while (ctx->GetData(predicate.get(), &over, sizeof(over), 0) == S_FALSE) {
            ::SwitchToThread();
        }
        written = data.NumPrimitivesWritten;
        needed = data.PrimitivesStorageNeeded;
        overflowed = over != FALSE;
        std::string why;
        return D3d11Tools::read_buffer(device, b0.get(), pos, why) && D3d11Tools::read_buffer(device, b1.get(), col, why);
    };
    bool overflowed = false;
    UINT64 written = 0;
    UINT64 needed = 0;
    std::vector<std::uint8_t> pos;
    std::vector<std::uint8_t> col;
    if (!run(kVertices, overflowed, written, needed, pos, col)) {
        return Outcome::fail("stream output readback failed");
    }
    if (written != kVertices || needed != kVertices || overflowed) {
        problems.add("exact-size buffers: written " + std::to_string(written) + ", needed " + std::to_string(needed) + ", overflow " + std::to_string(overflowed));
    }
    for (UINT v = 0; v < kVertices; ++v) {
        float p[4];
        float c[4];
        std::memcpy(p, pos.data() + v * 16, 16);
        std::memcpy(c, col.data() + v * 16, 16);
        const float id = static_cast<float>(v / 3);
        const float k = static_cast<float>(v % 3);
        if (p[0] != id || p[1] != k || p[3] != 1.0f || c[0] != id * 2 || c[1] != k + 10 || c[2] != 7.0f) {
            problems.add("stream output vertex " + std::to_string(v) + " holds wrong data");
            break;
        }
    }
    if (!run(10, overflowed, written, needed, pos, col)) {
        return Outcome::fail("stream output readback failed");
    }
    if (written != 10 || needed != kVertices || !overflowed) {
        problems.add("overflowing buffers: written " + std::to_string(written) + ", needed " + std::to_string(needed) + ", overflow " + std::to_string(overflowed));
    }
    D3d11Tools::reset_state(device);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome RenderChecks::format_sweep(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    Geometry geometry;
    Com<ID3D11PixelShader> src = Kit::color_ps(device, "1, 0, 0, 0.5", error);
    Com<ID3D11VertexShader> fullscreen = D3d11Tools::vs(device, D3d11Tools::kFullscreenVs, error);
    if (!src || !fullscreen) {
        return Outcome::fail(error);
    }
    int tested = 0;
    int blended = 0;
    for (int number = 1; number <= 115; ++number) {
        const DXGI_FORMAT format = static_cast<DXGI_FORMAT>(number);
        const FormatSpec spec = FormatCodec::spec(format);
        UINT support = 0;
        if (!spec.known || FAILED(device.device->CheckFormatSupport(format, &support)) || (support & D3D11_FORMAT_SUPPORT_RENDER_TARGET) == 0) {
            continue;
        }
        ++tested;
        const bool integer = spec.kind == FormatKind::Uint || spec.kind == FormatKind::Sint;
        Com<ID3D11Texture2D> tex = D3d11Tools::target(device, 8, 8, format, 0);
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, tex.get());
        if (!tex || !view) {
            problems.add("format " + std::to_string(number) + " reports render target support but the target cannot be created");
            continue;
        }
        double clear[4] = {0.2, 0.4, 0.6, 0.8};
        if (spec.kind == FormatKind::Uint) {
            clear[0] = 3; clear[1] = 2; clear[2] = 1; clear[3] = 1;
        } else if (spec.kind == FormatKind::Sint) {
            clear[0] = -3; clear[1] = 2; clear[2] = -1; clear[3] = 1;
        }
        const float f[4] = {static_cast<float>(clear[0]), static_cast<float>(clear[1]), static_cast<float>(clear[2]), static_cast<float>(clear[3])};
        device.context->ClearRenderTargetView(view.get(), f);
        std::vector<std::uint8_t> bytes;
        if (!D3d11Tools::read_pixels(device, tex.get(), bytes, error)) {
            problems.add("format " + std::to_string(number) + ": " + error);
            continue;
        }
        std::string why;
        if (!FormatCodec::matches(spec, bytes.data() + static_cast<std::size_t>(spec.bytes) * 9, clear, why)) {
            problems.add("format " + std::to_string(number) + " clear: " + why);
        }
        if (!integer && (support & D3D11_FORMAT_SUPPORT_BLENDABLE) != 0) {
            ++blended;
            D3d11Tools::clear(device, view.get(), 0, 0, 1, 1);
            D3D11_BLEND_DESC blend{};
            blend.RenderTarget[0].BlendEnable = TRUE;
            blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
            blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blend.RenderTarget[0].RenderTargetWriteMask = 0xf;
            Com<ID3D11BlendState> state;
            device.device->CreateBlendState(&blend, state.put());
            device.context->OMSetBlendState(state.get(), nullptr, 0xffffffff);
            if (!D3d11Tools::draw_fullscreen(device, src.get(), view.get(), 8, 8, error)) {
                return Outcome::fail(error);
            }
            device.context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
            if (!D3d11Tools::read_pixels(device, tex.get(), bytes, error)) {
                problems.add("format " + std::to_string(number) + ": " + error);
                continue;
            }
            const double blended_value[4] = {0.5, 0.0, 0.5, 0.5};
            if (!FormatCodec::matches(spec, bytes.data() + static_cast<std::size_t>(spec.bytes) * 9, blended_value, why)) {
                problems.add("format " + std::to_string(number) + " blended draw: " + why);
            }
        }
    }
    {
        Com<ID3D11Texture2D> typeless = D3d11Tools::target(device, 4, 4, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D11_BIND_SHADER_RESOURCE);
        Com<ID3D11Texture2D> raw = D3d11Tools::target(device, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        Com<ID3D11Texture2D> linear = D3d11Tools::target(device, 4, 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0);
        Com<ID3D11RenderTargetView> srgb_view = D3d11Tools::rtv(device, typeless.get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        Com<ID3D11ShaderResourceView> srgb_srv = D3d11Tools::srv(device, typeless.get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        Com<ID3D11RenderTargetView> linear_view = D3d11Tools::rtv(device, linear.get());
        Com<ID3D11PixelShader> half = Kit::color_ps(device, "0.5, 0.5, 0.5, 1", error);
        Com<ID3D11PixelShader> sample = D3d11Tools::ps(device, "Texture2D t : register(t0);\nfloat4 main(float4 p : SV_Position) : SV_Target { return t.Load(int3(1, 1, 0)); }", error);
        if (!typeless || !raw || !linear || !srgb_view || !srgb_srv || !linear_view || !half || !sample) {
            problems.add("the sRGB scene cannot be built: " + error);
        } else {
            static_cast<void>(D3d11Tools::draw_fullscreen(device, half.get(), srgb_view.get(), 4, 4, error));
            device.context->CopyResource(raw.get(), typeless.get());
            std::vector<std::uint8_t> bytes;
            if (D3d11Tools::read_pixels(device, raw.get(), bytes, error)) {
                const int want = static_cast<int>(std::lround(FormatCodec::linear_to_srgb(0.5) * 255.0));
                if (std::abs(static_cast<int>(bytes[0]) - want) > 1) {
                    problems.add("an sRGB target stored " + std::to_string(bytes[0]) + " for linear 0.5, expected " + std::to_string(want));
                }
            }
            ID3D11ShaderResourceView* views[1] = {srgb_srv.get()};
            device.context->PSSetShaderResources(0, 1, views);
            static_cast<void>(D3d11Tools::draw_fullscreen(device, sample.get(), linear_view.get(), 4, 4, error));
            if (D3d11Tools::read_pixels(device, linear.get(), bytes, error)) {
                float value = 0;
                std::memcpy(&value, bytes.data(), 4);
                if (std::fabs(value - 0.5f) > 0.01f) {
                    problems.add("sampling an sRGB view returned " + std::to_string(value) + " for linear 0.5");
                }
            }
        }
    }
    context.measure("formats", tested);
    context.measure("blended", blended);
    D3d11Tools::reset_state(device);
    return problems.outcome();
}

Outcome RenderChecks::depth_stencil_formats(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    struct Case {
        const char* name;
        DXGI_FORMAT typeless, depth_view, depth_srv, stencil_srv;
        double steps;
        bool float_depth;
    };
    const Case cases[] = {{"D16", DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_UNKNOWN, 65535.0, false},
                          {"D24S8", DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, DXGI_FORMAT_X24_TYPELESS_G8_UINT, 16777215.0, false},
                          {"D32", DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_UNKNOWN, 0, true},
                          {"D32S8", DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, DXGI_FORMAT_X32_TYPELESS_G8X24_UINT, 0, true}};
    const char* vs_source = "cbuffer C : register(b0) { float z; float3 pad; }\nfloat4 main(uint id : SV_VertexID) : SV_Position { float2 p = float2((id << 1) & 2, id & 2); return float4(p * float2(2, -2) + float2(-1, 1), z, 1); }";
    Com<ID3D11VertexShader> vs = D3d11Tools::vs(device, vs_source, error);
    Com<ID3D11PixelShader> color = D3d11Tools::ps(device, "cbuffer C : register(b0) { float z; float3 pad; }\nfloat4 main() : SV_Target { return float4(z, 0, 0, 1); }", error);
    Com<ID3D11PixelShader> reader = D3d11Tools::ps(device,
        "Texture2D<float> d : register(t0); Texture2D<uint2> s : register(t1);\nfloat4 main() : SV_Target { return float4(d.Load(int3(2, 2, 0)), s.Load(int3(2, 2, 0)).g / 255.0, 0, 1); }", error);
    Com<ID3D11PixelShader> depth_only = D3d11Tools::ps(device, "Texture2D<float> d : register(t0);\nfloat4 main() : SV_Target { return float4(d.Load(int3(2, 2, 0)), 0, 0, 1); }", error);
    Com<ID3D11Buffer> constants = D3d11Tools::buffer(device, 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, nullptr);
    if (!vs || !color || !reader || !depth_only || !constants) {
        return Outcome::fail("scene shaders: " + error);
    }
    for (const Case& c : cases) {
        UINT support = 0;
        const bool has_stencil = c.stencil_srv != DXGI_FORMAT_UNKNOWN;
        if (FAILED(device.device->CheckFormatSupport(c.depth_view, &support)) || (support & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL) == 0) {
            problems.add(std::string(c.name) + " is not reported as a depth format");
            continue;
        }
        D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(8, 8, c.typeless, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE);
        Com<ID3D11Texture2D> depth = D3d11Tools::texture(device, desc);
        Com<ID3D11Texture2D> target = D3d11Tools::target(device, 8, 8);
        Com<ID3D11Texture2D> readout = D3d11Tools::target(device, 8, 8, DXGI_FORMAT_R32G32B32A32_FLOAT, 0);
        if (!depth) {
            problems.add(std::string(c.name) + ": a typeless depth texture with shader access cannot be created");
            continue;
        }
        D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
        dd.Format = c.depth_view;
        dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        Com<ID3D11DepthStencilView> dsv;
        device.device->CreateDepthStencilView(depth.get(), &dd, dsv.put());
        Com<ID3D11RenderTargetView> color_view = D3d11Tools::rtv(device, target.get());
        Com<ID3D11RenderTargetView> read_view = D3d11Tools::rtv(device, readout.get());
        Com<ID3D11ShaderResourceView> depth_srv = D3d11Tools::srv(device, depth.get(), c.depth_srv);
        Com<ID3D11ShaderResourceView> stencil_srv = has_stencil ? D3d11Tools::srv(device, depth.get(), c.stencil_srv) : Com<ID3D11ShaderResourceView>();
        if (!dsv || !depth_srv || (has_stencil && !stencil_srv)) {
            problems.add(std::string(c.name) + ": depth or stencil views cannot be created");
            continue;
        }
        D3D11_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = TRUE;
        ds.DepthFunc = D3D11_COMPARISON_GREATER;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        ds.StencilEnable = has_stencil ? TRUE : FALSE;
        ds.StencilReadMask = 0xff;
        ds.StencilWriteMask = 0xff;
        ds.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS};
        ds.BackFace = ds.FrontFace;
        Com<ID3D11DepthStencilState> state;
        device.device->CreateDepthStencilState(&ds, state.put());
        device.context->ClearDepthStencilView(dsv.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);
        D3d11Tools::clear(device, color_view.get(), 0, 0, 0, 1);
        ID3D11RenderTargetView* list[1] = {color_view.get()};
        device.context->OMSetRenderTargets(1, list, dsv.get());
        device.context->OMSetDepthStencilState(state.get(), 0x5A);
        D3d11Tools::set_viewport(device, 8, 8);
        device.context->IASetInputLayout(nullptr);
        device.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device.context->VSSetShader(vs.get(), nullptr, 0);
        device.context->PSSetShader(color.get(), nullptr, 0);
        ID3D11Buffer* raw = constants.get();
        device.context->VSSetConstantBuffers(0, 1, &raw);
        device.context->PSSetConstantBuffers(0, 1, &raw);
        for (float z : {0.25f, 0.75f, 0.5f}) {
            const float data[4] = {z, 0, 0, 0};
            device.context->UpdateSubresource(constants.get(), 0, nullptr, data, 0, 0);
            device.context->Draw(3, 0);
        }
        TexelImage image;
        if (!D3d11Tools::read_texture(device, target.get(), 0, image, error)) {
            problems.add(std::string(c.name) + ": " + error);
            continue;
        }
        if (std::abs(static_cast<int>(image.data[0]) - 191) > 1) {
            problems.add(std::string(c.name) + ": reversed-Z colour is " + std::to_string(image.data[0]) + ", the draw at z 0.75 should win");
        }
        device.context->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11ShaderResourceView* views[2] = {depth_srv.get(), stencil_srv.get()};
        device.context->PSSetShaderResources(0, 2, views);
        static_cast<void>(D3d11Tools::draw_fullscreen(device, has_stencil ? reader.get() : depth_only.get(), read_view.get(), 8, 8, error));
        std::vector<std::uint8_t> bytes;
        if (!D3d11Tools::read_pixels(device, readout.get(), bytes, error)) {
            problems.add(std::string(c.name) + ": " + error);
            continue;
        }
        float values[4];
        std::memcpy(values, bytes.data() + 16 * 18, 16);
        double want = 0.75;
        double tolerance = 1e-6;
        if (!c.float_depth) {
            want = std::round(0.75 * c.steps) / c.steps;
            tolerance = 1.5 / c.steps;
        }
        if (std::fabs(values[0] - want) > tolerance) {
            problems.add(std::string(c.name) + ": depth read through the typeless view is " + std::to_string(values[0]) + ", expected " + std::to_string(want));
        }
        if (has_stencil && std::fabs(values[1] - 0x5A / 255.0) > 1e-4) {
            problems.add(std::string(c.name) + ": stencil read through the typeless view is " + std::to_string(values[1] * 255.0) + ", expected 90");
        }
        ID3D11ShaderResourceView* clear_views[2] = {nullptr, nullptr};
        device.context->PSSetShaderResources(0, 2, clear_views);
    }
    D3d11Tools::reset_state(device);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome RenderChecks::multi_rt_viewports(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    ID3D11DeviceContext* ctx = device.context;
    Com<ID3D11VertexShader> vs = D3d11Tools::vs(device, D3d11Tools::kFullscreenVs, error);
    if (!vs) {
        return Outcome::fail(error);
    }
    {
        const DXGI_FORMAT formats[8] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_UINT,
                                        DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R8G8_UNORM};
        std::string source = "struct O {";
        std::string body = "O main() { O o;";
        const char* types[8] = {"float4", "float4", "float4", "uint4", "float4", "float4", "uint4", "float4"};
        std::vector<Com<ID3D11Texture2D>> textures(8);
        std::vector<Com<ID3D11RenderTargetView>> views(8);
        ID3D11RenderTargetView* raw[8] = {};
        for (int i = 0; i < 8; ++i) {
            source += std::string(" ") + types[i] + " c" + std::to_string(i) + " : SV_Target" + std::to_string(i) + ";";
            body += " o.c" + std::to_string(i) + " = " + types[i] + "(" + std::to_string(i + 1) + ", 2, 3, 1);";
            textures[static_cast<std::size_t>(i)] = D3d11Tools::target(device, 8, 8, formats[i], 0);
            views[static_cast<std::size_t>(i)] = D3d11Tools::rtv(device, textures[static_cast<std::size_t>(i)].get());
            raw[i] = views[static_cast<std::size_t>(i)].get();
            if (raw[i] == nullptr) {
                return Outcome::fail("a render target of format " + std::to_string(formats[i]) + " cannot be created");
            }
        }
        Com<ID3D11PixelShader> ps = D3d11Tools::ps(device, source + " };\n" + body + " return o; }", error);
        if (!ps) {
            return Outcome::fail(error);
        }
        ctx->OMSetRenderTargets(8, raw, nullptr);
        D3d11Tools::set_viewport(device, 8, 8);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs.get(), nullptr, 0);
        ctx->PSSetShader(ps.get(), nullptr, 0);
        ctx->Draw(3, 0);
        for (int i = 0; i < 8; ++i) {
            const FormatSpec spec = FormatCodec::spec(formats[i]);
            std::vector<std::uint8_t> bytes;
            if (!D3d11Tools::read_pixels(device, textures[static_cast<std::size_t>(i)].get(), bytes, error)) {
                problems.add(error);
                continue;
            }
            double want[4] = {i + 1.0, 2.0, 3.0, 1.0};
            if (spec.kind != FormatKind::Uint && spec.kind != FormatKind::Float) {
                want[0] = std::fmin(i + 1.0, 1.0);
                want[1] = want[2] = 1.0;
                if (formats[i] == DXGI_FORMAT_R10G10B10A2_UNORM || formats[i] == DXGI_FORMAT_R8G8B8A8_UNORM || formats[i] == DXGI_FORMAT_R8_UNORM) {
                    want[0] = 1.0;
                }
            }
            std::string why;
            if (!FormatCodec::matches(spec, bytes.data() + static_cast<std::size_t>(spec.bytes) * 9, want, why)) {
                problems.add("render target " + std::to_string(i) + ": " + why);
            }
        }
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
    }
    if (device.level >= D3D_FEATURE_LEVEL_10_0) {
        const char* gs_source =
            "struct I { float4 pos : SV_Position; }; struct O { float4 pos : SV_Position; uint vp : SV_ViewportArrayIndex; nointerpolation uint slot : SLOT; };\n"
            "[maxvertexcount(48)] void main(triangle I i[3], inout TriangleStream<O> s) {\n"
            "  for (uint v = 0; v < 16; ++v) { for (uint k = 0; k < 3; ++k) { O o; o.pos = i[k].pos; o.vp = v; o.slot = v + 1; s.Append(o); } s.RestartStrip(); }\n"
            "}\n";
        const char* slice_source =
            "struct I { float4 pos : SV_Position; }; struct O { float4 pos : SV_Position; uint rt : SV_RenderTargetArrayIndex; nointerpolation uint slot : SLOT; };\n"
            "[maxvertexcount(12)] void main(triangle I i[3], inout TriangleStream<O> s) {\n"
            "  for (uint v = 0; v < 4; ++v) { for (uint k = 0; k < 3; ++k) { O o; o.pos = i[k].pos; o.rt = v; o.slot = (v + 1) * 10; s.Append(o); } s.RestartStrip(); }\n"
            "}\n";
        Com<ID3D11GeometryShader> gs = D3d11Tools::gs(device, gs_source, error);
        Com<ID3D11GeometryShader> slice_gs = D3d11Tools::gs(device, slice_source, error);
        Com<ID3D11PixelShader> ps = D3d11Tools::ps(device, "float4 main(float4 p : SV_Position, nointerpolation uint slot : SLOT) : SV_Target { return float4(slot / 255.0, 0, 0, 1); }", error);
        if (!gs || !slice_gs || !ps) {
            problems.add("geometry shader scene: " + error);
        } else {
            Com<ID3D11Texture2D> target = D3d11Tools::target(device, 64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
            Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target.get());
            D3d11Tools::clear(device, view.get(), 0, 0, 0, 1);
            ID3D11RenderTargetView* list[1] = {view.get()};
            ctx->OMSetRenderTargets(1, list, nullptr);
            D3D11_VIEWPORT viewports[16];
            D3D11_RECT rects[16];
            for (int i = 0; i < 16; ++i) {
                viewports[i] = {static_cast<float>(i % 4) * 16.0f, static_cast<float>(i / 4) * 16.0f, 16.0f, 16.0f, 0, 1};
                rects[i] = {(i % 4) * 16 + 2, (i / 4) * 16 + 2, (i % 4) * 16 + 14, (i / 4) * 16 + 14};
            }
            D3D11_RASTERIZER_DESC raster{};
            raster.FillMode = D3D11_FILL_SOLID;
            raster.CullMode = D3D11_CULL_NONE;
            raster.ScissorEnable = TRUE;
            raster.DepthClipEnable = TRUE;
            Com<ID3D11RasterizerState> rs;
            device.device->CreateRasterizerState(&raster, rs.put());
            ctx->RSSetState(rs.get());
            ctx->RSSetViewports(16, viewports);
            ctx->RSSetScissorRects(16, rects);
            ctx->VSSetShader(vs.get(), nullptr, 0);
            ctx->GSSetShader(gs.get(), nullptr, 0);
            ctx->PSSetShader(ps.get(), nullptr, 0);
            ctx->Draw(3, 0);
            TexelImage image;
            if (!D3d11Tools::read_texture(device, target.get(), 0, image, error)) {
                problems.add(error);
            } else {
                int bad = 0;
                for (UINT y = 0; y < 64 && bad < 3; ++y) {
                    for (UINT x = 0; x < 64; ++x) {
                        const int tile = static_cast<int>(y / 16) * 4 + static_cast<int>(x / 16);
                        const bool inside = x % 16 >= 2 && x % 16 < 14 && y % 16 >= 2 && y % 16 < 14;
                        const std::uint32_t want = Kit::rgba(inside ? tile + 1 : 0, 0, 0, 255);
                        if (image.pixel(x, y) != want && bad++ < 3) {
                            problems.add("viewport array: pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is 0x" + Text::hex(image.pixel(x, y), 8) + ", expected 0x" + Text::hex(want, 8));
                        }
                    }
                }
            }
            Com<ID3D11Texture2D> array = D3d11Tools::texture(device, D3d11Tools::desc2d(8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, 1, 4));
            Com<ID3D11RenderTargetView> array_view;
            if (array) {
                device.device->CreateRenderTargetView(array.get(), nullptr, array_view.put());
            }
            if (!array_view) {
                problems.add("a render target array cannot be created");
            } else {
                D3d11Tools::clear(device, array_view.get(), 0, 0, 0, 1);
                ID3D11RenderTargetView* array_list[1] = {array_view.get()};
                ctx->OMSetRenderTargets(1, array_list, nullptr);
                ctx->GSSetShader(slice_gs.get(), nullptr, 0);
                D3d11Tools::set_viewport(device, 8, 8);
                ctx->RSSetState(nullptr);
                ctx->Draw(3, 0);
                for (UINT slice = 0; slice < 4; ++slice) {
                    if (!D3d11Tools::read_texture(device, array.get(), slice, image, error)) {
                        problems.add(error);
                        break;
                    }
                    std::string why;
                    if (!Kit::uniform(image, Kit::rgba(static_cast<int>(slice + 1) * 10, 0, 0, 255), why)) {
                        problems.add("render target array slice " + std::to_string(slice) + ": " + why);
                    }
                }
            }
        }
    } else {
        context.note("viewport and render target array routing needs feature level 10_0");
    }
    D3d11Tools::reset_state(device);
    return problems.outcome();
}

Outcome RenderChecks::resource_hazards(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    ID3D11DeviceContext* ctx = device.context;
    Com<ID3D11Texture2D> tex = D3d11Tools::target(device, 8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    Com<ID3D11RenderTargetView> rtv = D3d11Tools::rtv(device, tex.get());
    Com<ID3D11ShaderResourceView> srv = D3d11Tools::srv(device, tex.get());
    if (!rtv || !srv) {
        return Outcome::fail("a render target with a shader resource view cannot be created");
    }
    ID3D11ShaderResourceView* views[1] = {srv.get()};
    ctx->PSSetShaderResources(0, 1, views);
    ID3D11RenderTargetView* targets[1] = {rtv.get()};
    ctx->OMSetRenderTargets(1, targets, nullptr);
    ID3D11ShaderResourceView* bound = nullptr;
    ctx->PSGetShaderResources(0, 1, &bound);
    if (bound != nullptr) {
        problems.add("binding a resource as render target left its shader resource view bound");
        bound->Release();
    }
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->OMSetRenderTargets(1, targets, nullptr);
    ctx->PSSetShaderResources(0, 1, views);
    ID3D11RenderTargetView* current = nullptr;
    ctx->OMGetRenderTargets(1, &current, nullptr);
    if (current != nullptr) {
        problems.add("binding a shader resource view of the bound render target left the render target bound");
        current->Release();
    }
    ctx->ClearState();
    Com<ID3D11Texture2D> ping[2] = {D3d11Tools::target(device, 8, 8, DXGI_FORMAT_R32_FLOAT), D3d11Tools::target(device, 8, 8, DXGI_FORMAT_R32_FLOAT)};
    Com<ID3D11RenderTargetView> ping_rtv[2] = {D3d11Tools::rtv(device, ping[0].get()), D3d11Tools::rtv(device, ping[1].get())};
    Com<ID3D11ShaderResourceView> ping_srv[2] = {D3d11Tools::srv(device, ping[0].get()), D3d11Tools::srv(device, ping[1].get())};
    Com<ID3D11PixelShader> step = D3d11Tools::ps(device, "Texture2D<float> t : register(t0);\nfloat4 main(float4 p : SV_Position) : SV_Target { return float4(t.Load(int3(uint2(p.xy), 0)) + 1.0, 0, 0, 1); }", error);
    if (!step) {
        return Outcome::fail(error);
    }
    D3d11Tools::clear(device, ping_rtv[0].get(), 0, 0, 0, 1);
    for (int pass = 0; pass < 100; ++pass) {
        const int from = pass % 2;
        const int to = 1 - from;
        ID3D11ShaderResourceView* in[1] = {ping_srv[from].get()};
        ctx->PSSetShaderResources(0, 1, in);
        if (!D3d11Tools::draw_fullscreen(device, step.get(), ping_rtv[to].get(), 8, 8, error)) {
            return Outcome::fail(error);
        }
        ID3D11ShaderResourceView* none[1] = {nullptr};
        ctx->PSSetShaderResources(0, 1, none);
    }
    std::vector<std::uint8_t> bytes;
    if (!D3d11Tools::read_pixels(device, ping[0].get(), bytes, error)) {
        return Outcome::fail(error);
    }
    for (UINT p = 0; p < 64; ++p) {
        float value = 0;
        std::memcpy(&value, bytes.data() + p * 4, 4);
        if (value != 100.0f) {
            problems.add("after a chain of 100 passes pixel " + std::to_string(p) + " holds " + std::to_string(value));
            break;
        }
    }
    D3d11Tools::reset_state(device);
    static_cast<void>(context);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d11
