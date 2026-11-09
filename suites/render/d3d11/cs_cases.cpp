#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

const char* const kShowTex =
    "Texture2D t : register(t0);\nSamplerState s : register(s0);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "float4 main(VO i) : SV_Target { return t.SampleLevel(s, i.uv, 0); }\n";

bool words_image(Rig& r, ID3D11Buffer* buffer, unsigned words, Cap& out, const char* label) {
    Bytes bytes;
    if (!r.read_bytes(buffer, bytes)) return false;
    rr::Image image(words / 4, 1, rr::PixelKind::Rgba32Uint);
    std::memcpy(image.bytes().data(), bytes.data(), words * 4);
    out.add(label, std::move(image));
    return true;
}

bool show_texture(Rig& r, Cap& out, ID3D11Texture2D* tex, D3D11_FILTER filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR) {
    Target target;
    Com<ID3D11PixelShader> ps = r.ps(kShowTex);
    Com<ID3D11SamplerState> smp = r.smp(sdesc(filter, D3D11_TEXTURE_ADDRESS_CLAMP));
    Com<ID3D11ShaderResourceView> srv = r.srv(tex);
    if (!ps || !smp || !srv || !r.color(target)) return false;
    r.bind(target);
    ID3D11ShaderResourceView* v = srv.get();
    ID3D11SamplerState* s = smp.get();
    r.ctx->CSSetShader(nullptr, nullptr, 0);
    r.ctx->PSSetShaderResources(0, 1, &v);
    r.ctx->PSSetSamplers(0, 1, &s);
    r.draw_fs(ps.get());
    return r.shot(out, target);
}

void unbind_cs(Rig& r) {
    ID3D11UnorderedAccessView* none[4] = {};
    ID3D11ShaderResourceView* nones[4] = {};
    r.ctx->CSSetUnorderedAccessViews(0, 4, none, nullptr);
    r.ctx->CSSetShaderResources(0, 4, nones);
}

const Add kWriteTexture(spec("cs", "write_rwtexture2d_then_draw").tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> tex = r.tex(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
    Com<ID3D11ComputeShader> cs = r.cs(
        "RWTexture2D<float4> u : register(u0);\n[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) { float2 p = float2(id.xy) / 64.0;\n"
        "  u[id.xy] = float4(p.x, p.y, 0.5 + 0.5 * sin((p.x + p.y) * 25.0), 1); }\n");
    if (!tex || !cs) return false;
    Com<ID3D11UnorderedAccessView> uav = r.uav(tex.get());
    if (!uav) return false;
    ID3D11UnorderedAccessView* u = uav.get();
    r.ctx->CSSetShader(cs.get(), nullptr, 0);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
    r.ctx->Dispatch(8, 8, 1);
    unbind_cs(r);
    return show_texture(r, out, tex.get());
});

const Add kBlur(spec("cs", "groupshared_gaussian_blur_barriers").tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> src = r.tex_data(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, rr::Pattern::checker(64, 64, 8, rr::Pattern::pack(250, 240, 30), rr::Pattern::pack(20, 60, 200)));
    Com<ID3D11Texture2D> mid = r.tex(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
    Com<ID3D11Texture2D> dst = r.tex(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
    auto source = [](bool horizontal) {
        const std::string pc = horizontal ? "int2(i, row)" : "int2(row, i)";
        const std::string lo = horizontal ? "int2(0, row)" : "int2(row, 0)";
        const std::string hi = horizontal ? "int2(63, row)" : "int2(row, 63)";
        return std::string("Texture2D<float4> t : register(t0);\nRWTexture2D<float4> u : register(u0);\ngroupshared float4 tile[72];\n"
                           "static const float w[9] = {0.027, 0.065, 0.121, 0.176, 0.2, 0.176, 0.121, 0.065, 0.027};\n"
                           "[numthreads(64, 1, 1)] void main(uint3 g : SV_GroupID, uint3 th : SV_GroupThreadID) { int row = g.x; int i = th.x;\n"
                           "  tile[i + 4] = t.Load(int3(" + pc + ", 0)); if (i < 4) { tile[i] = t.Load(int3(" + lo + ", 0)); tile[68 + i] = t.Load(int3(" + hi + ", 0)); }\n"
                           "  GroupMemoryBarrierWithGroupSync(); float4 acc = 0; [unroll] for (int k = 0; k < 9; ++k) acc += tile[i + k] * w[k]; u[" + pc + "] = acc; }\n");
    };
    Com<ID3D11ComputeShader> horizontal = r.cs(source(true));
    Com<ID3D11ComputeShader> vertical = r.cs(source(false));
    if (!src || !mid || !dst || !horizontal || !vertical) return false;
    Com<ID3D11ShaderResourceView> s0 = r.srv(src.get());
    Com<ID3D11ShaderResourceView> s1 = r.srv(mid.get());
    Com<ID3D11UnorderedAccessView> u0 = r.uav(mid.get());
    Com<ID3D11UnorderedAccessView> u1 = r.uav(dst.get());
    if (!s0 || !s1 || !u0 || !u1) return false;
    ID3D11ShaderResourceView* v0 = s0.get();
    ID3D11UnorderedAccessView* uu0 = u0.get();
    r.ctx->CSSetShader(horizontal.get(), nullptr, 0);
    r.ctx->CSSetShaderResources(0, 1, &v0);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &uu0, nullptr);
    r.ctx->Dispatch(64, 1, 1);
    unbind_cs(r);
    ID3D11ShaderResourceView* v1 = s1.get();
    ID3D11UnorderedAccessView* uu1 = u1.get();
    r.ctx->CSSetShader(vertical.get(), nullptr, 0);
    r.ctx->CSSetShaderResources(0, 1, &v1);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &uu1, nullptr);
    r.ctx->Dispatch(64, 1, 1);
    unbind_cs(r);
    return show_texture(r, out, dst.get());
});

