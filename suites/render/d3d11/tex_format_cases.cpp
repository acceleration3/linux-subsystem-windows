#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

struct FormatRow {
    const char* name;
    DXGI_FORMAT format;
};

const FormatRow kFormats[] = {
    {"r32g32b32a32_float", DXGI_FORMAT_R32G32B32A32_FLOAT}, {"r32g32b32a32_uint", DXGI_FORMAT_R32G32B32A32_UINT},
    {"r32g32b32a32_sint", DXGI_FORMAT_R32G32B32A32_SINT},   {"r32g32b32_float", DXGI_FORMAT_R32G32B32_FLOAT},
    {"r16g16b16a16_float", DXGI_FORMAT_R16G16B16A16_FLOAT}, {"r16g16b16a16_unorm", DXGI_FORMAT_R16G16B16A16_UNORM},
    {"r16g16b16a16_uint", DXGI_FORMAT_R16G16B16A16_UINT},   {"r16g16b16a16_snorm", DXGI_FORMAT_R16G16B16A16_SNORM},
    {"r16g16b16a16_sint", DXGI_FORMAT_R16G16B16A16_SINT},   {"r32g32_float", DXGI_FORMAT_R32G32_FLOAT},
    {"r32g32_uint", DXGI_FORMAT_R32G32_UINT},               {"r32g32_sint", DXGI_FORMAT_R32G32_SINT},
    {"r10g10b10a2_unorm", DXGI_FORMAT_R10G10B10A2_UNORM},   {"r10g10b10a2_uint", DXGI_FORMAT_R10G10B10A2_UINT},
    {"r11g11b10_float", DXGI_FORMAT_R11G11B10_FLOAT},       {"r8g8b8a8_unorm", DXGI_FORMAT_R8G8B8A8_UNORM},
    {"r8g8b8a8_unorm_srgb", DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}, {"r8g8b8a8_uint", DXGI_FORMAT_R8G8B8A8_UINT},
    {"r8g8b8a8_snorm", DXGI_FORMAT_R8G8B8A8_SNORM},         {"r8g8b8a8_sint", DXGI_FORMAT_R8G8B8A8_SINT},
    {"r16g16_float", DXGI_FORMAT_R16G16_FLOAT},             {"r16g16_unorm", DXGI_FORMAT_R16G16_UNORM},
    {"r16g16_uint", DXGI_FORMAT_R16G16_UINT},               {"r16g16_snorm", DXGI_FORMAT_R16G16_SNORM},
    {"r16g16_sint", DXGI_FORMAT_R16G16_SINT},               {"r32_float", DXGI_FORMAT_R32_FLOAT},
    {"r32_uint", DXGI_FORMAT_R32_UINT},                     {"r32_sint", DXGI_FORMAT_R32_SINT},
    {"r8g8_unorm", DXGI_FORMAT_R8G8_UNORM},                 {"r8g8_uint", DXGI_FORMAT_R8G8_UINT},
    {"r8g8_snorm", DXGI_FORMAT_R8G8_SNORM},                 {"r8g8_sint", DXGI_FORMAT_R8G8_SINT},
    {"r16_float", DXGI_FORMAT_R16_FLOAT},                   {"r16_unorm", DXGI_FORMAT_R16_UNORM},
    {"r16_uint", DXGI_FORMAT_R16_UINT},                     {"r16_snorm", DXGI_FORMAT_R16_SNORM},
    {"r16_sint", DXGI_FORMAT_R16_SINT},                     {"r8_unorm", DXGI_FORMAT_R8_UNORM},
    {"r8_uint", DXGI_FORMAT_R8_UINT},                       {"r8_snorm", DXGI_FORMAT_R8_SNORM},
    {"r8_sint", DXGI_FORMAT_R8_SINT},                       {"a8_unorm", DXGI_FORMAT_A8_UNORM},
    {"b5g6r5_unorm", DXGI_FORMAT_B5G6R5_UNORM},             {"b5g5r5a1_unorm", DXGI_FORMAT_B5G5R5A1_UNORM},
    {"b8g8r8a8_unorm", DXGI_FORMAT_B8G8R8A8_UNORM},         {"b8g8r8a8_unorm_srgb", DXGI_FORMAT_B8G8R8A8_UNORM_SRGB},
    {"b8g8r8x8_unorm", DXGI_FORMAT_B8G8R8X8_UNORM},         {"b4g4r4a4_unorm", DXGI_FORMAT_B4G4R4A4_UNORM},
    {"r9g9b9e5_sharedexp", DXGI_FORMAT_R9G9B9E5_SHAREDEXP},
};

