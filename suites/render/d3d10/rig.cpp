#include "render/d3d10/rig.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::render::d3d10 {

namespace {

class FnScene : public D3d10SingleScene {
public:
    FnScene(Body body, Caps caps) : body_(std::move(body)), caps_(std::move(caps)) {}

    std::string unsupported(Kit& kit) override {
        if (!caps_) {
            return {};
        }
        Rig rig(kit);
        return caps_(rig);
    }

    bool draw(Kit& kit, rr::CaptureSet& out, std::string& error) override {
        Rig rig(kit);
        body_(rig, out);
        error = rig.error;
        return rig.ok();
    }

private:
    Body body_;
    Caps caps_;
};

float unpack_small_float(std::uint32_t bits, unsigned mantissa_bits) {
    const unsigned exponent = bits >> mantissa_bits;
    const std::uint32_t mantissa = bits & ((1u << mantissa_bits) - 1u);
    const float scale = static_cast<float>(1u << mantissa_bits);
    if (exponent == 0) {
        return static_cast<float>(mantissa) / scale * std::ldexp(1.0f, -14);
    }
    if (exponent == 31) {
        return mantissa != 0 ? std::nanf("") : INFINITY;
    }
    return (1.0f + static_cast<float>(mantissa) / scale) * std::ldexp(1.0f, static_cast<int>(exponent) - 15);
}

float snorm(std::int32_t value, std::int32_t max_value) { return std::max(static_cast<float>(value) / static_cast<float>(max_value), -1.0f); }

UINT texel_bytes(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R8_UNORM:
        case DXGI_FORMAT_R8_UINT:
        case DXGI_FORMAT_R8_SNORM:
        case DXGI_FORMAT_R8_SINT:
        case DXGI_FORMAT_A8_UNORM:
            return 1;
        case DXGI_FORMAT_R8G8_UNORM:
        case DXGI_FORMAT_R8G8_UINT:
        case DXGI_FORMAT_R8G8_SNORM:
        case DXGI_FORMAT_R16_UNORM:
        case DXGI_FORMAT_R16_FLOAT:
        case DXGI_FORMAT_R16_UINT:
        case DXGI_FORMAT_R16_SNORM:
        case DXGI_FORMAT_R16_SINT:
        case DXGI_FORMAT_B5G6R5_UNORM:
        case DXGI_FORMAT_B5G5R5A1_UNORM:
        case DXGI_FORMAT_D16_UNORM:
            return 2;
        case DXGI_FORMAT_R16G16B16A16_UNORM:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UINT:
        case DXGI_FORMAT_R16G16B16A16_SNORM:
        case DXGI_FORMAT_R16G16B16A16_SINT:
        case DXGI_FORMAT_R32G32_FLOAT:
        case DXGI_FORMAT_R32G32_UINT:
        case DXGI_FORMAT_R32G32_SINT:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return 8;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_UINT:
        case DXGI_FORMAT_R32G32B32A32_SINT:
            return 16;
        default:
            return 4;
    }
}

}  // namespace

void single(rr::CaseSpec spec, Body body, Caps caps) {
    auto shared_body = std::make_shared<Body>(std::move(body));
    rr::CaseTable<Kit>::instance().add(std::move(spec), [shared_body, caps] {
        return std::unique_ptr<rr::Scene<Kit>>(new FnScene(*shared_body, caps));
    });
}

rr::CaseSpec spec(const char* group, const std::string& name, unsigned variant) {
    rr::CaseSpec out(group, name);
    out.variant(variant);
    return out;
}

Rig::~Rig() {
    for (IUnknown* object : keep_) {
        object->Release();
    }
}

Rig::Rig(Kit& k) : kit(k), d(k.device()), sm(k.level_10_1 ? "4_1" : "4_0") {}

void Rig::fail(const std::string& text) {
    if (error.empty()) {
        error = text;
    }
}

void Rig::check(HRESULT result, const char* what) {
    if (FAILED(result)) {
        fail(std::string(what) + ": " + D3d10Tools::hr(result));
    }
}

Com<ID3D10VertexShader> Rig::vs(const std::string& source) {
    std::string e;
    std::vector<std::uint8_t> code;
    if (!D3d10Tools::compile(source, (std::string("vs_") + sm).c_str(), code, e)) {
        fail(e);
        return {};
    }
    vs_bytes = code;
    Com<ID3D10VertexShader> out;
    check(d->CreateVertexShader(code.data(), code.size(), out.put()), "CreateVertexShader");
    return hold(std::move(out));
}

Com<ID3D10PixelShader> Rig::ps(const std::string& source) {
    std::string e;
    std::vector<std::uint8_t> code;
    if (!D3d10Tools::compile(source, (std::string("ps_") + sm).c_str(), code, e)) {
        fail(e);
        return {};
    }
    Com<ID3D10PixelShader> out;
    check(d->CreatePixelShader(code.data(), code.size(), out.put()), "CreatePixelShader");
    return hold(std::move(out));
}

Com<ID3D10GeometryShader> Rig::gs(const std::string& source) {
    std::string e;
    std::vector<std::uint8_t> code;
    if (!D3d10Tools::compile(source, (std::string("gs_") + sm).c_str(), code, e)) {
        fail(e);
        return {};
    }
    Com<ID3D10GeometryShader> out;
    check(d->CreateGeometryShader(code.data(), code.size(), out.put()), "CreateGeometryShader");
    return hold(std::move(out));
}