const Add kHistogram(spec("cs", "histogram_interlockedadd_visualised").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) {
    Bytes px(64 * 64 * 4);
    std::uint32_t state = 5;
    for (std::size_t i = 0; i < px.size(); i += 4) {
        const float a = rr::Pattern::unit_random(state);
        const float v = std::fmin(1.0f, a * a * 1.3f);
        px[i] = static_cast<std::uint8_t>(v * 255);
        px[i + 1] = static_cast<std::uint8_t>(v * 200);
        px[i + 2] = static_cast<std::uint8_t>(v * 120);
        px[i + 3] = 255;
    }
    Com<ID3D11Texture2D> src = r.tex_data(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, px);
    Com<ID3D11Buffer> hist = r.buf(64 * 4, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS);
    Com<ID3D11ComputeShader> cs = r.cs(
        "Texture2D<float4> t : register(t0);\nRWByteAddressBuffer h : register(u0);\n[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
        "  float l = dot(t.Load(int3(id.xy, 0)).rgb, float3(0.3, 0.59, 0.11)); uint bin = min(63, uint(l * 64)); uint o; h.InterlockedAdd(bin * 4, 1, o); }\n");
    Com<ID3D11PixelShader> bars = r.ps(
        "ByteAddressBuffer h : register(t0);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
        "float4 main(VO i) : SV_Target { uint bin = min(63, uint(i.uv.x * 64)); float v = h.Load(bin * 4) / 160.0; return (1 - i.uv.y) < v ? float4(0.9, 0.7, 0.2, 1) : float4(0.1, 0.1, 0.15, 1); }\n");
    if (!src || !hist || !cs || !bars) return false;
    Com<ID3D11ShaderResourceView> s = r.srv(src.get());
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_R32_TYPELESS;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = 64;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    Com<ID3D11UnorderedAccessView> u = r.uav(hist.get(), &ud);
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32_TYPELESS;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    sd.BufferEx.NumElements = 64;
    sd.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
    Com<ID3D11ShaderResourceView> hv = r.srv(hist.get(), &sd);
    if (!s || !u || !hv) return false;
    const UINT zero[4] = {0, 0, 0, 0};
    r.ctx->ClearUnorderedAccessViewUint(u.get(), zero);
    ID3D11ShaderResourceView* sv = s.get();
    ID3D11UnorderedAccessView* uv = u.get();
    r.ctx->CSSetShader(cs.get(), nullptr, 0);
    r.ctx->CSSetShaderResources(0, 1, &sv);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &uv, nullptr);
    r.ctx->Dispatch(8, 8, 1);
    unbind_cs(r);
    Target target;
    if (!r.color(target)) return false;
    r.bind(target);
    ID3D11ShaderResourceView* hvv = hv.get();
    r.ctx->CSSetShader(nullptr, nullptr, 0);
    r.ctx->PSSetShaderResources(0, 1, &hvv);
    r.draw_fs(bars.get());
    if (!r.shot(out, target)) return false;
    return words_image(r, hist.get(), 64, out, "histogram");
});

const Add kAppendConsume(spec("cs", "append_consume_buffers").tolerance(rr::Tolerance::exact()), [](Rig& r, Cap& out) {
    Com<ID3D11Buffer> list = r.buf(256 * 8, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 8);
    Com<ID3D11Buffer> sums = r.buf(16, D3D11_BIND_UNORDERED_ACCESS, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS);
    Com<ID3D11Buffer> count = r.buf(16, D3D11_BIND_UNORDERED_ACCESS, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS);
    Com<ID3D11ComputeShader> produce = r.cs(
        "AppendStructuredBuffer<uint2> a : register(u0);\n[numthreads(16, 1, 1)] void main(uint3 id : SV_DispatchThreadID) { if ((id.x % 3) != 1) a.Append(uint2(id.x, id.x * id.x)); }\n");
    Com<ID3D11ComputeShader> consume = r.cs(
        "ConsumeStructuredBuffer<uint2> c : register(u0);\nRWByteAddressBuffer sums : register(u1);\n[numthreads(1, 1, 1)] void main() {\n"
        "  uint total = 0; uint x = 0; for (int k = 0; k < 256; ++k) { uint2 v = c.Consume(); total += v.y; x ^= v.x; } uint o; sums.InterlockedAdd(0, total, o); sums.InterlockedXor(4, x, o); }\n");
    if (!list || !sums || !count || !produce || !consume) return false;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_UNKNOWN;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = 256;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
    Com<ID3D11UnorderedAccessView> au = r.uav(list.get(), &ud);
    D3D11_UNORDERED_ACCESS_VIEW_DESC rd{};
    rd.Format = DXGI_FORMAT_R32_TYPELESS;
    rd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    rd.Buffer.NumElements = 4;
    rd.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    Com<ID3D11UnorderedAccessView> su = r.uav(sums.get(), &rd);
    if (!au || !su) return false;
    const UINT zero[4] = {0, 0, 0, 0};
    r.ctx->ClearUnorderedAccessViewUint(su.get(), zero);
    ID3D11UnorderedAccessView* a = au.get();
    UINT start = 0;
    r.ctx->CSSetShader(produce.get(), nullptr, 0);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &a, &start);
    r.ctx->Dispatch(1, 1, 1);
    r.ctx->CopyStructureCount(count.get(), 0, au.get());
    ID3D11UnorderedAccessView* both[2] = {au.get(), su.get()};
    UINT starts[2] = {0xFFFFFFFFu, 0xFFFFFFFFu};
    r.ctx->CSSetShader(consume.get(), nullptr, 0);
    r.ctx->CSSetUnorderedAccessViews(0, 2, both, starts);
    r.ctx->Dispatch(1, 1, 1);
    unbind_cs(r);
    Bytes cb;
    if (!r.read_bytes(count.get(), cb)) return false;
    Bytes sb;
    if (!r.read_bytes(sums.get(), sb)) return false;
    rr::Image image(2, 1, rr::PixelKind::Rgba32Uint);
    std::memcpy(image.bytes().data(), cb.data(), 4);
    std::memcpy(image.bytes().data() + 16, sb.data(), 8);
    out.add("counts", std::move(image));
    return true;
}, [](Rig& r) { return need_fl(r, D3D_FEATURE_LEVEL_11_0); });