std::uint32_t pack_e5(float r, float g, float b) {
    const float peak = std::fmax(r, std::fmax(g, b));
    if (peak <= 0.0f) return 0;
    int e = static_cast<int>(std::floor(std::log2(peak)));
    e = std::max(e, -16) + 1;
    float denom = std::ldexp(1.0f, e - 9);
    if (static_cast<int>(std::floor(peak / denom + 0.5f)) == 512) {
        ++e;
        denom = std::ldexp(1.0f, e - 9);
    }
    auto mant = [&](float c) { return static_cast<std::uint32_t>(std::floor(std::fmax(c, 0.0f) / denom + 0.5f)) & 511u; };
    return mant(r) | (mant(g) << 9) | (mant(b) << 18) | (static_cast<std::uint32_t>(e + 15) << 27);
}

void texel_values(const FormatSpec& s, int x, int y, double v[4]) {
    const double fx = x / 15.0;
    const double fy = y / 15.0;
    const double x3 = ((x ^ y) & 15) / 15.0;
    const int bits = s.field[0].bits;
    switch (s.kind) {
        case FormatKind::Unorm:
        case FormatKind::Srgb:
            v[0] = fx; v[1] = fy; v[2] = x3; v[3] = 1.0 - fx * 0.5;
            break;
        case FormatKind::Snorm:
            v[0] = fx * 2 - 1; v[1] = fy * 2 - 1; v[2] = x3 * 2 - 1; v[3] = 0.5 - fx * 0.5;
            break;
        case FormatKind::Float:
            v[0] = bits < 16 ? fx * 3 : fx * 5 - 1; v[1] = bits < 16 ? fy * 3 : fy * 5 - 1; v[2] = x3 * 2; v[3] = 4.0 * fx;
            break;
        case FormatKind::Uint: {
            const double top = static_cast<double>(std::min<std::uint64_t>((1ull << bits) - 1, 1ull << 24));
            v[0] = std::floor(fx * top); v[1] = std::floor(fy * top); v[2] = std::floor(x3 * top); v[3] = std::floor(top / 2);
            break;
        }
        case FormatKind::Sint: {
            const double top = static_cast<double>(std::min<std::uint64_t>((1ull << (bits - 1)) - 1, 1ull << 24));
            v[0] = std::floor((fx * 2 - 1) * top); v[1] = std::floor((fy * 2 - 1) * top); v[2] = std::floor((x3 * 2 - 1) * top); v[3] = std::floor(top / 3);
            break;
        }
    }
}

struct Plan {
    std::string srv_type;
    std::string load;
    std::string display;
};

Plan plan_for(FormatKind kind, int bits) {
    Plan p;
    const double top = kind == FormatKind::Uint ? static_cast<double>(std::min<std::uint64_t>((1ull << bits) - 1, 1ull << 24))
                                                : static_cast<double>(std::min<std::uint64_t>((1ull << (bits - 1)) - 1, 1ull << 24));
    switch (kind) {
        case FormatKind::Uint:
            p.srv_type = "uint4";
            p.display = "float4(v) / " + std::to_string(top);
            break;
        case FormatKind::Sint:
            p.srv_type = "int4";
            p.display = "float4(v) / " + std::to_string(top) + " * 0.5 + 0.5";
            break;
        case FormatKind::Snorm:
            p.srv_type = "float4";
            p.display = "v * 0.5 + 0.5";
            break;
        case FormatKind::Float:
            p.srv_type = "float4";
            p.display = "v * 0.2 + 0.2";
            break;
        default:
            p.srv_type = "float4";
            p.display = "v";
            break;
    }
    return p;
}

