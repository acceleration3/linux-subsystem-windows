#pragma once

#include "render/common/mat4.hpp"
#include "render/common/mesh.hpp"
#include "render/common/pattern.hpp"
#include "render/d3d11/d3d11.hpp"
#include "support/bc_codec.hpp"
#include "support/format_codec.hpp"

#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lsw::suites::render::d3d11 {
using Cap = rr::CaptureSet;
using Bytes = std::vector<std::uint8_t>;

struct V2 {
    float x, y, u, v;
};

struct V3C {
    float x, y, z, r, g, b;
};

struct Fs {
    static constexpr const char* kVs =
        "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
        "VO main(uint id : SV_VertexID) { VO o; float2 t = float2((id << 1) & 2, id & 2);\n"
        "  o.p = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = t; return o; }\n";
    static constexpr const char* kIn = "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n";
};

D3D11_RASTERIZER_DESC rdesc(D3D11_FILL_MODE fill = D3D11_FILL_SOLID, D3D11_CULL_MODE cull = D3D11_CULL_NONE);
D3D11_BLEND_DESC bdesc();
D3D11_DEPTH_STENCIL_DESC ddesc(bool enable = false, D3D11_COMPARISON_FUNC func = D3D11_COMPARISON_LESS, bool write = true);
D3D11_SAMPLER_DESC sdesc(D3D11_FILTER filter = D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_MODE address = D3D11_TEXTURE_ADDRESS_CLAMP);
D3D11_INPUT_ELEMENT_DESC elem(const char* name, DXGI_FORMAT format, UINT offset, UINT slot = 0, UINT index = 0, bool instance = false,
                              UINT step = 0);

class Rig {
public:
    explicit Rig(Kit& k) : kit(k), dev(k.device()), ctx(k.context()) {}

    bool fail(const std::string& text);
    [[nodiscard]] bool broken(const char* name) const;
    bool ok(HRESULT result, const char* what);
    [[nodiscard]] bool fl11() const { return kit.dev.level >= D3D_FEATURE_LEVEL_11_0; }
    [[nodiscard]] bool fl11_1() const { return kit.dev.level >= D3D_FEATURE_LEVEL_11_1; }

    Com<ID3D11VertexShader> vs(const std::string& src, Bytes* code = nullptr, const char* profile = nullptr, const char* entry = "main");
    Com<ID3D11PixelShader> ps(const std::string& src, const char* profile = nullptr, const char* entry = "main");
    Com<ID3D11GeometryShader> gs(const std::string& src, const char* profile = nullptr, const char* entry = "main");
    Com<ID3D11GeometryShader> gs_so(const std::string& src, const std::vector<D3D11_SO_DECLARATION_ENTRY>& decl, const std::vector<UINT>& strides,
                                    UINT rasterized, const char* entry = "main");
    Com<ID3D11GeometryShader> vs_so(const std::string& src, const std::vector<D3D11_SO_DECLARATION_ENTRY>& decl, const std::vector<UINT>& strides,
                                    UINT rasterized, Bytes* code = nullptr);
    Com<ID3D11HullShader> hs(const std::string& src, const char* entry = "main");
    Com<ID3D11DomainShader> ds(const std::string& src, const char* entry = "main");
    Com<ID3D11ComputeShader> cs(const std::string& src, const char* profile = nullptr, const char* entry = "main");
    Com<ID3D11InputLayout> layout(const std::vector<D3D11_INPUT_ELEMENT_DESC>& elements, const Bytes& code);

    Com<ID3D11Buffer> buf(UINT size, UINT bind, const void* data = nullptr, D3D11_USAGE usage = D3D11_USAGE_DEFAULT, UINT cpu = 0, UINT misc = 0,
                          UINT stride = 0);
    template <class T>
    Com<ID3D11Buffer> vbuf(const std::vector<T>& data) {
        return buf(static_cast<UINT>(data.size() * sizeof(T)), D3D11_BIND_VERTEX_BUFFER, data.data());
    }
    template <class T>
    Com<ID3D11Buffer> ibuf(const std::vector<T>& data) {
        return buf(static_cast<UINT>(data.size() * sizeof(T)), D3D11_BIND_INDEX_BUFFER, data.data());
    }
    Com<ID3D11Buffer> cbuf(const void* data, UINT size);
    template <class T>
    Com<ID3D11Buffer> cb(const T& value) {
        return cbuf(&value, sizeof(T));
    }
    template <class T>
    void update(ID3D11Buffer* buffer, const T& value) {
        ctx->UpdateSubresource(buffer, 0, nullptr, &value, 0, 0);
    }

