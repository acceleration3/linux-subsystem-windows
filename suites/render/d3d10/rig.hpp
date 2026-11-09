#pragma once

#include "render/common/mat4.hpp"
#include "render/common/mesh.hpp"
#include "render/common/pattern.hpp"
#include "render/d3d/convert.hpp"
#include "render/d3d10/d3d10.hpp"

#include <d3d10effect.h>

#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lsw::suites::render::d3d10 {
struct Vtx {
    float x, y, z;
    float r, g, b, a;
    float u, v;
};

struct MeshVertex {
    float p[3];
    float n[3];
    float uv[2];
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

class Geo {
public:
    [[nodiscard]] static std::vector<Vtx> quad(float x0, float y0, float x1, float y1, float z, float r, float g, float b, float a = 1.0f);
    [[nodiscard]] static std::vector<Vtx> quad_uv(float x0, float y0, float x1, float y1, float z, float u0, float v0, float u1, float v1);
    [[nodiscard]] static std::vector<Vtx> triangle(float cx, float cy, float radius, float z, float r, float g, float b, float a = 1.0f);
    [[nodiscard]] static std::vector<Vtx> stripe_scene(unsigned count);
    [[nodiscard]] static MeshData sphere(unsigned rings, unsigned segments, float radius);
    [[nodiscard]] static MeshData torus(unsigned rings, unsigned segments, float major, float minor);
    [[nodiscard]] static MeshData grid(unsigned cells, float extent, const std::vector<float>& heights);
    [[nodiscard]] static MeshData box(float hx, float hy, float hz);
    [[nodiscard]] static std::vector<float> height_field(unsigned cells, std::uint32_t seed);
};

struct Std {
    static constexpr const char* kVs =
        "struct VI { float3 p : POSITION; float4 c : COLOR; float2 uv : TEXCOORD0; };\n"
        "struct VO { float4 p : SV_Position; float4 c : COLOR; float2 uv : TEXCOORD0; };\n"
        "VO main(VI i) { VO o; o.p = float4(i.p, 1.0); o.c = i.c; o.uv = i.uv; return o; }\n";
    static constexpr const char* kPs =
        "float4 main(float4 p : SV_Position, float4 c : COLOR) : SV_Target { return c; }\n";
    static constexpr const char* kPsUv =
        "float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target { return c + uv.x * 0.00001; }\n";
    static constexpr const char* kFullVs =
        "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
        "VO main(uint id : SV_VertexID) { VO o; float2 t = float2((id << 1) & 2, id & 2);\n"
        "  o.p = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = t; return o; }\n";
};

class Rig {
public:
    explicit Rig(Kit& kit);
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    ~Rig();

    Kit& kit;
    ID3D10Device* d;
    std::string error;
    const char* sm;
    Target main;
    Depth depth_main;
    std::vector<std::uint8_t> vs_bytes;
    Com<ID3D10RasterizerState> default_raster;

    [[nodiscard]] bool ok() const { return error.empty(); }
    void fail(const std::string& text);
    void check(HRESULT result, const char* what);
    [[nodiscard]] bool level_1() const { return kit.level_10_1; }

    [[nodiscard]] Com<ID3D10VertexShader> vs(const std::string& source);
    [[nodiscard]] Com<ID3D10PixelShader> ps(const std::string& source);
    [[nodiscard]] Com<ID3D10GeometryShader> gs(const std::string& source);
    [[nodiscard]] Com<ID3D10GeometryShader> gs_so(const std::string& source, const std::vector<D3D10_SO_DECLARATION_ENTRY>& decl, UINT stride);
    [[nodiscard]] Com<ID3D10InputLayout> layout(const std::vector<D3D10_INPUT_ELEMENT_DESC>& elements);
    [[nodiscard]] Com<ID3D10InputLayout> std_layout();

    [[nodiscard]] Com<ID3D10Buffer> buf(UINT size, D3D10_USAGE usage, UINT bind, UINT cpu, const void* data);
    [[nodiscard]] Com<ID3D10Buffer> vbuf(const void* data, UINT size, UINT extra_bind = 0);
    [[nodiscard]] Com<ID3D10Buffer> vbuf(const std::vector<Vtx>& v) { return vbuf(v.data(), static_cast<UINT>(v.size() * sizeof(Vtx))); }
    [[nodiscard]] Com<ID3D10Buffer> ibuf16(const std::vector<std::uint16_t>& v);
    [[nodiscard]] Com<ID3D10Buffer> ibuf32(const std::vector<std::uint32_t>& v);
    [[nodiscard]] Com<ID3D10Buffer> cbuf(UINT size, const void* data = nullptr);

