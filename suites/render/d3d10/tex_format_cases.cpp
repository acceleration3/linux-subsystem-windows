#include "render/d3d10/util.hpp"

#include <cstring>

namespace lsw::suites::render::d3d10 {
namespace {

struct TexFormat {
    const char* name;
    DXGI_FORMAT format;
    char cls;
    unsigned bytes;
    char gen;
    bool linear;
    float tol;
};

const TexFormat kFormats[] = {
    {"r32g32b32a32_float", DXGI_FORMAT_R32G32B32A32_FLOAT, 'f', 16, 'f', false, 1e-4f},
    {"r32g32b32_float", DXGI_FORMAT_R32G32B32_FLOAT, 'f', 12, 'f', false, 1e-4f},
    {"r32g32_float", DXGI_FORMAT_R32G32_FLOAT, 'f', 8, 'f', false, 1e-4f},
    {"r32_float", DXGI_FORMAT_R32_FLOAT, 'f', 4, 'f', false, 1e-4f},
    {"r32g32b32a32_uint", DXGI_FORMAT_R32G32B32A32_UINT, 'u', 16, 'u', false, 0},
    {"r32g32b32_uint", DXGI_FORMAT_R32G32B32_UINT, 'u', 12, 'u', false, 0},
    {"r32g32_uint", DXGI_FORMAT_R32G32_UINT, 'u', 8, 'u', false, 0},
    {"r32_uint", DXGI_FORMAT_R32_UINT, 'u', 4, 'u', false, 0},
    {"r32g32b32a32_sint", DXGI_FORMAT_R32G32B32A32_SINT, 'i', 16, 'u', false, 0},
    {"r32g32_sint", DXGI_FORMAT_R32G32_SINT, 'i', 8, 'u', false, 0},
    {"r32_sint", DXGI_FORMAT_R32_SINT, 'i', 4, 'u', false, 0},
    {"r16g16b16a16_float", DXGI_FORMAT_R16G16B16A16_FLOAT, 'f', 8, 'h', true, 1e-3f},
    {"r16g16b16a16_unorm", DXGI_FORMAT_R16G16B16A16_UNORM, 'f', 8, 'r', true, 1e-3f},
    {"r16g16b16a16_snorm", DXGI_FORMAT_R16G16B16A16_SNORM, 'f', 8, 'r', true, 1e-3f},
    {"r16g16b16a16_uint", DXGI_FORMAT_R16G16B16A16_UINT, 'u', 8, 'r', false, 0},
    {"r16g16b16a16_sint", DXGI_FORMAT_R16G16B16A16_SINT, 'i', 8, 'r', false, 0},
    {"r16g16_float", DXGI_FORMAT_R16G16_FLOAT, 'f', 4, 'h', true, 1e-3f},
    {"r16g16_unorm", DXGI_FORMAT_R16G16_UNORM, 'f', 4, 'r', true, 1e-3f},
    {"r16g16_snorm", DXGI_FORMAT_R16G16_SNORM, 'f', 4, 'r', true, 1e-3f},
    {"r16g16_uint", DXGI_FORMAT_R16G16_UINT, 'u', 4, 'r', false, 0},
    {"r16g16_sint", DXGI_FORMAT_R16G16_SINT, 'i', 4, 'r', false, 0},
    {"r16_float", DXGI_FORMAT_R16_FLOAT, 'f', 2, 'h', true, 1e-3f},
    {"r16_unorm", DXGI_FORMAT_R16_UNORM, 'f', 2, 'r', true, 1e-3f},
    {"r16_snorm", DXGI_FORMAT_R16_SNORM, 'f', 2, 'r', true, 1e-3f},
    {"r16_uint", DXGI_FORMAT_R16_UINT, 'u', 2, 'r', false, 0},
    {"r16_sint", DXGI_FORMAT_R16_SINT, 'i', 2, 'r', false, 0},
    {"r8g8b8a8_unorm", DXGI_FORMAT_R8G8B8A8_UNORM, 'f', 4, 'r', true, 1e-3f},
    {"r8g8b8a8_unorm_srgb", DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 'f', 4, 'r', true, 3e-3f},
    {"r8g8b8a8_snorm", DXGI_FORMAT_R8G8B8A8_SNORM, 'f', 4, 'r', true, 1e-3f},
    {"r8g8b8a8_uint", DXGI_FORMAT_R8G8B8A8_UINT, 'u', 4, 'r', false, 0},
    {"r8g8b8a8_sint", DXGI_FORMAT_R8G8B8A8_SINT, 'i', 4, 'r', false, 0},
    {"r8g8_unorm", DXGI_FORMAT_R8G8_UNORM, 'f', 2, 'r', true, 1e-3f},
    {"r8g8_snorm", DXGI_FORMAT_R8G8_SNORM, 'f', 2, 'r', true, 1e-3f},
    {"r8g8_uint", DXGI_FORMAT_R8G8_UINT, 'u', 2, 'r', false, 0},
    {"r8g8_sint", DXGI_FORMAT_R8G8_SINT, 'i', 2, 'r', false, 0},
    {"r8_unorm", DXGI_FORMAT_R8_UNORM, 'f', 1, 'r', true, 1e-3f},
    {"r8_snorm", DXGI_FORMAT_R8_SNORM, 'f', 1, 'r', true, 1e-3f},
    {"r8_uint", DXGI_FORMAT_R8_UINT, 'u', 1, 'r', false, 0},
    {"r8_sint", DXGI_FORMAT_R8_SINT, 'i', 1, 'r', false, 0},
    {"a8_unorm", DXGI_FORMAT_A8_UNORM, 'f', 1, 'r', true, 1e-3f},
    {"b8g8r8a8_unorm", DXGI_FORMAT_B8G8R8A8_UNORM, 'f', 4, 'r', true, 1e-3f},
    {"b8g8r8a8_unorm_srgb", DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, 'f', 4, 'r', true, 3e-3f},
    {"b8g8r8x8_unorm", DXGI_FORMAT_B8G8R8X8_UNORM, 'f', 4, 'r', true, 1e-3f},
    {"r10g10b10a2_unorm", DXGI_FORMAT_R10G10B10A2_UNORM, 'f', 4, 'r', true, 1e-3f},
    {"r10g10b10a2_uint", DXGI_FORMAT_R10G10B10A2_UINT, 'u', 4, 'r', false, 0},
    {"r11g11b10_float", DXGI_FORMAT_R11G11B10_FLOAT, 'f', 4, 'e', true, 1e-3f},
    {"r9g9b9e5_sharedexp", DXGI_FORMAT_R9G9B9E5_SHAREDEXP, 'f', 4, 'x', true, 2e-3f},
    {"b5g6r5_unorm", DXGI_FORMAT_B5G6R5_UNORM, 'f', 2, 'r', true, 1e-3f},
    {"b5g5r5a1_unorm", DXGI_FORMAT_B5G5R5A1_UNORM, 'f', 2, 'r', true, 1e-3f},
    {"bc1_unorm", DXGI_FORMAT_BC1_UNORM, 'f', 8, 'b', true, 6e-3f},
    {"bc1_unorm_srgb", DXGI_FORMAT_BC1_UNORM_SRGB, 'f', 8, 'b', true, 8e-3f},
    {"bc2_unorm", DXGI_FORMAT_BC2_UNORM, 'f', 16, 'b', true, 6e-3f},
    {"bc2_unorm_srgb", DXGI_FORMAT_BC2_UNORM_SRGB, 'f', 16, 'b', true, 8e-3f},
    {"bc3_unorm", DXGI_FORMAT_BC3_UNORM, 'f', 16, 'b', true, 6e-3f},
    {"bc3_unorm_srgb", DXGI_FORMAT_BC3_UNORM_SRGB, 'f', 16, 'b', true, 8e-3f},
    {"bc4_unorm", DXGI_FORMAT_BC4_UNORM, 'f', 8, 'b', true, 6e-3f},
    {"bc4_snorm", DXGI_FORMAT_BC4_SNORM, 'f', 8, 'b', true, 2e-2f},
    {"bc5_unorm", DXGI_FORMAT_BC5_UNORM, 'f', 16, 'b', true, 6e-3f},
    {"bc5_snorm", DXGI_FORMAT_BC5_SNORM, 'f', 16, 'b', true, 2e-2f},
};

std::vector<std::uint8_t> make_data(const TexFormat& f, UINT width, UINT height, std::uint32_t seed) {
    const bool block = f.gen == 'b';
    const std::size_t count = block ? static_cast<std::size_t>(width / 4) * (height / 4) : static_cast<std::size_t>(width) * height;
    std::vector<std::uint8_t> data(count * f.bytes);
    std::uint32_t state = seed;
    auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    for (std::size_t i = 0; i < count; ++i) {
        std::uint8_t* t = data.data() + i * f.bytes;
        switch (f.gen) {
            case 'f':
                for (unsigned c = 0; c < f.bytes / 4; ++c) {
                    const float v = static_cast<float>(next() & 0xffff) / 16384.0f - 2.0f;
                    std::memcpy(t + c * 4, &v, 4);
                }
                break;
            case 'h':
                for (unsigned c = 0; c < f.bytes / 2; ++c) {
                    const std::uint16_t h = rr::PixelInfo::float_to_half(static_cast<float>(next() & 0xffff) / 16384.0f - 2.0f);
                    std::memcpy(t + c * 2, &h, 2);
                }
                break;
            case 'u':
                for (unsigned c = 0; c < f.bytes / 4; ++c) {
                    const std::uint32_t v = (next() << 16) ^ next();
                    std::memcpy(t + c * 4, &v, 4);
                }
                break;
            case 'e': {
                const std::uint32_t v = (next() & 0x3fu) | ((next() % 7u + 12u) << 6) | ((next() & 0x3fu) << 11) | ((next() % 7u + 12u) << 17) | ((next() & 0x1fu) << 22) | ((next() % 7u + 12u) << 27);
                std::memcpy(t, &v, 4);
                break;
            }
            case 'x': {
                const std::uint32_t v = (next() & 0x1ffu) | ((next() & 0x1ffu) << 9) | ((next() & 0x1ffu) << 18) | ((10 + next() % 10) << 27);
                std::memcpy(t, &v, 4);
                break;
            }
            default:
                for (unsigned c = 0; c < f.bytes; ++c) t[c] = static_cast<std::uint8_t>(next());
                break;
        }
    }
    return data;
}

const char* kLoadPs[3] = {
    "Texture2D<float4> t : register(t0);\n"
    "float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target { return t.Load(int3(int2(uv * 16), 0)); }\n",
    "Texture2D<uint4> t : register(t0);\n"
    "float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target { return float4(t.Load(int3(int2(uv * 16), 0))); }\n",
    "Texture2D<int4> t : register(t0);\n"
    "float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target { return float4(t.Load(int3(int2(uv * 16), 0))); }\n",
};

const char* kIntPs[2] = {
    "Texture2D<uint4> t : register(t0);\n"
    "uint4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target { return t.Load(int3(int2(uv * 16), 0)); }\n",
    "Texture2D<int4> t : register(t0);\n"
    "uint4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target { return asuint(t.Load(int3(int2(uv * 16), 0))); }\n",
};

const char* kSamplePs =
    "Texture2D<float4> t : register(t0);\nSamplerState s : register(s0);\n"
    "float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target { return t.Sample(s, uv * 0.9 + 0.05); }\n";

void run_format(Rig& r, rr::CaptureSet& o, const TexFormat& f, bool sample) {
    r.begin(128, 128, DXGI_FORMAT_R32G32B32A32_FLOAT);
    r.use_std();
    const bool block = f.gen == 'b';
    const std::vector<std::uint8_t> data = make_data(f, 16, 16, 12345);
    const UINT pitch = block ? 4 * f.bytes : 16 * f.bytes;
    Com<ID3D10Texture2D> tex = r.tex2d(16, 16, f.format, D3D10_BIND_SHADER_RESOURCE, 1, 1, {data.data()}, {pitch});
    Com<ID3D10ShaderResourceView> view = r.srv(tex.get());
    Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR));
    Com<ID3D10PixelShader> ps = r.ps(sample ? kSamplePs : kLoadPs[f.cls == 'f' ? 0 : f.cls == 'u' ? 1 : 2]);
    if (!r.ok()) return;
    r.d->PSSetShader(ps.get());
    r.ps_srv(0, view.get());
    r.ps_smp(0, smp.get());
    r.draw_std(Geo::quad_uv(-1, -1, 1, 1, 0.5f, 0, 0, 1, 1));
    r.cap_main(o);
}