    Com<ID3D11Texture2D> tex(UINT w, UINT h, DXGI_FORMAT format, UINT bind, UINT mips = 1, UINT array = 1, UINT samples = 1,
                             const D3D11_SUBRESOURCE_DATA* init = nullptr, UINT misc = 0);
    Com<ID3D11Texture2D> tex_data(UINT w, UINT h, DXGI_FORMAT format, const Bytes& pixels, UINT bind = D3D11_BIND_SHADER_RESOURCE);
    Com<ID3D11Texture2D> tex_chain(UINT w, UINT h, const Bytes& base, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM, UINT array = 1,
                                   UINT misc = 0);
    Com<ID3D11ShaderResourceView> srv(ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC* desc = nullptr);
    Com<ID3D11RenderTargetView> rtv(ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC* desc = nullptr);
    Com<ID3D11UnorderedAccessView> uav(ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc = nullptr);
    Com<ID3D11DepthStencilView> dsv(ID3D11Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc = nullptr);
    Com<ID3D11SamplerState> smp(const D3D11_SAMPLER_DESC& desc);
    Com<ID3D11BlendState> blend(const D3D11_BLEND_DESC& desc);
    Com<ID3D11RasterizerState> rast(const D3D11_RASTERIZER_DESC& desc);
    Com<ID3D11DepthStencilState> dss(const D3D11_DEPTH_STENCIL_DESC& desc);
    Com<ID3D11ShaderResourceView> srv_buffer(ID3D11Buffer* buffer, DXGI_FORMAT format, UINT first, UINT count);

    bool color(Target& out, UINT w = 128, UINT h = 128, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM, UINT samples = 1);
    bool depth(Depth& out, UINT w = 128, UINT h = 128, DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT, UINT samples = 1, UINT quality = 0);

    void bind(Target& target, Depth* depth = nullptr, const float* clear = nullptr, bool clear_depth = true);
    void viewport(float x, float y, float w, float h, float min_depth = 0.0f, float max_depth = 1.0f);
    void draw_fs(ID3D11PixelShader* pixel);
    void unbind_srvs();
    void reset();

    bool shot(Cap& out, Target& target, const char* label = "");
    bool read_image(ID3D11Texture2D* texture, DXGI_FORMAT format, unsigned subresource, rr::Image& out);
    std::string debug_messages();
    bool depth_image(Depth& depth, rr::Image& out);
    bool read_bytes(ID3D11Buffer* buffer, Bytes& out);
    bool read_texels(ID3D11Texture2D* texture, unsigned subresource, TexelImage& out);

    Kit& kit;
    ID3D11Device* dev;
    ID3D11DeviceContext* ctx;
    std::string error;

private:
    Com<ID3D11VertexShader> fs_vs_;
    Com<ID3D11RasterizerState> default_rs_;
};

using Body = std::function<bool(Rig&, Cap&)>;
using Needs = std::function<std::string(Rig&)>;

class FnScene : public D3d11SingleScene {
public:
    FnScene(Body body, Needs needs) : body_(std::move(body)), needs_(std::move(needs)) {}
    [[nodiscard]] std::string unsupported(Kit& kit) override;
    bool draw(Kit& kit, rr::CaptureSet& out, std::string& error) override;

private:
    Body body_;
    Needs needs_;
};

class Add {
public:
    Add(rr::CaseSpec spec, Body body, Needs needs = {});
};

template <class State>
class StateScene : public D3d11Scene {
public:
    explicit StateScene(State state) : state_(std::move(state)) {}
    [[nodiscard]] std::string unsupported(Kit& kit) override {
        Rig rig(kit);
        return state_.needs(rig);
    }
    bool setup(Kit& kit, std::string& error) override {
        Rig rig(kit);
        if (!state_.init(rig)) {
            error = rig.error;
            return false;
        }
        return true;
    }
    bool frame(Kit& kit, unsigned index, std::string& error) override {
        Rig rig(kit);
        if (!state_.step(rig, index)) {
            error = rig.error;
            return false;
        }
        return debug_ok(rig, error);
    }
    bool capture(Kit& kit, unsigned index, rr::CaptureSet& out, std::string& error) override {
        Rig rig(kit);
        if (!state_.shot(rig, index, out)) {
            error = rig.error;
            return false;
        }
        return debug_ok(rig, error);
    }

private:
    static bool debug_ok(Rig& rig, std::string& error) {
        if (!rig.kit.debug) {
            return true;
        }
        const std::string messages = rig.debug_messages();
        if (messages.empty()) {
            return true;
        }
        error = "debug layer: " + messages;
        return false;
    }

    State state_;
};

template <class State>
class AddState {
public:
    explicit AddState(rr::CaseSpec spec, std::function<State()> make = [] { return State(); }) {
        spec.source_hash(LSW_RENDER_SOURCE_HASH);
        rr::CaseTable<Kit>::instance().add(std::move(spec), [make = std::move(make)] { return std::unique_ptr<rr::Scene<Kit>>(new StateScene<State>(make())); });
    }
};

rr::CaseSpec spec(const char* group, const char* name);

std::string need_format(Rig& rig, DXGI_FORMAT format, UINT bits, const char* what);
std::string need_msaa(Rig& rig, DXGI_FORMAT format, UINT samples);
std::string need_fl(Rig& rig, D3D_FEATURE_LEVEL level);

}  // namespace lsw::suites::render::d3d11