Com<ID3D10GeometryShader> Rig::gs_so(const std::string& source, const std::vector<D3D10_SO_DECLARATION_ENTRY>& decl, UINT stride) {
    std::string e;
    std::vector<std::uint8_t> code;
    const std::string profile = std::string(source.empty() ? "vs_" : "gs_") + sm;
    if (source.empty()) {
        code = vs_bytes;
    } else if (!D3d10Tools::compile(source, profile.c_str(), code, e)) {
        fail(e);
        return {};
    }
    Com<ID3D10GeometryShader> out;
    check(d->CreateGeometryShaderWithStreamOutput(code.data(), code.size(), decl.data(), static_cast<UINT>(decl.size()), stride, out.put()),
          "CreateGeometryShaderWithStreamOutput");
    return hold(std::move(out));
}

Com<ID3D10InputLayout> Rig::layout(const std::vector<D3D10_INPUT_ELEMENT_DESC>& elements) {
    Com<ID3D10InputLayout> out;
    check(d->CreateInputLayout(elements.data(), static_cast<UINT>(elements.size()), vs_bytes.data(), vs_bytes.size(), out.put()),
          "CreateInputLayout");
    return hold(std::move(out));
}

Com<ID3D10InputLayout> Rig::std_layout() {
    return layout({{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
                   {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0},
                   {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D10_INPUT_PER_VERTEX_DATA, 0}});
}

Com<ID3D10Buffer> Rig::buf(UINT size, D3D10_USAGE usage, UINT bind, UINT cpu, const void* data) {
    std::string e;
    Com<ID3D10Buffer> out = kit.buffer(size, usage, bind, cpu, data, e);
    if (!out) {
        fail(e);
    }
    return hold(std::move(out));
}

Com<ID3D10Buffer> Rig::vbuf(const void* data, UINT size, UINT extra_bind) {
    return buf(size, D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER | extra_bind, 0, data);
}

Com<ID3D10Buffer> Rig::ibuf16(const std::vector<std::uint16_t>& v) {
    return buf(static_cast<UINT>(v.size() * 2), D3D10_USAGE_DEFAULT, D3D10_BIND_INDEX_BUFFER, 0, v.data());
}

Com<ID3D10Buffer> Rig::ibuf32(const std::vector<std::uint32_t>& v) {
    return buf(static_cast<UINT>(v.size() * 4), D3D10_USAGE_DEFAULT, D3D10_BIND_INDEX_BUFFER, 0, v.data());
}

Com<ID3D10Buffer> Rig::cbuf(UINT size, const void* data) {
    return buf(size, D3D10_USAGE_DEFAULT, D3D10_BIND_CONSTANT_BUFFER, 0, data);
}

D3D10_RASTERIZER_DESC Rig::raster_desc() {
    D3D10_RASTERIZER_DESC desc{};
    desc.FillMode = D3D10_FILL_SOLID;
    desc.CullMode = D3D10_CULL_NONE;
    desc.DepthClipEnable = TRUE;
    return desc;
}

D3D10_DEPTH_STENCIL_DESC Rig::depth_desc() {
    D3D10_DEPTH_STENCIL_DESC desc{};
    desc.DepthEnable = FALSE;
    desc.DepthWriteMask = D3D10_DEPTH_WRITE_MASK_ALL;
    desc.DepthFunc = D3D10_COMPARISON_LESS;
    desc.StencilReadMask = 0xff;
    desc.StencilWriteMask = 0xff;
    desc.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_COMPARISON_ALWAYS};
    desc.BackFace = desc.FrontFace;
    return desc;
}

D3D10_BLEND_DESC Rig::blend_desc() {
    D3D10_BLEND_DESC desc{};
    desc.SrcBlend = D3D10_BLEND_ONE;
    desc.DestBlend = D3D10_BLEND_ZERO;
    desc.BlendOp = D3D10_BLEND_OP_ADD;
    desc.SrcBlendAlpha = D3D10_BLEND_ONE;
    desc.DestBlendAlpha = D3D10_BLEND_ZERO;
    desc.BlendOpAlpha = D3D10_BLEND_OP_ADD;
    for (UINT i = 0; i < 8; ++i) {
        desc.RenderTargetWriteMask[i] = D3D10_COLOR_WRITE_ENABLE_ALL;
    }
    return desc;
}

D3D10_SAMPLER_DESC Rig::sampler_desc(D3D10_FILTER filter, D3D10_TEXTURE_ADDRESS_MODE mode) {
    D3D10_SAMPLER_DESC desc{};
    desc.Filter = filter;
    desc.AddressU = desc.AddressV = desc.AddressW = mode;
    desc.MaxAnisotropy = 1;
    desc.ComparisonFunc = D3D10_COMPARISON_NEVER;
    desc.MaxLOD = D3D10_FLOAT32_MAX;
    return desc;
}

Com<ID3D10RasterizerState> Rig::raster(const D3D10_RASTERIZER_DESC& desc) {
    Com<ID3D10RasterizerState> out;
    check(d->CreateRasterizerState(&desc, out.put()), "CreateRasterizerState");
    return hold(std::move(out));
}

Com<ID3D10DepthStencilState> Rig::depth(const D3D10_DEPTH_STENCIL_DESC& desc) {
    Com<ID3D10DepthStencilState> out;
    check(d->CreateDepthStencilState(&desc, out.put()), "CreateDepthStencilState");
    return hold(std::move(out));
}

Com<ID3D10BlendState> Rig::blend(const D3D10_BLEND_DESC& desc) {
    Com<ID3D10BlendState> out;
    check(d->CreateBlendState(&desc, out.put()), "CreateBlendState");
    return hold(std::move(out));
}

Com<ID3D10BlendState1> Rig::blend1(const D3D10_BLEND_DESC1& desc) {
    Com<ID3D10BlendState1> out;
    if (kit.dev.device1 == nullptr) {
        fail("no ID3D10Device1");
        return hold(std::move(out));
    }
    check(kit.dev.device1->CreateBlendState1(&desc, out.put()), "CreateBlendState1");
    return hold(std::move(out));
}