std::string sample_ps(const Plan& p, bool load, const char* type_override = nullptr, int size = 16) {
    std::string src = "Texture2D<" + std::string(type_override != nullptr ? type_override : p.srv_type.c_str()) + "> t : register(t0);\nSamplerState s : register(s0);\n";
    src += "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n";
    src += "float4 main(VO i) : SV_Target { ";
    if (load || p.srv_type != "float4") {
        src += p.srv_type + " raw = t.Load(int3(i.uv * " + std::to_string(size) + ".0, 0)); float4 v = float4(raw);";
    } else {
        src += "float4 v = t.Sample(s, i.uv);";
    }
    src += " float4 d = " + p.display + "; float4 o = float4(d.rgb, 1); o.rgb = (i.uv.x > 0.8) ? d.aaa : o.rgb; return saturate(o); }\n";
    return src;
}

bool run_format(Rig& r, Cap& out, DXGI_FORMAT format) {
    const FormatSpec spec = FormatCodec::spec(format);
    Bytes data(16 * 16 * static_cast<std::size_t>(spec.bytes));
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            std::uint8_t* texel = data.data() + (static_cast<std::size_t>(y) * 16 + x) * static_cast<std::size_t>(spec.bytes);
            if (format == DXGI_FORMAT_R9G9B9E5_SHAREDEXP) {
                const std::uint32_t packed = pack_e5(x / 15.0f * 3.0f, y / 15.0f * 2.0f, ((x ^ y) & 15) / 15.0f);
                std::memcpy(texel, &packed, 4);
                continue;
            }
            double v[4];
            texel_values(spec, x, y, v);
            FormatCodec::encode(spec, v, texel);
        }
    }
    FormatKind kind = format == DXGI_FORMAT_R9G9B9E5_SHAREDEXP ? FormatKind::Float : spec.kind;
    const Plan plan = plan_for(kind, format == DXGI_FORMAT_R9G9B9E5_SHAREDEXP ? 16 : spec.field[0].bits);
    Target target;
    Com<ID3D11Texture2D> texture = r.tex_data(16, 16, format, data);
    Com<ID3D11PixelShader> ps = r.ps(sample_ps(plan, false));
    Com<ID3D11SamplerState> sampler = r.smp(sdesc());
    if (!r.color(target) || !texture || !ps || !sampler) return false;
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

struct Registrar {
    Registrar() {
        for (const FormatRow& row : kFormats) {
            const rr::Tolerance tolerance = rr::Tolerance::blend(3).with_outliers(0.002);
            auto cs = spec("tex", (std::string("sample_") + row.name).c_str()).tolerance(tolerance);
            if (row.format == DXGI_FORMAT_B4G4R4A4_UNORM) cs.variant(1);
            Add(std::move(cs), [&row](Rig& r, Cap& out) { return run_format(r, out, row.format); },
                [&row](Rig& r) { return need_format(r, row.format, D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_LOAD, "sampling"); });
        }
    }
};
const Registrar kRegistrar;

struct BcRow {
    const char* name;
    int family;
    DXGI_FORMAT format;
    char kind;
};

