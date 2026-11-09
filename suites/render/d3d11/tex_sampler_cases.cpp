#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

rr::Pattern::Bytes make_pattern(unsigned size) {
    rr::Pattern::Bytes base = rr::Pattern::checker(size, size, size / 8, rr::Pattern::pack(240, 240, 235), rr::Pattern::pack(30, 60, 160));
    const rr::Pattern::Bytes grad = rr::Pattern::gradient(size, size);
    for (std::size_t i = 0; i < base.size(); i += 4) {
        for (int k = 0; k < 3; ++k) {
            base[i + k] = static_cast<std::uint8_t>((base[i + k] * 3 + grad[i + k]) / 4);
        }
        base[i + 3] = static_cast<std::uint8_t>(90 + ((i / 4) % size) * 2 + ((i / 4) / size) % 40);
    }
    return base;
}

const char* const kFloorVs =
    "cbuffer Frame : register(b0) { column_major float4x4 m; };\n"
    "struct VI { float3 p : POSITION; float2 uv : TEXCOORD0; };\n"
    "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "VO main(VI i) { VO o; o.p = mul(m, float4(i.p, 1)); o.uv = i.uv; return o; }\n";
const char* const kFloorPs =
    "Texture2D t : register(t0);\nSamplerState s : register(s0);\n"
    "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "float4 main(VO i) : SV_Target { return t.Sample(s, i.uv); }\n";

struct FloorVertex {
    float x, y, z, u, v;
};

bool draw_floor(Rig& r, Cap& out, const D3D11_SAMPLER_DESC& sampler_desc, UINT size = 64, float uv_scale = 1.0f) {
    Target target;
    Bytes code;
    Com<ID3D11VertexShader> vs = r.vs(kFloorVs, &code);
    Com<ID3D11PixelShader> ps = r.ps(kFloorPs);
    Com<ID3D11Texture2D> texture = r.tex_chain(size, size, make_pattern(size));
    Com<ID3D11SamplerState> sampler = r.smp(sampler_desc);
    if (!r.color(target) || !vs || !ps || !texture || !sampler) return false;
    Com<ID3D11ShaderResourceView> view = r.srv(texture.get());
    Com<ID3D11InputLayout> il = r.layout({elem("POSITION", DXGI_FORMAT_R32G32B32_FLOAT, 0), elem("TEXCOORD", DXGI_FORMAT_R32G32_FLOAT, 12)}, code);
    const float s = uv_scale;
    const std::vector<FloorVertex> verts = {{-4, -0.9f, -1.2f, -2 * s, 0}, {4, -0.9f, -1.2f, 2 * s, 0}, {-4, -0.9f, -24, -2 * s, 11 * s},
                                            {4, -0.9f, -24, 2 * s, 11 * s}};
    Com<ID3D11Buffer> vb = r.vbuf(verts);
    const rr::Mat4 m = rr::Mat4::perspective_d3d(1.1f, 1.0f, 0.5f, 40.0f);
    Com<ID3D11Buffer> cb = r.cbuf(m.m, 64);
    if (!view || !il || !vb || !cb) return false;
    r.bind(target);
    ID3D11Buffer* vbs = vb.get();
    ID3D11Buffer* cbs = cb.get();
    const UINT stride = sizeof(FloorVertex);
    const UINT offset = 0;
    ID3D11ShaderResourceView* v = view.get();
    ID3D11SamplerState* sp = sampler.get();
    r.ctx->IASetInputLayout(il.get());
    r.ctx->IASetVertexBuffers(0, 1, &vbs, &stride, &offset);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->VSSetConstantBuffers(0, 1, &cbs);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    r.ctx->PSSetShaderResources(0, 1, &v);
    r.ctx->PSSetSamplers(0, 1, &sp);
    r.ctx->Draw(4, 0);
    return r.shot(out, target);
}

struct FilterRow {
    const char* name;
    D3D11_FILTER filter;
};

const FilterRow kFilters[] = {
    {"min_mag_mip_point", D3D11_FILTER_MIN_MAG_MIP_POINT},
    {"min_mag_point_mip_linear", D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR},
    {"min_point_mag_linear_mip_point", D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT},
    {"min_point_mag_mip_linear", D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR},
    {"min_linear_mag_mip_point", D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT},
    {"min_linear_mag_point_mip_linear", D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR},
    {"min_mag_linear_mip_point", D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT},
    {"min_mag_mip_linear", D3D11_FILTER_MIN_MAG_MIP_LINEAR},
};