Com<ID3D10SamplerState> Rig::sampler(const D3D10_SAMPLER_DESC& desc) {
    Com<ID3D10SamplerState> out;
    check(d->CreateSamplerState(&desc, out.put()), "CreateSamplerState");
    return hold(std::move(out));
}

Com<ID3D10Texture2D> Rig::tex2d(UINT width, UINT height, DXGI_FORMAT format, UINT bind, UINT mips, UINT array_size,
                                const std::vector<const void*>& data, const std::vector<UINT>& pitches, UINT misc) {
    D3D10_TEXTURE2D_DESC desc = D3d10Tools::desc2d(width, height, format, bind, D3D10_USAGE_DEFAULT, 0, mips, array_size);
    desc.MiscFlags = misc;
    std::vector<D3D10_SUBRESOURCE_DATA> init(data.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
        init[i].pSysMem = data[i];
        init[i].SysMemPitch = i < pitches.size() ? pitches[i] : 0;
    }
    HRESULT result = S_OK;
    Com<ID3D10Texture2D> out = D3d10Tools::texture(kit.dev, desc, init.empty() ? nullptr : init.data(), &result);
    if (!out) {
        fail("CreateTexture2D: " + D3d10Tools::hr(result));
    }
    return hold(std::move(out));
}

Com<ID3D10Texture2D> Rig::rgba_tex(UINT width, UINT height, const rr::Pattern::Bytes& bytes, bool mips, bool srgb) {
    const unsigned levels = mips ? rr::Pattern::level_count(width, height) : 1;
    const std::vector<rr::Pattern::Bytes> chain = rr::Pattern::mip_chain(bytes, width, height, levels);
    std::vector<const void*> data;
    std::vector<UINT> pitch;
    for (unsigned i = 0; i < levels; ++i) {
        data.push_back(chain[i].data());
        pitch.push_back(std::max(1u, width >> i) * 4);
    }
    return tex2d(width, height, srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_SHADER_RESOURCE, levels, 1,
                 data, pitch);
}

Com<ID3D10ShaderResourceView> Rig::srv(ID3D10Resource* resource, const D3D10_SHADER_RESOURCE_VIEW_DESC* desc) {
    Com<ID3D10ShaderResourceView> out;
    check(d->CreateShaderResourceView(resource, desc, out.put()), "CreateShaderResourceView");
    return hold(std::move(out));
}

Com<ID3D10RenderTargetView> Rig::rtv(ID3D10Resource* resource, const D3D10_RENDER_TARGET_VIEW_DESC* desc) {
    Com<ID3D10RenderTargetView> out;
    check(d->CreateRenderTargetView(resource, desc, out.put()), "CreateRenderTargetView");
    return hold(std::move(out));
}

Com<ID3D10DepthStencilView> Rig::dsv(ID3D10Resource* resource, const D3D10_DEPTH_STENCIL_VIEW_DESC* desc) {
    Com<ID3D10DepthStencilView> out;
    check(d->CreateDepthStencilView(resource, desc, out.put()), "CreateDepthStencilView");
    return hold(std::move(out));
}

void Rig::target(Target& out, UINT width, UINT height, DXGI_FORMAT format, UINT samples) {
    std::string e;
    if (!kit.make_target(out, width, height, format, samples, e)) {
        fail(e);
    }
}

void Rig::depth_target(Depth& out, UINT width, UINT height, DXGI_FORMAT format, UINT samples) {
    std::string e;
    if (!kit.make_depth(out, width, height, format, samples, e)) {
        fail(e);
    }
}

void Rig::begin(UINT width, UINT height, DXGI_FORMAT format, bool with_depth, const float* clear_color) {
    target(main, width, height, format);
    if (with_depth) {
        depth_target(depth_main, width, height);
    }
    if (!ok()) {
        return;
    }
    bind_rt(main, with_depth ? &depth_main : nullptr);
    no_cull();
    static const float def[4] = {0.1f, 0.1f, 0.15f, 1.0f};
    const float* c = clear_color != nullptr ? clear_color : def;
    clear(main, c[0], c[1], c[2], c[3]);
    if (with_depth) {
        clear_depth(depth_main);
    }
    viewport(width, height);
}

namespace {
bool compile_effect(const std::string& source, const char* profile, UINT effect_flags, std::vector<std::uint8_t>& code, std::string& error) {
    using Compile = HRESULT(WINAPI*)(const void*, SIZE_T, const char*, const D3D_SHADER_MACRO*, ID3DInclude*, const char*, const char*, UINT, UINT, ID3DBlob**, ID3DBlob**);
    static HMODULE module = ::LoadLibraryA("d3dcompiler_47.dll");
    const Compile compile = module != nullptr ? reinterpret_cast<Compile>(reinterpret_cast<void*>(::GetProcAddress(module, "D3DCompile"))) : nullptr;
    if (compile == nullptr) {
        error = "d3dcompiler_47.dll has no D3DCompile";
        return false;
    }
    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    const HRESULT result = compile(source.data(), source.size(), "effect", nullptr, nullptr, "", profile, 0, effect_flags, &blob, &errors);
    if (FAILED(result)) {
        error = std::string("compiling ") + profile + " failed: " + D3d10Tools::hr(result);
        if (errors != nullptr) {
            error += " " + std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
            errors->Release();
        }
        return false;
    }
    if (errors != nullptr) errors->Release();
    code.assign(static_cast<const std::uint8_t*>(blob->GetBufferPointer()), static_cast<const std::uint8_t*>(blob->GetBufferPointer()) + blob->GetBufferSize());
    blob->Release();
    return true;
}

HMODULE effects_library() {
    static HMODULE module = ::LoadLibraryA("d3d10.dll");
    return module;
}
}  // namespace