const BcRow kBc[] = {
    {"bc1_unorm", 1, DXGI_FORMAT_BC1_UNORM, 'u'},   {"bc1_unorm_srgb", 1, DXGI_FORMAT_BC1_UNORM_SRGB, 'u'},
    {"bc2_unorm", 2, DXGI_FORMAT_BC2_UNORM, 'u'},   {"bc2_unorm_srgb", 2, DXGI_FORMAT_BC2_UNORM_SRGB, 'u'},
    {"bc3_unorm", 3, DXGI_FORMAT_BC3_UNORM, 'u'},   {"bc3_unorm_srgb", 3, DXGI_FORMAT_BC3_UNORM_SRGB, 'u'},
    {"bc4_unorm", 4, DXGI_FORMAT_BC4_UNORM, 'u'},   {"bc4_snorm", 4, DXGI_FORMAT_BC4_SNORM, 's'},
    {"bc5_unorm", 5, DXGI_FORMAT_BC5_UNORM, 'u'},   {"bc5_snorm", 5, DXGI_FORMAT_BC5_SNORM, 's'},
    {"bc6h_uf16", 6, DXGI_FORMAT_BC6H_UF16, 'f'},   {"bc6h_sf16", 6, DXGI_FORMAT_BC6H_SF16, 'f'},
    {"bc7_unorm", 7, DXGI_FORMAT_BC7_UNORM, 'u'},   {"bc7_unorm_srgb", 7, DXGI_FORMAT_BC7_UNORM_SRGB, 'u'},
};