    template <class T>
    [[nodiscard]] Com<ID3D10Buffer> cb(const T& value) {
        const UINT size = (static_cast<UINT>(sizeof(T)) + 15u) & ~15u;
        std::vector<std::uint8_t> padded(size, 0);
        std::memcpy(padded.data(), &value, sizeof(T));
        return cbuf(size, padded.data());
    }
    template <class T>
    void set(ID3D10Buffer* buffer, const T& value) {
        const UINT size = (static_cast<UINT>(sizeof(T)) + 15u) & ~15u;
        std::vector<std::uint8_t> padded(size, 0);
        std::memcpy(padded.data(), &value, sizeof(T));
        d->UpdateSubresource(buffer, 0, nullptr, padded.data(), 0, 0);
    }

    [[nodiscard]] static D3D10_RASTERIZER_DESC raster_desc();
    [[nodiscard]] static D3D10_DEPTH_STENCIL_DESC depth_desc();
    [[nodiscard]] static D3D10_BLEND_DESC blend_desc();
    [[nodiscard]] static D3D10_SAMPLER_DESC sampler_desc(D3D10_FILTER filter = D3D10_FILTER_MIN_MAG_MIP_POINT,
                                                          D3D10_TEXTURE_ADDRESS_MODE mode = D3D10_TEXTURE_ADDRESS_CLAMP);
    [[nodiscard]] Com<ID3D10RasterizerState> raster(const D3D10_RASTERIZER_DESC& desc);
    [[nodiscard]] Com<ID3D10DepthStencilState> depth(const D3D10_DEPTH_STENCIL_DESC& desc);
    [[nodiscard]] Com<ID3D10BlendState> blend(const D3D10_BLEND_DESC& desc);
    [[nodiscard]] Com<ID3D10BlendState1> blend1(const D3D10_BLEND_DESC1& desc);
    [[nodiscard]] Com<ID3D10SamplerState> sampler(const D3D10_SAMPLER_DESC& desc);

    [[nodiscard]] Com<ID3D10Texture2D> tex2d(UINT width, UINT height, DXGI_FORMAT format, UINT bind, UINT mips = 1, UINT array_size = 1,
                                             const std::vector<const void*>& data = {}, const std::vector<UINT>& pitches = {}, UINT misc = 0);
    [[nodiscard]] Com<ID3D10Texture2D> rgba_tex(UINT width, UINT height, const rr::Pattern::Bytes& bytes, bool mips = false, bool srgb = false);
    [[nodiscard]] Com<ID3D10ShaderResourceView> srv(ID3D10Resource* resource, const D3D10_SHADER_RESOURCE_VIEW_DESC* desc = nullptr);
    [[nodiscard]] Com<ID3D10RenderTargetView> rtv(ID3D10Resource* resource, const D3D10_RENDER_TARGET_VIEW_DESC* desc = nullptr);
    [[nodiscard]] Com<ID3D10DepthStencilView> dsv(ID3D10Resource* resource, const D3D10_DEPTH_STENCIL_VIEW_DESC* desc = nullptr);

    void target(Target& out, UINT width, UINT height, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM, UINT samples = 1);
    void depth_target(Depth& out, UINT width, UINT height, DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT, UINT samples = 1);
    void begin(UINT width = 128, UINT height = 128, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM, bool with_depth = false,
               const float* clear = nullptr);

    [[nodiscard]] Com<ID3D10Effect> effect(const std::string& source, const char* profile = "fx_4_0", ID3D10EffectPool* pool = nullptr);
    [[nodiscard]] Com<ID3D10EffectPool> effect_pool(const std::string& source, const char* profile = "fx_4_0");
    void no_cull();
    void bind_rt(Target& target, Depth* depth = nullptr);
    void clear(Target& target, float r, float g, float b, float a);
    void clear_depth(Depth& depth, float value = 1.0f, UINT8 stencil = 0, UINT flags = D3D10_CLEAR_DEPTH | D3D10_CLEAR_STENCIL);
    void viewport(UINT width, UINT height, float x = 0, float y = 0, float min_depth = 0.0f, float max_depth = 1.0f);