struct Registrar {
    Registrar() {
        for (const FilterRow& row : kFilters) {
            Add(spec("tex", (std::string("filter_") + row.name).c_str()).tolerance(rr::Tolerance::psnr(26)),
                [&row](Rig& r, Cap& out) { return draw_floor(r, out, sdesc(row.filter, D3D11_TEXTURE_ADDRESS_WRAP)); });
        }
        for (UINT level : {2u, 4u, 8u, 16u}) {
            Add(spec("tex", ("filter_anisotropic_" + std::to_string(level)).c_str()).tolerance(rr::Tolerance::psnr(22)), [level](Rig& r, Cap& out) {
                D3D11_SAMPLER_DESC d = sdesc(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP);
                d.MaxAnisotropy = level;
                return draw_floor(r, out, d, 64, 2.0f);
            });
        }
        Add(spec("tex", "lod_bias_positive").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
            D3D11_SAMPLER_DESC d = sdesc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
            d.MipLODBias = 2.0f;
            return draw_floor(r, out, d);
        });
        Add(spec("tex", "lod_bias_negative").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
            D3D11_SAMPLER_DESC d = sdesc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
            d.MipLODBias = -1.5f;
            return draw_floor(r, out, d);
        });
        Add(spec("tex", "lod_min_clamp").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
            D3D11_SAMPLER_DESC d = sdesc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
            d.MinLOD = 2.5f;
            return draw_floor(r, out, d);
        });
        Add(spec("tex", "lod_max_clamp").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
            D3D11_SAMPLER_DESC d = sdesc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
            d.MaxLOD = 1.0f;
            return draw_floor(r, out, d);
        });
    }
};
const Registrar kRegistrar;

struct AddressRow {
    const char* name;
    D3D11_TEXTURE_ADDRESS_MODE mode;
    float border[4];
};

const AddressRow kAddress[] = {
    {"wrap", D3D11_TEXTURE_ADDRESS_WRAP, {0, 0, 0, 0}},
    {"mirror", D3D11_TEXTURE_ADDRESS_MIRROR, {0, 0, 0, 0}},
    {"clamp", D3D11_TEXTURE_ADDRESS_CLAMP, {0, 0, 0, 0}},
    {"border_red", D3D11_TEXTURE_ADDRESS_BORDER, {1, 0.1f, 0.1f, 1}},
    {"border_transparent_green", D3D11_TEXTURE_ADDRESS_BORDER, {0, 0.8f, 0.2f, 0.5f}},
    {"mirror_once", D3D11_TEXTURE_ADDRESS_MIRROR_ONCE, {0, 0, 0, 0}},
};

bool run_address(Rig& r, Cap& out, const AddressRow& row, D3D11_FILTER filter) {
    Target target;
    Com<ID3D11Texture2D> texture = r.tex_chain(32, 32, make_pattern(32));
    D3D11_SAMPLER_DESC d = sdesc(filter, row.mode);
    std::memcpy(d.BorderColor, row.border, 16);
    Com<ID3D11SamplerState> sampler = r.smp(d);
    const char* source =
        "Texture2D t : register(t0);\nSamplerState s : register(s0);\n"
        "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
        "float4 main(VO i) : SV_Target { return t.SampleLevel(s, i.uv * 3.0 - 1.0, 0); }\n";
    Com<ID3D11PixelShader> ps = r.ps(source);
    if (!r.color(target) || !texture || !sampler || !ps) return false;
    Com<ID3D11ShaderResourceView> view = r.srv(texture.get());
    if (!view) return false;
    r.bind(target);
    ID3D11ShaderResourceView* v = view.get();
    ID3D11SamplerState* s = sampler.get();
    r.ctx->PSSetShaderResources(0, 1, &v);
    r.ctx->PSSetSamplers(0, 1, &s);
    r.draw_fs(ps.get());
    return r.shot(out, target);
}

struct AddressRegistrar {
    AddressRegistrar() {
        for (const AddressRow& row : kAddress) {
            Add(spec("tex", (std::string("address_") + row.name + "_point").c_str()).tolerance(rr::Tolerance::filter(6, 0.01).with_edge(1)),
                [&row](Rig& r, Cap& out) { return run_address(r, out, row, D3D11_FILTER_MIN_MAG_MIP_POINT); });
            Add(spec("tex", (std::string("address_") + row.name + "_linear").c_str()).tolerance(rr::Tolerance::filter(6, 0.01).with_edge(1)),
                [&row](Rig& r, Cap& out) { return run_address(r, out, row, D3D11_FILTER_MIN_MAG_MIP_LINEAR); });
        }
    }
};
const AddressRegistrar kAddressRegistrar;

struct ExprRow {
    const char* name;
    const char* body;
    D3D11_FILTER filter;
    bool float_texture;
};

const char* const kPsHead =
    "Texture2D t : register(t0);\nSamplerState s : register(s0);\nSamplerComparisonState c : register(s1);\n"
    "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n";

