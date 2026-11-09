#pragma once

#include "render/common/mat4.hpp"
#include "render/d3d12/d3d12.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lsw::suites::render::d3d12 {

struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

class Shapes {
public:
    [[nodiscard]] static MeshData cube();
    [[nodiscard]] static MeshData sphere(unsigned stacks, unsigned slices, float radius);
    [[nodiscard]] static MeshData torus(unsigned rings, unsigned sides, float major, float minor);
    [[nodiscard]] static MeshData grid(unsigned cells, float extent);
    [[nodiscard]] static MeshData quad(float x0, float y0, float x1, float y1, float z = 0.0f);
    [[nodiscard]] static std::vector<D3D12_INPUT_ELEMENT_DESC> layout();
};

class Geometry {
public:
    [[nodiscard]] bool create(Kit& kit, const MeshData& mesh, bool wide_indices, std::string& error);
    [[nodiscard]] bool create_vertices(Kit& kit, const void* data, UINT count, UINT stride, std::string& error);
    void bind(ID3D12GraphicsCommandList* list, D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST) const;
    void draw(ID3D12GraphicsCommandList* list, UINT instances = 1, UINT first_instance = 0) const;
    void draw_vertices(ID3D12GraphicsCommandList* list, UINT instances = 1) const;

    ID3D12Resource* vertex = nullptr;
    ID3D12Resource* index = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW ibv{};
    UINT index_count = 0;
    UINT vertex_count = 0;
};

struct DepthFormats {
    DXGI_FORMAT resource;
    DXGI_FORMAT dsv;
    DXGI_FORMAT depth_srv;
    DXGI_FORMAT stencil_srv;
    bool has_stencil;
    [[nodiscard]] static DepthFormats of(DXGI_FORMAT dsv);
};

class Surface {
public:
    [[nodiscard]] bool color(Kit& kit, UINT width, UINT height, DXGI_FORMAT format, std::string& error, UINT samples = 1, UINT mips = 1,
                             UINT array = 1, D3D12_RESOURCE_FLAGS extra = D3D12_RESOURCE_FLAG_NONE, DXGI_FORMAT rtv_format = DXGI_FORMAT_UNKNOWN);
    [[nodiscard]] bool depth(Kit& kit, UINT width, UINT height, DXGI_FORMAT dsv_format, std::string& error, UINT samples = 1, UINT array = 1,
                             D3D12_RESOURCE_FLAGS extra = D3D12_RESOURCE_FLAG_NONE);

    void to(ID3D12GraphicsCommandList* list, D3D12_RESOURCE_STATES next);
    void bind_target(ID3D12GraphicsCommandList* list, const Surface* depth_surface = nullptr, bool read_only_depth = false);

    ID3D12Resource* resource = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    ID3D12DescriptorHeap* readonly_heap = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE view{};
    D3D12_CPU_DESCRIPTOR_HANDLE readonly_view{};
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT view_format = DXGI_FORMAT_UNKNOWN;
    UINT width = 0;
    UINT height = 0;
    UINT samples = 1;
    UINT mips = 1;
    UINT array = 1;
    bool is_depth = false;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
};

class Binder {
public:
    static constexpr UINT kSrvSlots = 8;
    static constexpr UINT kUavSlots = 4;
    static constexpr UINT kSamplerSlots = 4;

    [[nodiscard]] bool create(Kit& kit, std::string& error);
    void srv(UINT slot, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc);
    void srv_texture(UINT slot, ID3D12Resource* resource, DXGI_FORMAT format, UINT mips = 1);
    void srv_buffer(UINT slot, ID3D12Resource* resource, DXGI_FORMAT format, UINT first, UINT count, UINT stride = 0, bool raw = false);
    void uav(UINT slot, ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc);
    void uav_texture(UINT slot, ID3D12Resource* resource, DXGI_FORMAT format, UINT mip = 0);
    void uav_buffer(UINT slot, ID3D12Resource* resource, DXGI_FORMAT format, UINT first, UINT count, UINT stride = 0, bool raw = false);
    void sampler(UINT slot, const D3D12_SAMPLER_DESC& desc);
    void bind_graphics(ID3D12GraphicsCommandList* list) const;
    void bind_compute(ID3D12GraphicsCommandList* list) const;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu(UINT slot) const;
    [[nodiscard]] static D3D12_SAMPLER_DESC sampler_desc(D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE mode);