const Add kCounter(spec("cs", "uav_counter_increment_and_copy_structure_count").tolerance(rr::Tolerance::exact()).uniform_ok("counter"), [](Rig& r, Cap& out) {
    Com<ID3D11Buffer> buffer = r.buf(1024 * 4, D3D11_BIND_UNORDERED_ACCESS, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
    Com<ID3D11Buffer> count = r.buf(16, D3D11_BIND_UNORDERED_ACCESS, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS);
    Com<ID3D11ComputeShader> cs = r.cs(
        "RWStructuredBuffer<uint> u : register(u0);\n[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) { if ((id.x * 7) % 5 < 2) { uint k = u.IncrementCounter(); u[k] = 1; } }\n");
    if (!buffer || !count || !cs) return false;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_UNKNOWN;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = 1024;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_COUNTER;
    Com<ID3D11UnorderedAccessView> uav = r.uav(buffer.get(), &ud);
    if (!uav) return false;
    ID3D11UnorderedAccessView* u = uav.get();
    const UINT initial = 0;
    r.ctx->CSSetShader(cs.get(), nullptr, 0);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &u, &initial);
    r.ctx->Dispatch(8, 1, 1);
    r.ctx->CopyStructureCount(count.get(), 0, uav.get());
    unbind_cs(r);
    Bytes bytes, data;
    if (!r.read_bytes(count.get(), bytes) || !r.read_bytes(buffer.get(), data)) return false;
    rr::Image image(2, 1, rr::PixelKind::Rgba32Uint);
    std::memcpy(image.bytes().data(), bytes.data(), 4);
    UINT ones = 0;
    for (UINT i = 0; i < 1024; ++i) {
        UINT v;
        std::memcpy(&v, data.data() + i * 4, 4);
        ones += v == 1;
    }
    std::memcpy(image.bytes().data() + 16, &ones, 4);
    out.add("counter", std::move(image));
    return true;
}, [](Rig& r) { return need_fl(r, D3D_FEATURE_LEVEL_11_0); });

const Add kIndirect(spec("cs", "dispatch_indirect_args_from_compute").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) {
    Com<ID3D11Buffer> args = r.buf(16, D3D11_BIND_UNORDERED_ACCESS, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS | D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS);
    Com<ID3D11Texture2D> tex = r.tex(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
    Com<ID3D11ComputeShader> setup = r.cs("RWByteAddressBuffer a : register(u0);\n[numthreads(1, 1, 1)] void main() { a.Store3(0, uint3(4, 8, 1)); }\n");
    Com<ID3D11ComputeShader> work = r.cs(
        "RWTexture2D<float4> u : register(u0);\n[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID, uint3 g : SV_GroupID) { u[id.xy] = float4(g.x / 4.0, g.y / 8.0, ((id.x ^ id.y) & 1) * 0.5, 1); }\n");
    if (!args || !tex || !setup || !work) return false;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_R32_TYPELESS;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = 4;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    Com<ID3D11UnorderedAccessView> au = r.uav(args.get(), &ud);
    Com<ID3D11UnorderedAccessView> tu = r.uav(tex.get());
    if (!au || !tu) return false;
    ID3D11UnorderedAccessView* a = au.get();
    r.ctx->CSSetShader(setup.get(), nullptr, 0);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &a, nullptr);
    r.ctx->Dispatch(1, 1, 1);
    unbind_cs(r);
    ID3D11UnorderedAccessView* t = tu.get();
    r.ctx->CSSetShader(work.get(), nullptr, 0);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &t, nullptr);
    r.ctx->DispatchIndirect(args.get(), 0);
    unbind_cs(r);
    return show_texture(r, out, tex.get(), D3D11_FILTER_MIN_MAG_MIP_POINT);
}, [](Rig& r) { return need_fl(r, D3D_FEATURE_LEVEL_11_0); });