const ExprRow kExpr[] = {
    {"sample_level", "return t.SampleLevel(s, i.uv, i.uv.x * 5.0);", D3D11_FILTER_MIN_MAG_MIP_LINEAR, false},
    {"sample_grad", "return t.SampleGrad(s, i.uv * 2.0, float2(0.01, 0) * (1 + i.uv.y * 8), float2(0, 0.01));", D3D11_FILTER_MIN_MAG_MIP_LINEAR, false},
    {"sample_bias", "return t.SampleBias(s, i.uv * 4.0, i.uv.x * 4.0 - 1.0);", D3D11_FILTER_MIN_MAG_MIP_LINEAR, false},
    {"calculate_lod", "float l = t.CalculateLevelOfDetail(s, i.uv * (4.0 + 12.0 * i.uv.x)); float u = t.CalculateLevelOfDetailUnclamped(s, i.uv * (4.0 + 12.0 * i.uv.x)); return float4(l / 6.0, 0.5 + 0.1 * u, u / 6.0, 1);", D3D11_FILTER_MIN_MAG_MIP_LINEAR, false},
    {"gather_red", "return t.GatherRed(s, i.uv * 2.0);", D3D11_FILTER_MIN_MAG_MIP_POINT, false},
    {"gather_green", "return t.GatherGreen(s, i.uv * 2.0);", D3D11_FILTER_MIN_MAG_MIP_POINT, false},
    {"gather_blue", "return t.GatherBlue(s, i.uv * 2.0);", D3D11_FILTER_MIN_MAG_MIP_POINT, false},
    {"gather_alpha", "return t.GatherAlpha(s, i.uv * 2.0) * 0.7 + 0.1;", D3D11_FILTER_MIN_MAG_MIP_POINT, false},
    {"gather_offset", "return t.GatherGreen(s, i.uv * 2.0, int2(1, -1));", D3D11_FILTER_MIN_MAG_MIP_POINT, false},
    {"load_offset", "return t.Load(int3(i.uv * 32.0, 1), int2(3, -2));", D3D11_FILTER_MIN_MAG_MIP_POINT, false},
    {"sample_offset", "return t.Sample(s, i.uv * 3.0, int2(2, 1));", D3D11_FILTER_MIN_MAG_MIP_POINT, false},
    {"sample_cmp_level_zero", "float r = t.SampleCmpLevelZero(c, i.uv, 0.3 + 0.5 * i.uv.y); return float4(r, r * i.uv.x, 1 - r, 1);", D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT, true},
    {"sample_cmp_pcf", "float r = t.SampleCmpLevelZero(c, i.uv, 0.3 + 0.5 * i.uv.y); return float4(r, r * i.uv.x, 1 - r, 1);", D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, true},
    {"gather_cmp", "float4 r = t.GatherCmp(c, i.uv * 2.0, 0.5); return r;", D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT, true},
    {"sample_cmp", "float r = t.SampleCmp(c, i.uv, 0.2 + 0.6 * i.uv.x); return float4(r, 1 - r, i.uv.y, 1);", D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, true},
};

bool run_expr(Rig& r, Cap& out, const ExprRow& row) {
    Target target;
    Com<ID3D11Texture2D> texture;
    if (row.float_texture) {
        std::vector<float> ramp(32 * 32);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) ramp[static_cast<std::size_t>(y) * 32 + x] = 0.5f + 0.4f * std::sin(x * 0.4f) * std::cos(y * 0.3f) + ((x ^ y) & 4 ? 0.1f : -0.1f);
        D3D11_SUBRESOURCE_DATA init{ramp.data(), 32 * 4, 0};
        texture = r.tex(32, 32, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, 1, 1, 1, &init);
    } else {
        texture = r.tex_chain(64, 64, make_pattern(64));
    }
    D3D11_SAMPLER_DESC d = sdesc(row.filter, D3D11_TEXTURE_ADDRESS_WRAP);
    D3D11_SAMPLER_DESC cd = sdesc(row.filter, D3D11_TEXTURE_ADDRESS_CLAMP);
    cd.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (row.float_texture) d = cd;
    Com<ID3D11SamplerState> sampler = r.smp(d);
    Com<ID3D11SamplerState> comparison = r.smp(cd);
    Com<ID3D11PixelShader> ps = r.ps(std::string(kPsHead) + "float4 main(VO i) : SV_Target { " + row.body + " }\n");
    if (!r.color(target) || !texture || !sampler || !comparison || !ps) return false;
    Com<ID3D11ShaderResourceView> view = r.srv(texture.get());
    if (!view) return false;
    r.bind(target);
    ID3D11ShaderResourceView* v = view.get();
    ID3D11SamplerState* ss[2] = {row.float_texture ? comparison.get() : sampler.get(), comparison.get()};
    r.ctx->PSSetShaderResources(0, 1, &v);
    r.ctx->PSSetSamplers(0, 2, ss);
    r.draw_fs(ps.get());
    return r.shot(out, target);
}

struct ExprRegistrar {
    ExprRegistrar() {
        for (const ExprRow& row : kExpr) {
            Add(spec("tex", (std::string("intrinsic_") + row.name).c_str()).tolerance(std::string(row.name) == "calculate_lod" ? rr::Tolerance::filter(12, 0.03) : rr::Tolerance::filter(6, 0.01).with_edge(1)),
                [&row](Rig& r, Cap& out) { return run_expr(r, out, row); });
        }
    }
};
const ExprRegistrar kExprRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d11