    Kit* owner = nullptr;
    ID3D12RootSignature* signature = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    ID3D12DescriptorHeap* sampler_heap = nullptr;
    UINT increment = 0;
    UINT sampler_increment = 0;
};

class Pso {
public:
    explicit Pso(Kit& kit_ref, ID3D12RootSignature* signature);
    Pso& vs(const std::string& source);
    Pso& ps(const std::string& source);
    Pso& layout(const std::vector<D3D12_INPUT_ELEMENT_DESC>& elements);
    Pso& targets(std::initializer_list<DXGI_FORMAT> formats);
    Pso& depth(DXGI_FORMAT format);
    Pso& samples(UINT count);
    Pso& topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE type);
    Pso& stage(const std::string& source, const char* entry, const char* stage_name);
    [[nodiscard]] ID3D12PipelineState* build(std::string& error);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;
    Kit& kit;
    std::string failure;
    Kit::Bytes vs_code;
    Kit::Bytes ps_code;
    Kit::Bytes hs_code;
    Kit::Bytes ds_code;
    Kit::Bytes gs_code;
    std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
};

class Canvas {
public:
    [[nodiscard]] bool open(Kit& kit, UINT width, UINT height, DXGI_FORMAT format, std::string& error, DXGI_FORMAT depth_format = DXGI_FORMAT_UNKNOWN,
                            UINT samples = 1);
    void begin(ID3D12GraphicsCommandList* list, const float clear[4], bool clear_depth = true, float depth_value = 1.0f);
    void begin_keep(ID3D12GraphicsCommandList* list);
    void set_viewport(ID3D12GraphicsCommandList* list) const;
    [[nodiscard]] bool read(Kit& kit, rr::Image& out, std::string& error);
    [[nodiscard]] bool read_depth(Kit& kit, rr::Image& out, std::string& error);

    Kit* owner = nullptr;
    Surface color;
    Surface depth;
    Binder binder;
    bool has_depth = false;
    UINT width = 0;
    UINT height = 0;
};

template <D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type, class T>
struct alignas(void*) Sub {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
    T value{};
    Sub() = default;
    Sub(const T& v) : value(v) {}
    Sub& operator=(const T& v) {
        value = v;
        return *this;
    }
};

class Stage {
public:
    [[nodiscard]] bool open(Kit& kit, std::string& error, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT depth = DXGI_FORMAT_UNKNOWN,
                            UINT size = 128, UINT samples = 1);
    [[nodiscard]] Pso pso(const char* pixel = nullptr);
    void backdrop(ID3D12GraphicsCommandList* list);
    void mesh(ID3D12GraphicsCommandList* list, const Geometry& geometry, const rr::Mat4& model, float r, float g, float b, float a = 1.0f);
    [[nodiscard]] rr::Mat4 view_projection() const;
    [[nodiscard]] bool finish(rr::CaptureSet& out, std::string& error, const char* label = "");

    Kit* kit = nullptr;
    Canvas canvas;
    Geometry sphere;
    Geometry torus;
    Geometry cube;
    Geometry plane;
    ID3D12PipelineState* backdrop_pso = nullptr;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT depth_format = DXGI_FORMAT_UNKNOWN;
    UINT sample_count = 1;
};