bool typed_load(Rig& r, Cap& out, DXGI_FORMAT format, const char* type) {
    Com<ID3D11Texture2D> tex = r.tex(64, 64, format, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
    const std::string decl = std::string(type) == "float4" && format == DXGI_FORMAT_R8G8B8A8_UNORM ? "unorm float4" : type;
    const std::string src = std::string("RWTexture2D<") + decl + "> u : register(u0);\n[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
        "  u[id.xy] = float4(id.x / 64.0, id.y / 64.0, (id.x ^ id.y) / 64.0, 1.0); }\n";
    const std::string inc = std::string("RWTexture2D<") + decl + "> u : register(u0);\n[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
        "  float4 v = u[id.xy]; u[id.xy] = float4(v.y, v.z * 0.5, v.x, 1.0); }\n";
    if (std::string(type) == "float") {
    }
    Com<ID3D11ComputeShader> first = r.cs(src);
    Com<ID3D11ComputeShader> second = r.cs(inc);
    if (!tex || !first || !second) return false;
    Com<ID3D11UnorderedAccessView> uav = r.uav(tex.get());
    if (!uav) return false;
    ID3D11UnorderedAccessView* u = uav.get();
    r.ctx->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
    r.ctx->CSSetShader(first.get(), nullptr, 0);
    r.ctx->Dispatch(8, 8, 1);
    r.ctx->CSSetShader(second.get(), nullptr, 0);
    r.ctx->Dispatch(8, 8, 1);
    unbind_cs(r);
    return show_texture(r, out, tex.get(), D3D11_FILTER_MIN_MAG_MIP_POINT);
}

const Add kTypedLoadRgba32(spec("cs", "typed_uav_load_r32g32b32a32_float_in_place").tolerance(rr::Tolerance::blend(2)),
                           [](Rig& r, Cap& out) { return typed_load(r, out, DXGI_FORMAT_R32G32B32A32_FLOAT, "float4"); },
                           [](Rig& r) -> std::string {
                               D3D11_FEATURE_DATA_D3D11_OPTIONS2 o2{};
                               D3D11_FEATURE_DATA_D3D11_OPTIONS1 o1{};
                               (void)o1;
                               (void)o2;
                               UINT support = 0;
                               if (FAILED(r.dev->CheckFormatSupport(DXGI_FORMAT_R32G32B32A32_FLOAT, &support)) || !(support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW)) return "typed UAV";
                               ID3D11Device* d = r.dev;
                               UINT s2 = 0;
                               ID3D11Device* dd = d;
                               Com<ID3D11Device> keep;
                               (void)keep;
                               D3D11_FEATURE_DATA_FORMAT_SUPPORT2 fs{DXGI_FORMAT_R32G32B32A32_FLOAT, 0};
                               if (FAILED(dd->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &fs, sizeof fs)) || !(fs.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD)) return "typed UAV loads of R32G32B32A32_FLOAT";
                               (void)s2;
                               return {};
                           });

const Add kTypedLoadRgba8(spec("cs", "typed_uav_load_r8g8b8a8_unorm_in_place").tolerance(rr::Tolerance::blend(2)),
                          [](Rig& r, Cap& out) { return typed_load(r, out, DXGI_FORMAT_R8G8B8A8_UNORM, "float4"); },
                          [](Rig& r) -> std::string {
                              D3D11_FEATURE_DATA_FORMAT_SUPPORT2 fs{DXGI_FORMAT_R8G8B8A8_UNORM, 0};
                              if (FAILED(r.dev->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &fs, sizeof fs)) || !(fs.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD)) return "typed UAV loads of R8G8B8A8_UNORM";
                              return {};
                          });

const Add kTypedLoadR32(spec("cs", "typed_uav_load_r32_float_in_place").tolerance(rr::Tolerance::floats(1e-5f)), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> tex = r.tex(64, 64, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_UNORDERED_ACCESS);
    Com<ID3D11ComputeShader> first = r.cs("RWTexture2D<float> u : register(u0);\n[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) { u[id.xy] = (id.x * 3 + id.y * 5) / 64.0; }\n");
    Com<ID3D11ComputeShader> second = r.cs("RWTexture2D<float> u : register(u0);\n[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) { float v = u[id.xy]; u[id.xy] = v * v + 0.25; }\n");
    if (!tex || !first || !second) return false;
    Com<ID3D11UnorderedAccessView> uav = r.uav(tex.get());
    if (!uav) return false;
    ID3D11UnorderedAccessView* u = uav.get();
    r.ctx->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
    r.ctx->CSSetShader(first.get(), nullptr, 0);
    r.ctx->Dispatch(8, 8, 1);
    r.ctx->CSSetShader(second.get(), nullptr, 0);
    r.ctx->Dispatch(8, 8, 1);
    unbind_cs(r);
    rr::Image image;
    if (!r.read_image(tex.get(), DXGI_FORMAT_R32_FLOAT, 0, image)) return false;
    out.add("r32", std::move(image));
    return true;
});