    void use_std(D3D10_PRIMITIVE_TOPOLOGY topology = D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST, bool with_uv = true);
    void std_vb(ID3D10Buffer* buffer, UINT slot = 0, UINT offset = 0);
    void draw_std(const std::vector<Vtx>& vertices, D3D10_PRIMITIVE_TOPOLOGY topology = D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    void fullscreen(ID3D10PixelShader* pixel);
    void fullscreen(const std::string& pixel_source);

    void ps_srv(UINT slot, ID3D10ShaderResourceView* view);
    void ps_smp(UINT slot, ID3D10SamplerState* state);
    void ps_cb(UINT slot, ID3D10Buffer* buffer);
    void vs_cb(UINT slot, ID3D10Buffer* buffer);
    void gs_cb(UINT slot, ID3D10Buffer* buffer);
    void unbind_all();

    void read(Target& target, rr::Image& out);
    void cap(rr::CaptureSet& out, const std::string& label, Target& target);
    void cap_main(rr::CaptureSet& out, const std::string& label = "");
    void read_tex(ID3D10Texture2D* texture, UINT subresource, rr::Image& out);
    void cap_tex(rr::CaptureSet& out, const std::string& label, ID3D10Texture2D* texture, UINT subresource = 0);
    void read_buffer(ID3D10Buffer* buffer, std::vector<std::uint8_t>& out);
    void cap_buffer(rr::CaptureSet& out, const std::string& label, ID3D10Buffer* buffer, UINT floats_per_texel = 4);
    void cap_depth(rr::CaptureSet& out, const std::string& label, Depth& depth);
    void cap_stencil(rr::CaptureSet& out, const std::string& label, Depth& depth);

    [[nodiscard]] UINT format_support(DXGI_FORMAT format);
    [[nodiscard]] UINT msaa_quality(DXGI_FORMAT format, UINT samples);
    [[nodiscard]] static bool decode_texel(DXGI_FORMAT format, const std::uint8_t* src, float out[4]);

private:
    template <class T>
    Com<T> hold(Com<T> object) {
        if (object) {
            object->AddRef();
            keep_.push_back(object.get());
        }
        return object;
    }
    std::vector<IUnknown*> keep_;
};

using Body = std::function<void(Rig&, rr::CaptureSet&)>;
using Caps = std::function<std::string(Rig&)>;

void single(rr::CaseSpec spec, Body body, Caps caps = nullptr);

template <class State>
struct MultiBody {
    std::function<void(Rig&, State&)> setup;
    std::function<void(Rig&, State&, unsigned)> frame;
    std::function<void(Rig&, State&, unsigned, rr::CaptureSet&)> capture;
};

template <class State>
class MultiScene : public D3d10Scene {
public:
    explicit MultiScene(const MultiBody<State>* body, Caps caps) : body_(body), caps_(std::move(caps)) {}

    std::string unsupported(Kit& kit) override {
        if (!caps_) {
            return {};
        }
        Rig rig(kit);
        return caps_(rig);
    }

    bool setup(Kit& kit, std::string& error) override {
        rig_ = std::make_unique<Rig>(kit);
        body_->setup(*rig_, state_);
        error = rig_->error;
        return rig_->ok();
    }

    bool frame(Kit&, unsigned index, std::string& error) override {
        body_->frame(*rig_, state_, index);
        error = rig_->error;
        return rig_->ok();
    }

    bool capture(Kit&, unsigned index, rr::CaptureSet& out, std::string& error) override {
        if (body_->capture) {
            body_->capture(*rig_, state_, index, out);
        } else {
            rig_->cap_main(out);
        }
        error = rig_->error;
        return rig_->ok();
    }

private:
    const MultiBody<State>* body_;
    Caps caps_;
    std::unique_ptr<Rig> rig_;
    State state_;
};

template <class State>
void multi(rr::CaseSpec spec, MultiBody<State> body, Caps caps = nullptr) {
    auto shared = std::make_shared<MultiBody<State>>(std::move(body));
    rr::CaseTable<Kit>::instance().add(std::move(spec), [shared, caps] {
        return std::unique_ptr<rr::Scene<Kit>>(new MultiScene<State>(shared.get(), caps));
    });
}

struct Registrar {
    explicit Registrar(const std::function<void()>& fn) { fn(); }
};

[[nodiscard]] rr::CaseSpec spec(const char* group, const std::string& name, unsigned variant = 0);
}  // namespace lsw::suites::render::d3d10