Com<ID3D10Effect> Rig::effect(const std::string& source, const char* profile, ID3D10EffectPool* pool) {
    using Create = HRESULT(WINAPI*)(void*, SIZE_T, UINT, ID3D10Device*, ID3D10EffectPool*, ID3D10Effect**);
    Com<ID3D10Effect> out;
    const Create create = reinterpret_cast<Create>(reinterpret_cast<void*>(::GetProcAddress(effects_library(), "D3D10CreateEffectFromMemory")));
    if (create == nullptr) {
        fail("d3d10.dll has no D3D10CreateEffectFromMemory");
        return out;
    }
    std::vector<std::uint8_t> code;
    std::string e;
    if (!compile_effect(source, profile, pool != nullptr ? 1u : 0u, code, e)) {
        fail(e);
        return out;
    }
    check(create(code.data(), code.size(), pool != nullptr ? 1u : 0u, d, pool, out.put()), "D3D10CreateEffectFromMemory");
    return hold(std::move(out));
}

Com<ID3D10EffectPool> Rig::effect_pool(const std::string& source, const char* profile) {
    using Create = HRESULT(WINAPI*)(void*, SIZE_T, UINT, ID3D10Device*, ID3D10EffectPool**);
    Com<ID3D10EffectPool> out;
    const Create create = reinterpret_cast<Create>(reinterpret_cast<void*>(::GetProcAddress(effects_library(), "D3D10CreateEffectPoolFromMemory")));
    if (create == nullptr) {
        fail("d3d10.dll has no D3D10CreateEffectPoolFromMemory");
        return out;
    }
    std::vector<std::uint8_t> code;
    std::string e;
    if (!compile_effect(source, profile, 0, code, e)) {
        fail(e);
        return out;
    }
    check(create(code.data(), code.size(), 0, d, out.put()), "D3D10CreateEffectPoolFromMemory");
    return hold(std::move(out));
}

void Rig::no_cull() {
    default_raster = raster(raster_desc());
    d->RSSetState(default_raster.get());
}

void Rig::bind_rt(Target& target, Depth* depth_target_ptr) {
    ID3D10RenderTargetView* view = target.view.get();
    d->OMSetRenderTargets(1, &view, depth_target_ptr != nullptr ? depth_target_ptr->view.get() : nullptr);
}

void Rig::clear(Target& target, float r, float g, float b, float a) {
    const float c[4] = {r, g, b, a};
    d->ClearRenderTargetView(target.view.get(), c);
}

void Rig::clear_depth(Depth& depth_target_ref, float value, UINT8 stencil, UINT flags) {
    UINT usable = flags;
    if (depth_target_ref.format == DXGI_FORMAT_D32_FLOAT || depth_target_ref.format == DXGI_FORMAT_D16_UNORM) {
        usable &= ~static_cast<UINT>(D3D10_CLEAR_STENCIL);
    }
    d->ClearDepthStencilView(depth_target_ref.view.get(), usable, value, stencil);
}

void Rig::viewport(UINT width, UINT height, float x, float y, float min_depth, float max_depth) {
    D3D10_VIEWPORT vp{};
    vp.TopLeftX = static_cast<INT>(x);
    vp.TopLeftY = static_cast<INT>(y);
    vp.Width = width;
    vp.Height = height;
    vp.MinDepth = min_depth;
    vp.MaxDepth = max_depth;
    d->RSSetViewports(1, &vp);
}

void Rig::use_std(D3D10_PRIMITIVE_TOPOLOGY topology, bool with_uv) {
    Com<ID3D10VertexShader> shader = vs(Std::kVs);
    Com<ID3D10InputLayout> input = std_layout();
    Com<ID3D10PixelShader> pixel = ps(with_uv ? Std::kPsUv : Std::kPs);
    d->IASetInputLayout(input.get());
    d->IASetPrimitiveTopology(topology);
    d->VSSetShader(shader.get());
    d->PSSetShader(pixel.get());
}

void Rig::std_vb(ID3D10Buffer* buffer, UINT slot, UINT offset) {
    const UINT stride = sizeof(Vtx);
    d->IASetVertexBuffers(slot, 1, &buffer, &stride, &offset);
}

void Rig::draw_std(const std::vector<Vtx>& vertices, D3D10_PRIMITIVE_TOPOLOGY topology) {
    Com<ID3D10Buffer> vb = vbuf(vertices);
    std_vb(vb.get());
    d->IASetPrimitiveTopology(topology);
    d->Draw(static_cast<UINT>(vertices.size()), 0);
}

void Rig::fullscreen(ID3D10PixelShader* pixel) {
    Com<ID3D10VertexShader> shader = vs(Std::kFullVs);
    d->IASetInputLayout(nullptr);
    d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d->VSSetShader(shader.get());
    d->PSSetShader(pixel);
    d->Draw(3, 0);
}

void Rig::fullscreen(const std::string& pixel_source) {
    Com<ID3D10PixelShader> pixel = ps(pixel_source);
    if (ok()) {
        fullscreen(pixel.get());
    }
}

void Rig::ps_srv(UINT slot, ID3D10ShaderResourceView* view) { d->PSSetShaderResources(slot, 1, &view); }
void Rig::ps_smp(UINT slot, ID3D10SamplerState* state) { d->PSSetSamplers(slot, 1, &state); }
void Rig::ps_cb(UINT slot, ID3D10Buffer* buffer) { d->PSSetConstantBuffers(slot, 1, &buffer); }
void Rig::vs_cb(UINT slot, ID3D10Buffer* buffer) { d->VSSetConstantBuffers(slot, 1, &buffer); }
void Rig::gs_cb(UINT slot, ID3D10Buffer* buffer) { d->GSSetConstantBuffers(slot, 1, &buffer); }