struct StreamState {
    Com<ID3D10Texture2D> tex;
    Com<ID3D10ShaderResourceView> view;
    Com<ID3D10PixelShader> ps;
    std::vector<std::uint8_t> data;
};

void stream_setup(Rig& r, StreamState& s, const TexFormat& f) {
    r.begin(128, 128, f.cls == 'f' ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R32G32B32A32_UINT);
    r.use_std();
    s.data = make_data(f, 16, 16, 12345);
    const bool block = f.gen == 'b';
    std::vector<std::uint8_t> zeros(s.data.size(), 0);
    s.tex = r.tex2d(16, 16, f.format, D3D10_BIND_SHADER_RESOURCE, 1, 1, {zeros.data()}, {block ? 4 * f.bytes : 16 * f.bytes});
    s.view = r.srv(s.tex.get());
    s.ps = r.ps(f.cls == 'f' ? kLoadPs[0] : kIntPs[f.cls == 'u' ? 0 : 1]);
}

void stream_frame(Rig& r, StreamState& s, const TexFormat& f, unsigned frame) {
    const bool block = f.gen == 'b';
    D3D10_BOX box = {4 * frame, 0, 0, 4 * frame + 4, 16, 1};
    std::vector<std::uint8_t> part;
    UINT pitch;
    if (block) {
        pitch = f.bytes;
        for (unsigned br = 0; br < 4; ++br) part.insert(part.end(), s.data.begin() + (br * 4 + frame) * f.bytes, s.data.begin() + (br * 4 + frame + 1) * f.bytes);
    } else {
        pitch = 4 * f.bytes;
        for (unsigned y = 0; y < 16; ++y) part.insert(part.end(), s.data.begin() + (y * 16 + 4 * frame) * f.bytes, s.data.begin() + (y * 16 + 4 * frame + 4) * f.bytes);
    }
    if (!(r.mutate("tex_stream") && frame == 3)) {
        r.d->UpdateSubresource(s.tex.get(), 0, &box, part.data(), pitch, 0);
    }
    r.d->PSSetShader(s.ps.get());
    r.ps_srv(0, s.view.get());
    r.draw_std(Geo::quad_uv(-1, -1, 1, 1, 0.5f, 0, 0, 1, 1));
}