bool run_bc(Rig& r, Cap& out, const BcRow& row) {
    Bytes blocks = BcCodec::blocks(row.family, 1234u + static_cast<unsigned>(row.family), 16);
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = blocks.data();
    init.SysMemPitch = 4u * static_cast<UINT>(BcCodec::block_bytes(row.family));
    Com<ID3D11Texture2D> texture = r.tex(16, 16, row.format, D3D11_BIND_SHADER_RESOURCE, 1, 1, 1, &init);
    Plan plan;
    plan.srv_type = "float4";
    plan.display = row.kind == 's' ? "v * 0.5 + 0.5" : row.kind == 'f' ? "v / (1.0 + abs(v))" : "v";
    Com<ID3D11PixelShader> ps = r.ps(sample_ps(plan, false));
    Com<ID3D11SamplerState> sampler = r.smp(sdesc());
    Target target;
    if (!r.color(target) || !texture || !ps || !sampler) return false;
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

struct BcRegistrar {
    BcRegistrar() {
        for (const BcRow& row : kBc) {
            Add(spec("tex", (std::string("sample_") + row.name).c_str()).tolerance(rr::Tolerance::filter(6, 0.004)),
                [&row](Rig& r, Cap& out) { return run_bc(r, out, row); },
                [&row](Rig& r) { return need_format(r, row.format, D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE, "block compressed sampling"); });
        }
    }
};
const BcRegistrar kBcRegistrar;

struct DepthRow {
    const char* name;
    DXGI_FORMAT depth;
    DXGI_FORMAT view;
    bool stencil;
};

const DepthRow kDepthViews[] = {
    {"d32_float_as_r32_float", DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, false},
    {"d16_unorm_as_r16_unorm", DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_R16_UNORM, false},
    {"d24s8_depth_as_r24_unorm_x8", DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, false},
    {"d24s8_stencil_as_x24_g8_uint", DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_X24_TYPELESS_G8_UINT, true},
    {"d32s8_depth_as_r32_float_x8x24", DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, false},
    {"d32s8_stencil_as_x32_g8x24_uint", DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_X32_TYPELESS_G8X24_UINT, true},
};

bool run_depth_view(Rig& r, Cap& out, const DepthRow& row) {
    Depth depth;
    if (!r.depth(depth, 64, 64, row.depth)) return false;
    Com<ID3D11VertexShader> vs = r.vs(Hlsl3d::kFsVsZ);
    Com<ID3D11Buffer> zb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
    const char* ramp =
        "cbuffer Z : register(b0) { float4 zc; };\n"
        "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
        "struct PO { float d : SV_Depth; };\n"
        "PO main(VO i) { PO o; o.d = zc.x + 0.5 * i.uv.x * zc.y + 0.2 * i.uv.y; return o; }\n";
    Com<ID3D11PixelShader> fill = r.ps(ramp);
    D3D11_DEPTH_STENCIL_DESC dd = ddesc(true, D3D11_COMPARISON_ALWAYS, true);
    dd.StencilEnable = TRUE;
    dd.FrontFace = {D3D11_STENCIL_OP_REPLACE, D3D11_STENCIL_OP_REPLACE, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS};
    dd.BackFace = dd.FrontFace;
    Com<ID3D11DepthStencilState> state = r.dss(dd);
    D3D11_RASTERIZER_DESC rd = rdesc();
    rd.ScissorEnable = TRUE;
    Com<ID3D11RasterizerState> rs = r.rast(rd);
    Plan plan;
    plan.srv_type = row.stencil ? "uint4" : "float4";
    plan.display = row.stencil ? "float4(v) / 255.0" : "v";
    Com<ID3D11PixelShader> show = r.ps(sample_ps(plan, true, row.stencil ? "uint4" : "float4", 64));
    Target target;
    if (!vs || !zb || !fill || !state || !rs || !show || !r.color(target, 64, 64)) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = row.view;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    Com<ID3D11ShaderResourceView> srv = r.srv(depth.texture.get(), &sd);
    if (!srv) return false;
    ID3D11DeviceContext* c = r.ctx;
    c->OMSetRenderTargets(0, nullptr, depth.view.get());
    c->ClearDepthStencilView(depth.view.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    r.viewport(0, 0, 64, 64);
    c->RSSetState(rs.get());
    ID3D11Buffer* cb = zb.get();
    c->VSSetConstantBuffers(0, 1, &cb);
    c->PSSetConstantBuffers(0, 1, &cb);
    c->IASetInputLayout(nullptr);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(vs.get(), nullptr, 0);
    c->PSSetShader(fill.get(), nullptr, 0);
    const D3D11_RECT left{0, 0, 40, 64};
    const D3D11_RECT right{40, 0, 64, 64};
    const float z0[4] = {0.1f, 1.0f, 0, 0};
    c->UpdateSubresource(zb.get(), 0, nullptr, z0, 0, 0);
    c->RSSetScissorRects(1, &left);
    c->OMSetDepthStencilState(state.get(), 37);
    c->Draw(3, 0);
    const float z1[4] = {0.4f, 0.4f, 0, 0};
    c->UpdateSubresource(zb.get(), 0, nullptr, z1, 0, 0);
    c->RSSetScissorRects(1, &right);
    c->OMSetDepthStencilState(state.get(), 201);
    c->Draw(3, 0);
    Com<ID3D11SamplerState> sampler = r.smp(sdesc());
    r.bind(target, nullptr);
    ID3D11ShaderResourceView* v = srv.get();
    ID3D11SamplerState* s = sampler.get();
    D3D11_RASTERIZER_DESC rd2 = rdesc();
    Com<ID3D11RasterizerState> plain = r.rast(rd2);
    c->RSSetState(plain.get());
    c->OMSetDepthStencilState(nullptr, 0);
    c->PSSetShaderResources(0, 1, &v);
    c->PSSetSamplers(0, 1, &s);
    r.draw_fs(show.get());
    return r.shot(out, target);
}

struct DepthRegistrar {
    DepthRegistrar() {
        for (const DepthRow& row : kDepthViews) {
            Add(spec("tex", (std::string("sample_depth_") + row.name).c_str()).size(64, 64).tolerance(rr::Tolerance::blend(3)),
                [&row](Rig& r, Cap& out) { return run_depth_view(r, out, row); },
                [&row](Rig& r) { return need_format(r, row.depth, D3D11_FORMAT_SUPPORT_DEPTH_STENCIL, "depth stencil use"); });
        }
    }
};
const DepthRegistrar kDepthRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d11