const Add kTexture3d(spec("cs", "write_3d_texture_uav_then_sample_slices").tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    D3D11_TEXTURE3D_DESC d{};
    d.Width = d.Height = d.Depth = 16;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    Com<ID3D11Texture3D> tex;
    if (!r.ok(r.dev->CreateTexture3D(&d, nullptr, tex.put()), "CreateTexture3D")) return false;
    Com<ID3D11ComputeShader> cs = r.cs(
        "RWTexture3D<float4> u : register(u0);\n[numthreads(4, 4, 4)] void main(uint3 id : SV_DispatchThreadID) { float3 p = float3(id) / 16.0; float l = length(p - 0.5); u[id] = float4(p.x, p.y, l < 0.4 ? 1.0 : 0.0, 1); }\n");
    Com<ID3D11PixelShader> ps = r.ps(
        "Texture3D t : register(t0);\nSamplerState s : register(s0);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
        "float4 main(VO i) : SV_Target { return t.Sample(s, float3(i.uv, frac(i.uv.x * 3.0))); }\n");
    Com<ID3D11SamplerState> smp = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP));
    Target target;
    if (!cs || !ps || !smp || !r.color(target)) return false;
    Com<ID3D11UnorderedAccessView> uav = r.uav(tex.get());
    Com<ID3D11ShaderResourceView> srv = r.srv(tex.get());
    if (!uav || !srv) return false;
    ID3D11UnorderedAccessView* u = uav.get();
    r.ctx->CSSetShader(cs.get(), nullptr, 0);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
    r.ctx->Dispatch(4, 4, 4);
    unbind_cs(r);
    r.bind(target);
    ID3D11ShaderResourceView* v = srv.get();
    ID3D11SamplerState* s = smp.get();
    r.ctx->PSSetShaderResources(0, 1, &v);
    r.ctx->PSSetSamplers(0, 1, &s);
    r.draw_fs(ps.get());
    return r.shot(out, target);
});

const Add kGeneratedVertices(spec("cs", "compute_generated_vertex_buffer_then_draw").tolerance(rr::Tolerance::raster()), [](Rig& r, Cap& out) {
    Target target;
    Com<ID3D11Buffer> vb = r.buf(64 * 3 * 20, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_VERTEX_BUFFER, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS);
    Com<ID3D11ComputeShader> cs = r.cs(
        "RWByteAddressBuffer v : register(u0);\n[numthreads(8, 1, 1)] void main(uint3 id : SV_DispatchThreadID) { uint tri = id.x; float a = tri * 0.0981747; float rr = 0.25 + 0.6 * tri / 64.0; float2 c = float2(cos(a * 2.0), sin(a * 2.0)) * rr * 0.9;\n"
        "  for (uint k = 0; k < 3; ++k) { float b = a * 3.0 + k * 2.0944; float2 p = c + float2(cos(b), sin(b)) * 0.12; float3 col = float3(frac(tri * 0.07 + k * 0.33), 0.4 + 0.5 * sin(tri * 0.2), 0.3 + 0.1 * k);\n"
        "    uint base = (tri * 3 + k) * 20; v.Store2(base, asuint(p)); v.Store3(base + 8, asuint(col)); } }\n");
    Basic basic;
    if (!vb || !cs || !basic.make(r) || !r.color(target)) return false;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_R32_TYPELESS;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = 64 * 3 * 5;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    Com<ID3D11UnorderedAccessView> uav = r.uav(vb.get(), &ud);
    if (!uav) return false;
    ID3D11UnorderedAccessView* u = uav.get();
    r.ctx->CSSetShader(cs.get(), nullptr, 0);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
    r.ctx->Dispatch(8, 1, 1);
    unbind_cs(r);
    r.bind(target);
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    basic.vertices(r, vb.get());
    r.ctx->Draw(64 * 3, 0);
    return r.shot(out, target);
});

const Add kRawBuffer(spec("cs", "raw_buffer_uav_store_load_across_dispatches").tolerance(rr::Tolerance::exact()), [](Rig& r, Cap& out) {
    Com<ID3D11Buffer> source = r.buf(256, D3D11_BIND_UNORDERED_ACCESS, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS);
    Com<ID3D11Buffer> result = r.buf(256, D3D11_BIND_UNORDERED_ACCESS, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS);
    Com<ID3D11ComputeShader> first = r.cs("RWByteAddressBuffer b : register(u0);\n[numthreads(16, 1, 1)] void main(uint3 id : SV_DispatchThreadID) { uint i = id.x; b.Store4(i * 16, uint4(i, i * i, i ^ 0xA5A5, ~i)); }\n");
    Com<ID3D11ComputeShader> second = r.cs(
        "RWByteAddressBuffer a : register(u0);\nRWByteAddressBuffer o : register(u1);\n[numthreads(16, 1, 1)] void main(uint3 id : SV_DispatchThreadID) { uint i = id.x; uint4 v = a.Load4(((i + 1) % 16) * 16);\n"
        "  o.Store(i * 16 + 8, v.x + v.y * 3); o.Store2(i * 16, uint2(v.w, v.z)); o.Store(i * 16 + 12, ~i); }\n");
    if (!source || !result || !first || !second) return false;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_R32_TYPELESS;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = 64;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    Com<ID3D11UnorderedAccessView> su = r.uav(source.get(), &ud);
    Com<ID3D11UnorderedAccessView> ru = r.uav(result.get(), &ud);
    if (!su || !ru) return false;
    ID3D11UnorderedAccessView* both[2] = {su.get(), ru.get()};
    r.ctx->CSSetShader(first.get(), nullptr, 0);
    r.ctx->CSSetUnorderedAccessViews(0, 1, both, nullptr);
    r.ctx->Dispatch(1, 1, 1);
    r.ctx->CSSetShader(second.get(), nullptr, 0);
    r.ctx->CSSetUnorderedAccessViews(0, 2, both, nullptr);
    r.ctx->Dispatch(1, 1, 1);
    unbind_cs(r);
    Bytes bytes;
    if (!r.read_bytes(result.get(), bytes)) return false;
    for (unsigned i = 0; i < 16; ++i) {
        const unsigned n = (i + 1) % 16;
        const std::uint32_t v[4] = {n, n * n, n ^ 0xA5A5u, ~n};
        const std::uint32_t expected[4] = {v[3], v[2], v[0] + v[1] * 3, ~i};
        std::uint32_t got[4];
        std::memcpy(got, bytes.data() + i * 16, 16);
        for (int k = 0; k < 4; ++k)
            if (got[k] != expected[k]) return r.fail("raw buffer word " + std::to_string(i * 4 + k) + " is " + std::to_string(got[k]) + " but the model gives " + std::to_string(expected[k]));
    }
    return words_image(r, result.get(), 64, out, "raw");
});