void Rig::unbind_all() {
    ID3D10ShaderResourceView* none[16] = {};
    d->PSSetShaderResources(0, 16, none);
    d->VSSetShaderResources(0, 16, none);
    d->GSSetShaderResources(0, 16, none);
    ID3D10Buffer* no_buffer[4] = {};
    UINT offsets[4] = {};
    d->SOSetTargets(4, no_buffer, offsets);
}

void Rig::read(Target& target_ref, rr::Image& out) {
    rr::PixelKind kind;
    std::string e;
    if (Convert::kind_of(target_ref.format, kind)) {
        if (!kit.read_color(target_ref, out, e)) {
            fail(e);
        }
        return;
    }
    ID3D10Texture2D* source = target_ref.texture.get();
    if (target_ref.samples > 1) {
        d->ResolveSubresource(target_ref.resolved.get(), 0, target_ref.texture.get(), 0, target_ref.format);
        source = target_ref.resolved.get();
    }
    read_tex(source, 0, out);
}

void Rig::cap(rr::CaptureSet& out, const std::string& label, Target& target_ref) {
    rr::Image image;
    read(target_ref, image);
    if (ok()) {
        out.add(label, std::move(image));
    }
}

void Rig::cap_main(rr::CaptureSet& out, const std::string& label) { cap(out, label, main); }

void Rig::read_tex(ID3D10Texture2D* texture, UINT subresource, rr::Image& out) {
    D3D10_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    const UINT mips = std::max(1u, desc.MipLevels);
    const UINT mip = subresource % mips;
    const UINT width = std::max(1u, desc.Width >> mip);
    const UINT height = std::max(1u, desc.Height >> mip);
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Usage = D3D10_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    Com<ID3D10Texture2D> staging;
    check(d->CreateTexture2D(&desc, nullptr, staging.put()), "staging texture");
    if (!ok()) {
        return;
    }
    d->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, texture, subresource, nullptr);
    D3D10_MAPPED_TEXTURE2D mapped{};
    check(staging->Map(0, D3D10_MAP_READ, 0, &mapped), "Map of staging");
    if (!ok()) {
        return;
    }
    const UINT bytes = texel_bytes(desc.Format);
    if (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
        rr::Image direct(width, height, rr::PixelKind::Rgba8Unorm);
        for (UINT y = 0; y < height; ++y) {
            std::memcpy(direct.row(y), static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch, width * 4);
        }
        staging->Unmap(0);
        out = std::move(direct);
        return;
    }
    rr::Image image(width, height, rr::PixelKind::Rgba32Float);
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            float v[4] = {};
            if (!decode_texel(desc.Format, static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch + x * bytes, v)) {
                staging->Unmap(0);
                fail("cannot decode DXGI format " + std::to_string(static_cast<int>(desc.Format)));
                return;
            }
            for (unsigned c = 0; c < 4; ++c) {
                image.set_channel(x, y, c, v[c]);
            }
        }
    }
    staging->Unmap(0);
    out = std::move(image);
}

void Rig::cap_tex(rr::CaptureSet& out, const std::string& label, ID3D10Texture2D* texture, UINT subresource) {
    rr::Image image;
    read_tex(texture, subresource, image);
    if (ok()) {
        out.add(label, std::move(image));
    }
}

void Rig::read_buffer(ID3D10Buffer* buffer, std::vector<std::uint8_t>& out) {
    std::string e;
    if (!D3d10Tools::read_buffer(kit.dev, buffer, out, e)) {
        fail(e);
    }
}

void Rig::cap_buffer(rr::CaptureSet& out, const std::string& label, ID3D10Buffer* buffer, UINT floats_per_texel) {
    std::vector<std::uint8_t> bytes;
    read_buffer(buffer, bytes);
    if (!ok()) {
        return;
    }
    const std::size_t count = bytes.size() / (4 * floats_per_texel);
    const UINT width = static_cast<UINT>(std::min<std::size_t>(count, 256));
    const UINT height = static_cast<UINT>((count + width - 1) / width);
    rr::Image image(width, height, rr::PixelKind::Rgba32Float);
    for (std::size_t i = 0; i < count; ++i) {
        for (UINT c = 0; c < floats_per_texel && c < 4; ++c) {
            float value;
            std::memcpy(&value, bytes.data() + (i * floats_per_texel + c) * 4, 4);
            image.set_channel(static_cast<std::uint32_t>(i % width), static_cast<std::uint32_t>(i / width), c, value);
        }
    }
    out.add(label, std::move(image));
}

void Rig::cap_depth(rr::CaptureSet& out, const std::string& label, Depth& depth_ref) {
    D3D10_TEXTURE2D_DESC desc{};
    depth_ref.texture->GetDesc(&desc);
    desc.Usage = D3D10_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    Com<ID3D10Texture2D> staging;
    check(d->CreateTexture2D(&desc, nullptr, staging.put()), "staging depth");
    if (!ok()) {
        return;
    }
    d->CopyResource(staging.get(), depth_ref.texture.get());
    D3D10_MAPPED_TEXTURE2D mapped{};
    check(staging->Map(0, D3D10_MAP_READ, 0, &mapped), "Map of staging depth");
    if (!ok()) {
        return;
    }
    rr::Image image(desc.Width, desc.Height, rr::PixelKind::R32Float);
    const UINT bytes = texel_bytes(desc.Format);
    for (UINT y = 0; y < desc.Height; ++y) {
        const std::uint8_t* row = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
        for (UINT x = 0; x < desc.Width; ++x) {
            const std::uint8_t* p = row + x * bytes;
            float value = 0.0f;
            std::uint32_t word = 0;
            std::memcpy(&word, p, bytes >= 4 ? 4 : bytes);
            switch (desc.Format) {
                case DXGI_FORMAT_D16_UNORM:
                    value = static_cast<float>(word & 0xffffu) / 65535.0f;
                    break;
                case DXGI_FORMAT_D24_UNORM_S8_UINT:
                    value = static_cast<float>(word & 0xffffffu) / 16777215.0f;
                    break;
                default:
                    std::memcpy(&value, p, 4);
                    break;
            }
            image.set_channel(x, y, 0, value);
        }
    }
    staging->Unmap(0);
    out.add(label, std::move(image));
}