struct Hlsl12 {
    static constexpr const char* kRoot =
        "cbuffer Root : register(b0) { column_major float4x4 mvp; float4 p0; float4 p1; float4 p2; float4 p3; };\n"
        "cbuffer User : register(b1) { float4 u[16]; };\n";
    static constexpr const char* kLitVs =
        "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
        "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
        "VsOut main(VsIn i) { VsOut o; o.pos = mul(mvp, float4(i.pos, 1.0)); o.nrm = i.nrm; o.uv = i.uv; return o; }\n";
    static constexpr const char* kLitPs =
        "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
        "float4 main(VsOut i) : SV_Target {\n"
        "  float l = saturate(dot(normalize(i.nrm), normalize(float3(0.5, 0.8, 0.6)))) * 0.8 + 0.2;\n"
        "  return float4(p0.rgb * l, p0.a);\n"
        "}\n";
    static constexpr const char* kTexPs =
        "Texture2D tex0 : register(t0);\n"
        "SamplerState smp0 : register(s0);\n"
        "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
        "float4 main(VsOut i) : SV_Target {\n"
        "  float l = saturate(dot(normalize(i.nrm), normalize(float3(0.5, 0.8, 0.6)))) * 0.6 + 0.4;\n"
        "  float4 t = tex0.Sample(smp0, i.uv * p1.xy);\n"
        "  return float4(t.rgb * l * p0.rgb, t.a * p0.a);\n"
        "}\n";
    static constexpr const char* kFullVs =
        "struct VsOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
        "VsOut main(uint id : SV_VertexID) {\n"
        "  VsOut o; float2 p = float2((id << 1) & 2, id & 2);\n"
        "  o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = p; return o; }\n";
    static constexpr const char* kBackdropPs =
        "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
        "  float2 c = floor(pos.xy / 16.0); float ch = fmod(c.x + c.y, 2.0);\n"
        "  return float4(lerp(float3(0.2, 0.35, 0.6), float3(0.8, 0.7, 0.3), uv.x) * (0.6 + 0.4 * ch), 1.0); }\n";
    static constexpr const char* kSolidPs =
        "float4 main(float4 pos : SV_Position) : SV_Target { return p0; }\n";
    static constexpr const char* kFlatVs =
        "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
        "float4 main(VsIn i) : SV_Position { return mul(mvp, float4(i.pos, 1.0)); }\n";

    [[nodiscard]] static std::string with_root(const char* body) { return std::string(kRoot) + body; }
};

class Cases {
public:
    using DrawFn = std::function<bool(Kit&, rr::CaptureSet&, std::string&)>;
    using NeedFn = std::function<std::string(Kit&)>;

    static void single(rr::CaseSpec spec, DrawFn draw, NeedFn need = {});

    class Steps : public D3d12Scene {
    public:
        std::function<bool(Kit&, std::string&)> on_setup;
        std::function<bool(Kit&, unsigned, std::string&)> on_frame;
        std::function<bool(Kit&, unsigned, rr::CaptureSet&, std::string&)> on_capture;
        NeedFn need;
        [[nodiscard]] std::string unsupported(Kit& kit) override { return need ? need(kit) : std::string(); }
        [[nodiscard]] bool setup(Kit& kit, std::string& error) override { return on_setup ? on_setup(kit, error) : true; }
        [[nodiscard]] bool frame(Kit& kit, unsigned index, std::string& error) override { return on_frame ? on_frame(kit, index, error) : true; }
        [[nodiscard]] bool capture(Kit& kit, unsigned index, rr::CaptureSet& out, std::string& error) override { return on_capture(kit, index, out, error); }
    };

    static void frames(rr::CaseSpec spec, std::function<void(Steps&)> build, NeedFn need = {});
};

[[nodiscard]] bool fail_with(std::string& error, const std::string& text);
[[nodiscard]] std::string lacks_options(Kit& kit, const char* what, const std::function<bool(Kit&)>& test);
void set_cbv_root(ID3D12GraphicsCommandList* list, const rr::Mat4& mvp, const float p0[4], const float p1[4] = nullptr);
[[nodiscard]] rr::Mat4 camera(float aspect, float distance, float yaw, float pitch);
[[nodiscard]] D3D12_VIEWPORT viewport_of(UINT width, UINT height);
[[nodiscard]] D3D12_RECT rect_of(UINT width, UINT height);
}  // namespace lsw::suites::render::d3d12