const Add kStructured(spec("cs", "structured_buffer_uav_struct_stride").tolerance(rr::Tolerance::floats(1e-5f)), [](Rig& r, Cap& out) {
    Com<ID3D11Buffer> buffer = r.buf(64 * 32, D3D11_BIND_UNORDERED_ACCESS, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 32);
    Com<ID3D11ComputeShader> cs = r.cs(
        "struct Item { float3 pos; uint id; float4 color; };\nRWStructuredBuffer<Item> b : register(u0);\n"
        "[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) { Item it; it.pos = float3(id.x * 0.5, sqrt(id.x), id.x * id.x * 0.01); it.id = id.x * 3; it.color = float4(id.x / 63.0, 0.5, 1 - id.x / 63.0, 1); b[id.x] = it; }\n");
    if (!buffer || !cs) return false;
    Com<ID3D11UnorderedAccessView> uav = D3d11Tools::structured_uav(r.kit.dev, buffer.get(), 64);
    if (!uav) return r.fail("UAV creation failed");
    ID3D11UnorderedAccessView* u = uav.get();
    r.ctx->CSSetShader(cs.get(), nullptr, 0);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
    r.ctx->Dispatch(1, 1, 1);
    unbind_cs(r);
    Bytes bytes;
    if (!r.read_bytes(buffer.get(), bytes)) return false;
    rr::Image image(8, 64, rr::PixelKind::R32Float);
    std::memcpy(image.bytes().data(), bytes.data(), 64 * 32);
    out.add("items", std::move(image));
    return true;
});

const Add kClearUint(spec("cs", "clear_unordered_access_view_uint").tolerance(rr::Tolerance::exact()).uniform_ok("buffer"), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> tex = r.tex(32, 32, DXGI_FORMAT_R32G32B32A32_UINT, D3D11_BIND_UNORDERED_ACCESS);
    Com<ID3D11Buffer> buf = r.buf(256, D3D11_BIND_UNORDERED_ACCESS, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS);
    Com<ID3D11ComputeShader> cs = r.cs("RWTexture2D<uint4> u : register(u0);\n[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) { if ((id.x ^ id.y) & 4) u[id.xy] = u[id.xy] + uint4(id.x, id.y, 7, 0); }\n");
    if (!tex || !buf || !cs) return false;
    Com<ID3D11UnorderedAccessView> tu = r.uav(tex.get());
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_R32_TYPELESS;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = 64;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    Com<ID3D11UnorderedAccessView> bu = r.uav(buf.get(), &ud);
    if (!tu || !bu) return false;
    const UINT values[4] = {11, 22, 33, 44};
    r.ctx->ClearUnorderedAccessViewUint(tu.get(), values);
    const UINT fill[4] = {0xDEADBEEFu, 0, 0, 0};
    r.ctx->ClearUnorderedAccessViewUint(bu.get(), fill);
    ID3D11UnorderedAccessView* u = tu.get();
    r.ctx->CSSetShader(cs.get(), nullptr, 0);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
    r.ctx->Dispatch(4, 4, 1);
    unbind_cs(r);
    rr::Image image;
    if (!r.read_image(tex.get(), DXGI_FORMAT_R32G32B32A32_UINT, 0, image)) return false;
    out.add("texture", std::move(image));
    return words_image(r, buf.get(), 64, out, "buffer");
});

const Add kClearFloat(spec("cs", "clear_unordered_access_view_float").tolerance(rr::Tolerance::blend(1)), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> tex = r.tex(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
    Com<ID3D11ComputeShader> cs = r.cs("RWTexture2D<float4> u : register(u0);\n[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) { if (id.x > id.y) u[id.xy] = float4(id.x / 32.0, 0.2, id.y / 32.0, 1); }\n");
    if (!tex || !cs) return false;
    Com<ID3D11UnorderedAccessView> uav = r.uav(tex.get());
    if (!uav) return false;
    const float color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    r.ctx->ClearUnorderedAccessViewFloat(uav.get(), color);
    ID3D11UnorderedAccessView* u = uav.get();
    r.ctx->CSSetShader(cs.get(), nullptr, 0);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
    r.ctx->Dispatch(4, 4, 1);
    unbind_cs(r);
    return show_texture(r, out, tex.get(), D3D11_FILTER_MIN_MAG_MIP_POINT);
});