void Rig::cap_stencil(rr::CaptureSet& out, const std::string& label, Depth& depth_ref) {
    D3D10_TEXTURE2D_DESC desc{};
    depth_ref.texture->GetDesc(&desc);
    desc.Usage = D3D10_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    Com<ID3D10Texture2D> staging;
    check(d->CreateTexture2D(&desc, nullptr, staging.put()), "staging stencil");
    if (!ok()) {
        return;
    }
    d->CopyResource(staging.get(), depth_ref.texture.get());
    D3D10_MAPPED_TEXTURE2D mapped{};
    check(staging->Map(0, D3D10_MAP_READ, 0, &mapped), "Map of staging stencil");
    if (!ok()) {
        return;
    }
    const bool wide = desc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    rr::Image image(desc.Width, desc.Height, rr::PixelKind::R8Uint);
    for (UINT y = 0; y < desc.Height; ++y) {
        const std::uint8_t* row = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
        for (UINT x = 0; x < desc.Width; ++x) {
            image.row(y)[x] = wide ? row[x * 8 + 4] : row[x * 4 + 3];
        }
    }
    staging->Unmap(0);
    out.add(label, std::move(image));
}

UINT Rig::format_support(DXGI_FORMAT format) {
    UINT support = 0;
    d->CheckFormatSupport(format, &support);
    return support;
}

UINT Rig::msaa_quality(DXGI_FORMAT format, UINT samples) {
    UINT quality = 0;
    if (FAILED(d->CheckMultisampleQualityLevels(format, samples, &quality))) {
        return 0;
    }
    return quality;
}