void stream_case(const TexFormat& f, const Caps& caps) {
    const TexFormat copy = f;
    MultiBody<StreamState> body;
    body.setup = [copy](Rig& r, StreamState& s) { stream_setup(r, s, copy); };
    body.frame = [copy](Rig& r, StreamState& s, unsigned frame) { stream_frame(r, s, copy, frame); };
    multi<StreamState>(spec("tex", std::string("stream_update_load_") + f.name, f.name[0] == 'b' ? 1 : 0).frames(4, {1, 3}).tolerance(rr::Tolerance::floats(f.tol, f.tol)), body, caps);
}

const Registrar kTexFormats{[] {
    const char* sampled[] = {"r8_unorm", "r8g8b8a8_unorm", "r8g8b8a8_unorm_srgb", "r8g8b8a8_snorm", "r16_float", "r16_unorm", "r16g16b16a16_float", "r10g10b10a2_unorm", "r11g11b10_float", "r9g9b9e5_sharedexp", "bc1_unorm", "bc2_unorm", "bc3_unorm", "bc4_unorm", "bc5_unorm"};
    for (const TexFormat& f : kFormats) {
        const TexFormat copy = f;
        const Caps caps = [copy](Rig& r) -> std::string {
            const UINT need = D3D10_FORMAT_SUPPORT_TEXTURE2D | D3D10_FORMAT_SUPPORT_SHADER_LOAD;
            return (r.format_support(copy.format) & need) == need ? "" : std::string("shader-loadable 2D format ") + copy.name;
        };
        stream_case(f, caps);
        bool want_sample = false;
        for (const char* n : sampled) want_sample = want_sample || std::string(n) == f.name;
        if (f.linear && want_sample) {
            const Caps linear_caps = [copy](Rig& r) -> std::string {
                const UINT need = D3D10_FORMAT_SUPPORT_TEXTURE2D | D3D10_FORMAT_SUPPORT_SHADER_SAMPLE;
                return (r.format_support(copy.format) & need) == need ? "" : std::string("filterable 2D format ") + copy.name;
            };
            single(spec("tex", std::string("sample_linear_") + f.name).tolerance(rr::Tolerance::floats(copy.format == DXGI_FORMAT_R11G11B10_FLOAT || copy.format == DXGI_FORMAT_R9G9B9E5_SHAREDEXP ? 0.12f : 0.03f, 0.03f)),
                   [copy](Rig& r, rr::CaptureSet& o) { run_format(r, o, copy, true); }, linear_caps);
        }
    }
}};

}  // namespace
}  // namespace lsw::suites::render::d3d10