const Add kScan(spec("cs", "groupshared_prefix_sum_scan").tolerance(rr::Tolerance::exact()), [](Rig& r, Cap& out) {
    std::vector<UINT> input(256);
    for (UINT i = 0; i < 256; ++i) input[i] = (i * 7 + 3) % 11;
    Com<ID3D11Buffer> in = r.buf(1024, D3D11_BIND_SHADER_RESOURCE, input.data(), D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
    Com<ID3D11Buffer> result = r.buf(1024, D3D11_BIND_UNORDERED_ACCESS, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
    Com<ID3D11ComputeShader> cs = r.cs(
        "StructuredBuffer<uint> a : register(t0);\nRWStructuredBuffer<uint> o : register(u0);\ngroupshared uint tmp[2][256];\n"
        "[numthreads(256, 1, 1)] void main(uint3 t : SV_GroupThreadID) { uint i = t.x; uint src = 0; tmp[0][i] = a[i]; GroupMemoryBarrierWithGroupSync();\n"
        "  for (uint d = 1; d < 256; d <<= 1) { uint v = tmp[src][i]; if (i >= d) v += tmp[src][i - d]; tmp[1 - src][i] = v; src = 1 - src; GroupMemoryBarrierWithGroupSync(); } o[i] = tmp[src][i]; }\n");
    if (!in || !result || !cs) return false;
    Com<ID3D11ShaderResourceView> sv = D3d11Tools::structured_srv(r.kit.dev, in.get(), 0, 256);
    Com<ID3D11UnorderedAccessView> uv = D3d11Tools::structured_uav(r.kit.dev, result.get(), 256);
    if (!sv || !uv) return r.fail("view creation failed");
    ID3D11ShaderResourceView* s = sv.get();
    ID3D11UnorderedAccessView* u = uv.get();
    r.ctx->CSSetShader(cs.get(), nullptr, 0);
    r.ctx->CSSetShaderResources(0, 1, &s);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
    r.ctx->Dispatch(1, 1, 1);
    unbind_cs(r);
    return words_image(r, result.get(), 256, out, "scan");
});

const Add kReduction(spec("cs", "groupshared_reduction_sum_min_max").tolerance(rr::Tolerance::exact()), [](Rig& r, Cap& out) {
    std::vector<UINT> input(1024);
    for (UINT i = 0; i < 1024; ++i) input[i] = (i * 2654435761u) >> 20;
    Com<ID3D11Buffer> in = r.buf(4096, D3D11_BIND_SHADER_RESOURCE, input.data(), D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
    Com<ID3D11Buffer> result = r.buf(64, D3D11_BIND_UNORDERED_ACCESS, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
    Com<ID3D11ComputeShader> cs = r.cs(
        "StructuredBuffer<uint> a : register(t0);\nRWStructuredBuffer<uint> o : register(u0);\ngroupshared uint sum[256]; groupshared uint lo[256]; groupshared uint hi[256];\n"
        "[numthreads(256, 1, 1)] void main(uint3 g : SV_GroupID, uint3 t : SV_GroupThreadID) { uint i = g.x * 256 + t.x; uint v = a[i]; sum[t.x] = v; lo[t.x] = v; hi[t.x] = v; GroupMemoryBarrierWithGroupSync();\n"
        "  for (uint s = 128; s > 0; s >>= 1) { if (t.x < s) { sum[t.x] += sum[t.x + s]; lo[t.x] = min(lo[t.x], lo[t.x + s]); hi[t.x] = max(hi[t.x], hi[t.x + s]); } GroupMemoryBarrierWithGroupSync(); }\n"
        "  if (t.x == 0) { o[g.x * 4] = sum[0]; o[g.x * 4 + 1] = lo[0]; o[g.x * 4 + 2] = hi[0]; o[g.x * 4 + 3] = g.x; } }\n");
    if (!in || !result || !cs) return false;
    Com<ID3D11ShaderResourceView> sv = D3d11Tools::structured_srv(r.kit.dev, in.get(), 0, 1024);
    Com<ID3D11UnorderedAccessView> uv = D3d11Tools::structured_uav(r.kit.dev, result.get(), 16);
    if (!sv || !uv) return r.fail("view creation failed");
    ID3D11ShaderResourceView* s = sv.get();
    ID3D11UnorderedAccessView* u = uv.get();
    r.ctx->CSSetShader(cs.get(), nullptr, 0);
    r.ctx->CSSetShaderResources(0, 1, &s);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
    r.ctx->Dispatch(4, 1, 1);
    unbind_cs(r);
    return words_image(r, result.get(), 16, out, "reduction");
});

const Add kThreadIds(spec("cs", "thread_group_ids_numthreads_4x4x2").tolerance(rr::Tolerance::tight(1)), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> tex = r.tex(32, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
    Com<ID3D11ComputeShader> cs = r.cs(
        "RWTexture2D<float4> u : register(u0);\n[numthreads(4, 4, 2)] void main(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {\n"
        "  if (tid.z == 1) { u[uint2(dtid.x, dtid.y + 32)] = float4(gid.x / 8.0, gid.y / 8.0, gi / 32.0, 1); }\n"
        "  else { u[dtid.xy] = float4(tid.x / 4.0, tid.y / 4.0, 0.3, 1); } }\n");
    if (!tex || !cs) return false;
    Com<ID3D11UnorderedAccessView> uav = r.uav(tex.get());
    if (!uav) return false;
    ID3D11UnorderedAccessView* u = uav.get();
    r.ctx->CSSetShader(cs.get(), nullptr, 0);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
    r.ctx->Dispatch(8, 8, 1);
    unbind_cs(r);
    return show_texture(r, out, tex.get(), D3D11_FILTER_MIN_MAG_MIP_POINT);
});

const Add kDownsample(spec("cs", "sample_level_and_mip_downsample_chain").tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> tex = r.tex_data(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, rr::Pattern::noise(64, 64, 9), D3D11_BIND_SHADER_RESOURCE);
    Com<ID3D11Texture2D> half = r.tex(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
    Com<ID3D11Texture2D> quarter = r.tex(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
    const char* source =
        "Texture2D<float4> t : register(t0);\nRWTexture2D<float4> u : register(u0);\nSamplerState s : register(s0);\ncbuffer C : register(b0) { float4 k; };\n"
        "[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) { float2 uv = (float2(id.xy) + 0.5) * k.x; u[id.xy] = t.SampleLevel(s, uv, 0); }\n";
    Com<ID3D11ComputeShader> cs = r.cs(source);
    Com<ID3D11SamplerState> smp = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP));
    Com<ID3D11Buffer> cb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
    if (!tex || !half || !quarter || !cs || !smp || !cb) return false;
    Com<ID3D11ShaderResourceView> s0 = r.srv(tex.get());
    Com<ID3D11ShaderResourceView> s1 = r.srv(half.get());
    Com<ID3D11UnorderedAccessView> u1 = r.uav(half.get());
    Com<ID3D11UnorderedAccessView> u2 = r.uav(quarter.get());
    if (!s0 || !s1 || !u1 || !u2) return false;
    ID3D11SamplerState* sp = smp.get();
    ID3D11Buffer* c = cb.get();
    r.ctx->CSSetShader(cs.get(), nullptr, 0);
    r.ctx->CSSetSamplers(0, 1, &sp);
    r.ctx->CSSetConstantBuffers(0, 1, &c);
    const float k1[4] = {1.0f / 32.0f, 0, 0, 0};
    r.ctx->UpdateSubresource(cb.get(), 0, nullptr, k1, 0, 0);
    ID3D11ShaderResourceView* v0 = s0.get();
    ID3D11UnorderedAccessView* uu1 = u1.get();
    r.ctx->CSSetShaderResources(0, 1, &v0);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &uu1, nullptr);
    r.ctx->Dispatch(4, 4, 1);
    unbind_cs(r);
    const float k2[4] = {1.0f / 16.0f, 0, 0, 0};
    r.ctx->UpdateSubresource(cb.get(), 0, nullptr, k2, 0, 0);
    ID3D11ShaderResourceView* v1 = s1.get();
    ID3D11UnorderedAccessView* uu2 = u2.get();
    r.ctx->CSSetShaderResources(0, 1, &v1);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &uu2, nullptr);
    r.ctx->Dispatch(2, 2, 1);
    unbind_cs(r);
    return show_texture(r, out, quarter.get(), D3D11_FILTER_MIN_MAG_MIP_POINT);
});

const Add kSharedAtomics(spec("cs", "groupshared_interlocked_per_group_then_global").tolerance(rr::Tolerance::exact()), [](Rig& r, Cap& out) {
    Com<ID3D11Buffer> buffer = r.buf(64 * 4, D3D11_BIND_UNORDERED_ACCESS, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
    Com<ID3D11ComputeShader> cs = r.cs(
        "RWStructuredBuffer<uint> o : register(u0);\ngroupshared uint bins[8];\n[numthreads(64, 1, 1)] void main(uint3 g : SV_GroupID, uint3 t : SV_GroupThreadID, uint3 id : SV_DispatchThreadID) {\n"
        "  if (t.x < 8) bins[t.x] = 0; GroupMemoryBarrierWithGroupSync(); uint b = (id.x * 5 + g.x) & 7; InterlockedAdd(bins[b], id.x & 15); GroupMemoryBarrierWithGroupSync();\n"
        "  if (t.x < 8) InterlockedAdd(o[t.x], bins[t.x]); if (t.x == 0) InterlockedMax(o[8], g.x * 1000 + bins[0]); }\n");
    if (!buffer || !cs) return false;
    Com<ID3D11UnorderedAccessView> uav = D3d11Tools::structured_uav(r.kit.dev, buffer.get(), 64);
    if (!uav) return r.fail("UAV creation failed");
    const UINT zero[4] = {0, 0, 0, 0};
    r.ctx->ClearUnorderedAccessViewUint(uav.get(), zero);
    ID3D11UnorderedAccessView* u = uav.get();
    r.ctx->CSSetShader(cs.get(), nullptr, 0);
    r.ctx->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
    r.ctx->Dispatch(16, 1, 1);
    unbind_cs(r);
    return words_image(r, buffer.get(), 16, out, "bins");
});

}  // namespace

}  // namespace lsw::suites::render::d3d11