bool Rig::decode_texel(DXGI_FORMAT format, const std::uint8_t* s, float out[4]) {
    auto u16 = [&](unsigned i) {
        std::uint16_t v;
        std::memcpy(&v, s + i * 2, 2);
        return v;
    };
    auto u32 = [&](unsigned i) {
        std::uint32_t v;
        std::memcpy(&v, s + i * 4, 4);
        return v;
    };
    auto f32 = [&](unsigned i) {
        float v;
        std::memcpy(&v, s + i * 4, 4);
        return v;
    };
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            for (int i = 0; i < 4; ++i) out[i] = s[i] / 255.0f;
            return true;
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            out[0] = s[2] / 255.0f;
            out[1] = s[1] / 255.0f;
            out[2] = s[0] / 255.0f;
            out[3] = s[3] / 255.0f;
            return true;
        case DXGI_FORMAT_R8G8B8A8_SNORM:
            for (int i = 0; i < 4; ++i) out[i] = snorm(static_cast<std::int8_t>(s[i]), 127);
            return true;
        case DXGI_FORMAT_R8G8B8A8_UINT:
            for (int i = 0; i < 4; ++i) out[i] = s[i];
            return true;
        case DXGI_FORMAT_R8G8B8A8_SINT:
            for (int i = 0; i < 4; ++i) out[i] = static_cast<std::int8_t>(s[i]);
            return true;
        case DXGI_FORMAT_R8_UNORM:
            out[0] = s[0] / 255.0f;
            return true;
        case DXGI_FORMAT_R8_UINT:
            out[0] = s[0];
            return true;
        case DXGI_FORMAT_R8_SNORM:
            out[0] = snorm(static_cast<std::int8_t>(s[0]), 127);
            return true;
        case DXGI_FORMAT_R8_SINT:
            out[0] = static_cast<std::int8_t>(s[0]);
            return true;
        case DXGI_FORMAT_A8_UNORM:
            out[3] = s[0] / 255.0f;
            return true;
        case DXGI_FORMAT_R8G8_UNORM:
            out[0] = s[0] / 255.0f;
            out[1] = s[1] / 255.0f;
            return true;
        case DXGI_FORMAT_R8G8_UINT:
            out[0] = s[0];
            out[1] = s[1];
            return true;
        case DXGI_FORMAT_R8G8_SNORM:
            out[0] = snorm(static_cast<std::int8_t>(s[0]), 127);
            out[1] = snorm(static_cast<std::int8_t>(s[1]), 127);
            return true;
        case DXGI_FORMAT_R16_UNORM:
            out[0] = u16(0) / 65535.0f;
            return true;
        case DXGI_FORMAT_R16_FLOAT:
            out[0] = rr::PixelInfo::half_to_float(u16(0));
            return true;
        case DXGI_FORMAT_R16_UINT:
            out[0] = u16(0);
            return true;
        case DXGI_FORMAT_R16_SNORM:
            out[0] = snorm(static_cast<std::int16_t>(u16(0)), 32767);
            return true;
        case DXGI_FORMAT_R16_SINT:
            out[0] = static_cast<std::int16_t>(u16(0));
            return true;
        case DXGI_FORMAT_R16G16_UNORM:
            out[0] = u16(0) / 65535.0f;
            out[1] = u16(1) / 65535.0f;
            return true;
        case DXGI_FORMAT_R16G16_FLOAT:
            out[0] = rr::PixelInfo::half_to_float(u16(0));
            out[1] = rr::PixelInfo::half_to_float(u16(1));
            return true;
        case DXGI_FORMAT_R16G16_UINT:
            out[0] = u16(0);
            out[1] = u16(1);
            return true;
        case DXGI_FORMAT_R16G16_SNORM:
            out[0] = snorm(static_cast<std::int16_t>(u16(0)), 32767);
            out[1] = snorm(static_cast<std::int16_t>(u16(1)), 32767);
            return true;
        case DXGI_FORMAT_R16G16B16A16_UNORM:
            for (unsigned i = 0; i < 4; ++i) out[i] = u16(i) / 65535.0f;
            return true;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            for (unsigned i = 0; i < 4; ++i) out[i] = rr::PixelInfo::half_to_float(u16(i));
            return true;
        case DXGI_FORMAT_R16G16B16A16_UINT:
            for (unsigned i = 0; i < 4; ++i) out[i] = u16(i);
            return true;
        case DXGI_FORMAT_R16G16B16A16_SNORM:
            for (unsigned i = 0; i < 4; ++i) out[i] = snorm(static_cast<std::int16_t>(u16(i)), 32767);
            return true;
        case DXGI_FORMAT_R16G16B16A16_SINT:
            for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<std::int16_t>(u16(i));
            return true;
        case DXGI_FORMAT_R32_FLOAT:
            out[0] = f32(0);
            return true;
        case DXGI_FORMAT_R32_UINT:
            out[0] = static_cast<float>(u32(0));
            return true;
        case DXGI_FORMAT_R32_SINT:
            out[0] = static_cast<float>(static_cast<std::int32_t>(u32(0)));
            return true;
        case DXGI_FORMAT_R32G32_FLOAT:
            out[0] = f32(0);
            out[1] = f32(1);
            return true;
        case DXGI_FORMAT_R32G32_UINT:
            out[0] = static_cast<float>(u32(0));
            out[1] = static_cast<float>(u32(1));
            return true;
        case DXGI_FORMAT_R32G32_SINT:
            out[0] = static_cast<float>(static_cast<std::int32_t>(u32(0)));
            out[1] = static_cast<float>(static_cast<std::int32_t>(u32(1)));
            return true;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            for (unsigned i = 0; i < 4; ++i) out[i] = f32(i);
            return true;
        case DXGI_FORMAT_R32G32B32A32_UINT:
            for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<float>(u32(i));
            return true;
        case DXGI_FORMAT_R32G32B32A32_SINT:
            for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<float>(static_cast<std::int32_t>(u32(i)));
            return true;
        case DXGI_FORMAT_R10G10B10A2_UNORM: {
            const std::uint32_t v = u32(0);
            out[0] = (v & 1023u) / 1023.0f;
            out[1] = ((v >> 10) & 1023u) / 1023.0f;
            out[2] = ((v >> 20) & 1023u) / 1023.0f;
            out[3] = (v >> 30) / 3.0f;
            return true;
        }
        case DXGI_FORMAT_R10G10B10A2_UINT: {
            const std::uint32_t v = u32(0);
            out[0] = static_cast<float>(v & 1023u);
            out[1] = static_cast<float>((v >> 10) & 1023u);
            out[2] = static_cast<float>((v >> 20) & 1023u);
            out[3] = static_cast<float>(v >> 30);
            return true;
        }
        case DXGI_FORMAT_R11G11B10_FLOAT: {
            const std::uint32_t v = u32(0);
            out[0] = unpack_small_float(v & 2047u, 6);
            out[1] = unpack_small_float((v >> 11) & 2047u, 6);
            out[2] = unpack_small_float((v >> 22) & 1023u, 5);
            return true;
        }
        case DXGI_FORMAT_B5G6R5_UNORM: {
            const std::uint16_t v = u16(0);
            out[0] = ((v >> 11) & 31u) / 31.0f;
            out[1] = ((v >> 5) & 63u) / 63.0f;
            out[2] = (v & 31u) / 31.0f;
            return true;
        }
        case DXGI_FORMAT_B5G5R5A1_UNORM: {
            const std::uint16_t v = u16(0);
            out[0] = ((v >> 10) & 31u) / 31.0f;
            out[1] = ((v >> 5) & 31u) / 31.0f;
            out[2] = (v & 31u) / 31.0f;
            out[3] = static_cast<float>(v >> 15);
            return true;
        }
        default:
            return false;
    }
}

std::vector<Vtx> Geo::quad(float x0, float y0, float x1, float y1, float z, float r, float g, float b, float a) {
    return {{x0, y1, z, r, g, b, a, 0, 0}, {x1, y1, z, r, g, b, a, 1, 0}, {x1, y0, z, r, g, b, a, 1, 1},
            {x0, y1, z, r, g, b, a, 0, 0}, {x1, y0, z, r, g, b, a, 1, 1}, {x0, y0, z, r, g, b, a, 0, 1}};
}

std::vector<Vtx> Geo::quad_uv(float x0, float y0, float x1, float y1, float z, float u0, float v0, float u1, float v1) {
    return {{x0, y1, z, 1, 1, 1, 1, u0, v0}, {x1, y1, z, 1, 1, 1, 1, u1, v0}, {x1, y0, z, 1, 1, 1, 1, u1, v1},
            {x0, y1, z, 1, 1, 1, 1, u0, v0}, {x1, y0, z, 1, 1, 1, 1, u1, v1}, {x0, y0, z, 1, 1, 1, 1, u0, v1}};
}

std::vector<Vtx> Geo::triangle(float cx, float cy, float radius, float z, float r, float g, float b, float a) {
    return {{cx, cy + radius, z, r, g, b, a, 0.5f, 0}, {cx + 0.866f * radius, cy - 0.5f * radius, z, r, g, b, a, 1, 1},
            {cx - 0.866f * radius, cy - 0.5f * radius, z, r, g, b, a, 0, 1}};
}

MeshData Geo::sphere(unsigned rings, unsigned segments, float radius) {
    MeshData mesh;
    for (unsigned i = 0; i <= rings; ++i) {
        const float theta = 3.14159265f * static_cast<float>(i) / static_cast<float>(rings);
        for (unsigned j = 0; j <= segments; ++j) {
            const float phi = 6.2831853f * static_cast<float>(j) / static_cast<float>(segments);
            const float nx = std::sin(theta) * std::cos(phi);
            const float ny = std::cos(theta);
            const float nz = std::sin(theta) * std::sin(phi);
            mesh.vertices.push_back({{nx * radius, ny * radius, nz * radius}, {nx, ny, nz}, {static_cast<float>(j) / segments, static_cast<float>(i) / rings}});
        }
    }
    for (unsigned i = 0; i < rings; ++i) {
        for (unsigned j = 0; j < segments; ++j) {
            const std::uint32_t a = i * (segments + 1) + j;
            const std::uint32_t b = a + segments + 1;
            mesh.indices.insert(mesh.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    }
    return mesh;
}

MeshData Geo::torus(unsigned rings, unsigned segments, float major, float minor) {
    MeshData mesh;
    for (unsigned i = 0; i <= rings; ++i) {
        const float u = 6.2831853f * static_cast<float>(i) / static_cast<float>(rings);
        for (unsigned j = 0; j <= segments; ++j) {
            const float v = 6.2831853f * static_cast<float>(j) / static_cast<float>(segments);
            const float nx = std::cos(v) * std::cos(u);
            const float ny = std::sin(v);
            const float nz = std::cos(v) * std::sin(u);
            mesh.vertices.push_back({{(major + minor * std::cos(v)) * std::cos(u), minor * ny, (major + minor * std::cos(v)) * std::sin(u)},
                                     {nx, ny, nz},
                                     {static_cast<float>(i) / rings * 4.0f, static_cast<float>(j) / segments}});
        }
    }
    for (unsigned i = 0; i < rings; ++i) {
        for (unsigned j = 0; j < segments; ++j) {
            const std::uint32_t a = i * (segments + 1) + j;
            const std::uint32_t b = a + segments + 1;
            mesh.indices.insert(mesh.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    }
    return mesh;
}

std::vector<float> Geo::height_field(unsigned cells, std::uint32_t seed) {
    std::vector<float> out((cells + 1) * (cells + 1));
    std::uint32_t state = seed;
    const float p0 = rr::Pattern::unit_random(state) * 6.0f;
    const float p1 = rr::Pattern::unit_random(state) * 6.0f;
    for (unsigned z = 0; z <= cells; ++z) {
        for (unsigned x = 0; x <= cells; ++x) {
            const float fx = static_cast<float>(x) / cells;
            const float fz = static_cast<float>(z) / cells;
            out[z * (cells + 1) + x] = 0.35f * std::sin(fx * 7.0f + p0) * std::cos(fz * 5.0f + p1) + 0.15f * std::sin(fx * 17.0f + fz * 13.0f);
        }
    }
    return out;
}

MeshData Geo::grid(unsigned cells, float extent, const std::vector<float>& heights) {
    MeshData mesh;
    const unsigned n = cells + 1;
    for (unsigned z = 0; z < n; ++z) {
        for (unsigned x = 0; x < n; ++x) {
            const float h = heights[z * n + x];
            const float hx = heights[z * n + std::min(x + 1, cells)] - heights[z * n + (x > 0 ? x - 1 : 0)];
            const float hz = heights[std::min(z + 1, cells) * n + x] - heights[(z > 0 ? z - 1 : 0) * n + x];
            const float step = 2.0f * extent / cells * 2.0f;
            float nx = -hx, ny = step, nz = -hz;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            mesh.vertices.push_back({{(static_cast<float>(x) / cells * 2.0f - 1.0f) * extent, h, (static_cast<float>(z) / cells * 2.0f - 1.0f) * extent},
                                     {nx / len, ny / len, nz / len},
                                     {static_cast<float>(x) / cells, static_cast<float>(z) / cells}});
        }
    }
    for (unsigned z = 0; z < cells; ++z) {
        for (unsigned x = 0; x < cells; ++x) {
            const std::uint32_t a = z * n + x;
            mesh.indices.insert(mesh.indices.end(), {a, a + n, a + 1, a + 1, a + n, a + n + 1});
        }
    }
    return mesh;
}

MeshData Geo::box(float hx, float hy, float hz) {
    MeshData mesh;
    const float normals[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
        const float* n = normals[f];
        float t[3], b[3];
        if (f < 2) {
            t[0] = 0, t[1] = 1, t[2] = 0;
            b[0] = 0, b[1] = 0, b[2] = 1;
        } else if (f < 4) {
            t[0] = 1, t[1] = 0, t[2] = 0;
            b[0] = 0, b[1] = 0, b[2] = 1;
        } else {
            t[0] = 1, t[1] = 0, t[2] = 0;
            b[0] = 0, b[1] = 1, b[2] = 0;
        }
        const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int k = 0; k < 4; ++k) {
            const float s = (k & 1) ? 1.0f : -1.0f;
            const float q = (k & 2) ? 1.0f : -1.0f;
            MeshVertex v{};
            const float ext[3] = {hx, hy, hz};
            for (int c = 0; c < 3; ++c) {
                v.p[c] = (n[c] * 1.0f + t[c] * s + b[c] * q) * ext[c];
                v.n[c] = n[c];
            }
            v.uv[0] = (s + 1.0f) * 0.5f;
            v.uv[1] = (q + 1.0f) * 0.5f;
            mesh.vertices.push_back(v);
        }
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
    }
    return mesh;
}

}  // namespace lsw::suites::render::d3d10
