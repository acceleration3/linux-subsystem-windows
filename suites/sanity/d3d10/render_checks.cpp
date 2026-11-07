#include "sanity/d3d10/render_checks.hpp"

#include "sanity/d3d10/kit.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

namespace lsw::suites::sanity::d3d10 {

namespace {

constexpr const char* kPipelineVs =
    "struct I { float2 p : POSITION; float2 o : OFFSET; float4 c : COLOR; uint id : SV_InstanceID; };\n"
    "struct O { float4 p : SV_Position; float4 c : COLOR; };\n"
    "O main(I v) { O o; o.p = float4(v.p + v.o, 0.2 + 0.2 * v.id, 1); o.c = v.c; return o; }\n";

constexpr const char* kPipelineGs =
    "struct G { float4 p : SV_Position; float4 c : COLOR; };\n"
    "[maxvertexcount(6)] void main(triangle G t[3], inout TriangleStream<G> s) {\n"
    "  for (int i = 0; i < 3; i++) { s.Append(t[i]); }\n"
    "  s.RestartStrip();\n"
    "  for (int j = 0; j < 3; j++) { G g = t[j]; g.p.x = g.p.x * 0.5 + 0.5; g.p.y = g.p.y * 0.5 + 0.4; g.c = g.c * float4(0.5, 0.5, 0.5, 1.0); s.Append(g); }\n"
    "}\n";

constexpr const char* kPipelinePs =
    "float4 main(float4 p : SV_Position, float4 c : COLOR) : SV_Target { return c; }\n";

struct Instance {
    float x, y;
    float r, g, b, a;
};

bool pipeline_scene(D3d10Device& d, SceneImage& out, std::string& error) {
    ID3D10Device* device = d.device;
    Com<ID3D10VertexShader> vs = D3d10Tools::vs(d, kPipelineVs, error);
    Com<ID3D10GeometryShader> gs = D3d10Tools::gs(d, kPipelineGs, error);
    Com<ID3D10PixelShader> ps = D3d10Tools::ps(d, kPipelinePs, error);
    const std::vector<std::uint8_t> code = D3d10Tools::vs_code(kPipelineVs, error);
    if (!vs || !gs || !ps || code.empty()) {
        return false;
    }
    const D3D10_INPUT_ELEMENT_DESC elements[3] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
                                                  {"OFFSET", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D10_INPUT_PER_INSTANCE_DATA, 1},
                                                  {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 8, D3D10_INPUT_PER_INSTANCE_DATA, 1}};
    Com<ID3D10InputLayout> layout;
    if (FAILED(device->CreateInputLayout(elements, 3, code.data(), code.size(), layout.put()))) {
        error = "the input layout cannot be created";
        return false;
    }
    const float triangle[6] = {-0.8f, -0.8f, -0.8f, -0.2f, -0.2f, -0.8f};
    const Instance first[3] = {{0.0f, 0.0f, 1.0f, 0.2f, 0.2f, 1.0f}, {0.4f, 0.4f, 0.2f, 1.0f, 0.2f, 1.0f}, {0.8f, 0.8f, 0.2f, 0.2f, 1.0f, 1.0f}};
    const Instance second[1] = {{0.0f, 0.3f, 1.0f, 1.0f, 0.0f, 0.5f}};
    Com<ID3D10Buffer> vertices = D3d10Tools::buffer(d, sizeof(triangle), D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, 0, triangle);
    Com<ID3D10Buffer> first_buffer = D3d10Tools::buffer(d, sizeof(first), D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, 0, first);
    Com<ID3D10Buffer> second_buffer = D3d10Tools::buffer(d, sizeof(second), D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, 0, second);
    Com<ID3D10Texture2D> target = D3d10Tools::target(d, 64, 64);
    Com<ID3D10Texture2D> depth = D3d10Tools::texture(d, D3d10Tools::desc2d(64, 64, DXGI_FORMAT_D24_UNORM_S8_UINT, D3D10_BIND_DEPTH_STENCIL));
    if (!vertices || !first_buffer || !second_buffer || !target || !depth) {
        error = "the scene resources cannot be created";
        return false;
    }
    Com<ID3D10RenderTargetView> view = D3d10Tools::rtv(d, target.get());
    Com<ID3D10DepthStencilView> depth_view;
    device->CreateDepthStencilView(depth.get(), nullptr, depth_view.put());
    D3D10_DEPTH_STENCIL_DESC first_depth{};
    first_depth.DepthEnable = TRUE;
    first_depth.DepthWriteMask = D3D10_DEPTH_WRITE_MASK_ALL;
    first_depth.DepthFunc = D3D10_COMPARISON_LESS;
    first_depth.StencilEnable = TRUE;
    first_depth.StencilReadMask = 0xFF;
    first_depth.StencilWriteMask = 0xFF;
    first_depth.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_REPLACE, D3D10_COMPARISON_ALWAYS};
    first_depth.BackFace = first_depth.FrontFace;
    D3D10_DEPTH_STENCIL_DESC second_depth = first_depth;
    second_depth.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_COMPARISON_NOT_EQUAL};
    second_depth.BackFace = second_depth.FrontFace;
    Com<ID3D10DepthStencilState> first_state;
    Com<ID3D10DepthStencilState> second_state;
    device->CreateDepthStencilState(&first_depth, first_state.put());
    device->CreateDepthStencilState(&second_depth, second_state.put());
    D3D10_BLEND_DESC blend{};
    blend.BlendEnable[0] = TRUE;
    blend.SrcBlend = D3D10_BLEND_SRC_ALPHA;
    blend.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D10_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D10_BLEND_ONE;
    blend.DestBlendAlpha = D3D10_BLEND_ZERO;
    blend.BlendOpAlpha = D3D10_BLEND_OP_ADD;
    blend.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;
    Com<ID3D10BlendState> blend_state;
    device->CreateBlendState(&blend, blend_state.put());
    D3D10_RASTERIZER_DESC raster{};
    raster.FillMode = D3D10_FILL_SOLID;
    raster.CullMode = D3D10_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    Com<ID3D10RasterizerState> raster_state;
    device->CreateRasterizerState(&raster, raster_state.put());
    D3d10Tools::clear(d, view.get(), 0.0f, 0.0f, 0.0f, 1.0f);
    device->ClearDepthStencilView(depth_view.get(), D3D10_CLEAR_DEPTH | D3D10_CLEAR_STENCIL, 1.0f, 0);
    ID3D10RenderTargetView* targets[1] = {view.get()};
    device->OMSetRenderTargets(1, targets, depth_view.get());
    D3d10Tools::set_viewport(d, 64, 64);
    device->RSSetState(raster_state.get());
    device->IASetInputLayout(layout.get());
    device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    device->VSSetShader(vs.get());
    device->GSSetShader(gs.get());
    device->PSSetShader(ps.get());
    const UINT strides[2] = {8, sizeof(Instance)};
    const UINT offsets[2] = {0, 0};
    ID3D10Buffer* buffers[2] = {vertices.get(), first_buffer.get()};
    device->IASetVertexBuffers(0, 2, buffers, strides, offsets);
    device->OMSetDepthStencilState(first_state.get(), 1);
    device->DrawInstanced(3, 3, 0, 0);
    buffers[1] = second_buffer.get();
    device->IASetVertexBuffers(0, 2, buffers, strides, offsets);
    device->OMSetDepthStencilState(second_state.get(), 1);
    const float factor[4] = {0, 0, 0, 0};
    device->OMSetBlendState(blend_state.get(), factor, 0xFFFFFFFFu);
    device->DrawInstanced(3, 1, 0, 0);
    device->ClearState();
    if (!d.wait_idle(error)) {
        return false;
    }
    TexelImage image;
    if (!D3d10Tools::read_texture(d, target.get(), image, error)) {
        return false;
    }
    out.width = 64;
    out.height = 64;
    out.rgba = false;
    out.data = image.data;
    return true;
}

struct PositionColor {
    float x, y;
    float r, g, b, a;
};

constexpr const char* kQuadVs =
    "struct I { float2 p : POSITION; float4 c : COLOR; };\n"
    "struct O { float4 p : SV_Position; float4 c : COLOR; };\n"
    "O main(I i) { O o; o.p = float4(i.p, 0.5, 1); o.c = i.c; return o; }\n";

constexpr const char* kQuadPs = "float4 main(float4 p : SV_Position, float4 c : COLOR) : SV_Target { return c; }\n";

class QuadPipeline {
public:
    [[nodiscard]] bool init(D3d10Device& d, std::string& error) {
        device = &d;
        vs = D3d10Tools::vs(d, kQuadVs, error);
        ps = D3d10Tools::ps(d, kQuadPs, error);
        const std::vector<std::uint8_t> code = D3d10Tools::vs_code(kQuadVs, error);
        const D3D10_INPUT_ELEMENT_DESC elements[2] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
                                                      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D10_INPUT_PER_VERTEX_DATA, 0}};
        D3D10_RASTERIZER_DESC raster_desc{};
        raster_desc.FillMode = D3D10_FILL_SOLID;
        raster_desc.CullMode = D3D10_CULL_NONE;
        raster_desc.DepthClipEnable = TRUE;
        d.device->CreateRasterizerState(&raster_desc, raster.put());
        return vs && ps && raster && !code.empty() && SUCCEEDED(d.device->CreateInputLayout(elements, 2, code.data(), code.size(), layout.put()));
    }

    void draw(ID3D10Buffer* buffer, UINT first_vertex, ID3D10RenderTargetView* view, UINT width, UINT height) {
        ID3D10Device* d = device->device;
        ID3D10RenderTargetView* targets[1] = {view};
        d->OMSetRenderTargets(1, targets, nullptr);
        D3d10Tools::set_viewport(*device, width, height);
        d->RSSetState(raster.get());
        d->IASetInputLayout(layout.get());
        d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        const UINT stride = sizeof(PositionColor);
        const UINT offset = 0;
        ID3D10Buffer* raw = buffer;
        d->IASetVertexBuffers(0, 1, &raw, &stride, &offset);
        d->VSSetShader(vs.get());
        d->PSSetShader(ps.get());
        d->Draw(4, first_vertex);
    }

    static void quad(PositionColor out[4], float x0, float y0, float x1, float y1, const float color[4]) {
        out[0] = {x0, y1, color[0], color[1], color[2], color[3]};
        out[1] = {x0, y0, color[0], color[1], color[2], color[3]};
        out[2] = {x1, y1, color[0], color[1], color[2], color[3]};
        out[3] = {x1, y0, color[0], color[1], color[2], color[3]};
    }

    D3d10Device* device = nullptr;
    Com<ID3D10VertexShader> vs;
    Com<ID3D10PixelShader> ps;
    Com<ID3D10InputLayout> layout;
    Com<ID3D10RasterizerState> raster;
};

}  // namespace

Outcome RenderChecks::pipeline_warp(Context& context) {
    Problems problems;
    for (const bool with_1 : {false, true}) {
        if (with_1 && !D3d10Device::library_available(true)) {
            context.note("d3d10_1.dll is not available");
            continue;
        }
        D3d10Rig rig;
        std::string error;
        if (!rig.open(with_1, error)) {
            return Outcome::fail(error);
        }
        rig.compare(context, problems, std::string(Kit::name(with_1)) + " VS GS PS depth stencil blend instancing pipeline", pipeline_scene, Tolerance::bytes(3, 0.002));
    }
    return problems.outcome();
}

Outcome RenderChecks::streamout(Context& context) {
    AvAdapter adapter;
    D3d10Device d;
    std::string error;
    if (!Kit::open(adapter, d, false, error)) {
        return Outcome::fail(error);
    }
    ID3D10Device* device = d.device;
    Problems problems;
    const std::string vs_source = "float4 main(float4 p : POSITION) : POSITION { return p; }\n";
    const std::string point_gs = "struct S { float4 p : POSITION; };\n[maxvertexcount(1)] void main(point S v[1], inout PointStream<S> s) { s.Append(v[0]); }\n";
    const std::string triangle_gs = "struct S { float4 p : POSITION; };\n[maxvertexcount(3)] void main(triangle S v[3], inout TriangleStream<S> s) { s.Append(v[0]); s.Append(v[1]); s.Append(v[2]); }\n";
    Com<ID3D10VertexShader> vs = D3d10Tools::vs(d, vs_source, error);
    const std::vector<std::uint8_t> vs_bytes = D3d10Tools::vs_code(vs_source, error);
    std::vector<std::uint8_t> point_code;
    std::vector<std::uint8_t> triangle_code;
    if (!vs || vs_bytes.empty() || !D3d10Tools::compile(point_gs, "gs_4_0", point_code, error) || !D3d10Tools::compile(triangle_gs, "gs_4_0", triangle_code, error)) {
        return Outcome::fail(error);
    }
    const D3D10_SO_DECLARATION_ENTRY entry = {"POSITION", 0, 0, 4, 0};
    Com<ID3D10GeometryShader> point_so;
    Com<ID3D10GeometryShader> triangle_so;
    const HRESULT made_points = device->CreateGeometryShaderWithStreamOutput(point_code.data(), point_code.size(), &entry, 1, 16, point_so.put());
    const HRESULT made_triangles = device->CreateGeometryShaderWithStreamOutput(triangle_code.data(), triangle_code.size(), &entry, 1, 16, triangle_so.put());
    if (FAILED(made_points) || FAILED(made_triangles)) {
        return Outcome::fail("a geometry shader with stream output cannot be created: " + Kit::hr(FAILED(made_points) ? made_points : made_triangles));
    }
    const D3D10_INPUT_ELEMENT_DESC element = {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0};
    Com<ID3D10InputLayout> layout;
    device->CreateInputLayout(&element, 1, vs_bytes.data(), vs_bytes.size(), layout.put());
    float points[12][4];
    for (int i = 0; i < 12; ++i) {
        points[i][0] = static_cast<float>(i) * 1.5f - 3.0f;
        points[i][1] = static_cast<float>(i) * -2.25f;
        points[i][2] = static_cast<float>(i) * 0.125f;
        points[i][3] = 1.0f;
    }
    Com<ID3D10Buffer> input = D3d10Tools::buffer(d, sizeof(points), D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, 0, points);
    D3D10_DEPTH_STENCIL_DESC no_depth{};
    Com<ID3D10DepthStencilState> depth_state;
    device->CreateDepthStencilState(&no_depth, depth_state.put());
    if (!layout || !input || !depth_state) {
        return Outcome::fail("the stream output inputs cannot be created");
    }
    struct Case {
        const char* name;
        ID3D10GeometryShader* shader;
        D3D10_PRIMITIVE_TOPOLOGY topology;
        UINT vertices;
        UINT primitives;
        UINT vertices_per_primitive;
        UINT capacity;
    };
    const Case cases[] = {{"points fitting the buffer", point_so.get(), D3D10_PRIMITIVE_TOPOLOGY_POINTLIST, 10, 10, 1, 10},
                          {"points overflowing the buffer", point_so.get(), D3D10_PRIMITIVE_TOPOLOGY_POINTLIST, 10, 10, 1, 6},
                          {"triangles overflowing the buffer", triangle_so.get(), D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 12, 4, 3, 2}};
    for (const Case& c : cases) {
        const UINT capacity_vertices = c.capacity * c.vertices_per_primitive;
        Com<ID3D10Buffer> output = D3d10Tools::buffer(d, capacity_vertices * 16, D3D10_USAGE_DEFAULT, D3D10_BIND_STREAM_OUTPUT, 0, 0, nullptr);
        D3D10_QUERY_DESC query_desc{};
        query_desc.Query = D3D10_QUERY_SO_STATISTICS;
        Com<ID3D10Query> query;
        if (!output || FAILED(device->CreateQuery(&query_desc, query.put()))) {
            problems.add(std::string(c.name) + ": the output buffer or query cannot be created");
            continue;
        }
        ID3D10Buffer* in_buffer = input.get();
        const UINT stride = 16;
        const UINT offset = 0;
        device->IASetInputLayout(layout.get());
        device->IASetPrimitiveTopology(c.topology);
        device->IASetVertexBuffers(0, 1, &in_buffer, &stride, &offset);
        device->VSSetShader(vs.get());
        device->GSSetShader(c.shader);
        device->PSSetShader(nullptr);
        device->OMSetDepthStencilState(depth_state.get(), 0);
        ID3D10Buffer* targets[1] = {output.get()};
        const UINT zero = 0;
        device->SOSetTargets(1, targets, &zero);
        query->Begin();
        device->Draw(c.vertices, 0);
        query->End();
        ID3D10Buffer* none[1] = {nullptr};
        device->SOSetTargets(1, none, &zero);
        device->GSSetShader(nullptr);
        device->Flush();
        D3D10_QUERY_DATA_SO_STATISTICS stats{};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        HRESULT result = S_FALSE;
        while ((result = query->GetData(&stats, sizeof(stats), 0)) == S_FALSE && std::chrono::steady_clock::now() < deadline) {
            ::SwitchToThread();
        }
        if (result != S_OK) {
            problems.add(std::string(c.name) + ": the SO_STATISTICS query does not complete: " + Kit::hr(result));
            continue;
        }
        const UINT written = std::min(c.primitives, c.capacity);
        if (stats.NumPrimitivesWritten != written || stats.PrimitivesStorageNeeded != c.primitives) {
            problems.add(std::string(c.name) + ": SO_STATISTICS reports " + std::to_string(stats.NumPrimitivesWritten) + " written and " + std::to_string(stats.PrimitivesStorageNeeded) + " needed, expected " + std::to_string(written) + " and " + std::to_string(c.primitives));
        }
        std::vector<std::uint8_t> bytes;
        if (!D3d10Tools::read_buffer(d, output.get(), bytes, error)) {
            problems.add(std::string(c.name) + ": " + error);
            continue;
        }
        const UINT expected_vertices = written * c.vertices_per_primitive;
        for (UINT v = 0; v < expected_vertices; ++v) {
            if (std::memcmp(bytes.data() + static_cast<std::size_t>(v) * 16, points[v], 16) != 0) {
                problems.add(std::string(c.name) + ": output vertex " + std::to_string(v) + " differs from the input position");
                break;
            }
        }
        context.measure(std::string("so.") + c.name, std::to_string(stats.NumPrimitivesWritten) + "/" + std::to_string(stats.PrimitivesStorageNeeded));
    }
    device->ClearState();
    return problems.outcome();
}

Outcome RenderChecks::map_contracts(Context& context) {
    AvAdapter adapter;
    D3d10Device d;
    std::string error;
    if (!Kit::open(adapter, d, false, error)) {
        return Outcome::fail(error);
    }
    ID3D10Device* device = d.device;
    QuadPipeline pipeline;
    if (!pipeline.init(d, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    {
        Com<ID3D10Texture2D> source = D3d10Tools::target(d, 16, 16);
        Com<ID3D10RenderTargetView> view = D3d10Tools::rtv(d, source.get());
        D3d10Tools::clear(d, view.get(), 0.2f, 0.6f, 1.0f, 1.0f);
        TexelImage image;
        std::string where;
        if (!D3d10Tools::read_texture(d, source.get(), image, error) || !Kit::uniform(image, D3d10Tools::bgra(51, 153, 255, 255), where)) {
            problems.add("Map(READ) after a GPU clear: " + where + error);
        }
        std::vector<std::uint32_t> words(64);
        for (std::size_t i = 0; i < words.size(); ++i) {
            words[i] = static_cast<std::uint32_t>(0x9E3779B9u * (i + 1));
        }
        Com<ID3D10Buffer> gpu = D3d10Tools::buffer(d, 256, D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, 0, nullptr);
        device->UpdateSubresource(gpu.get(), 0, nullptr, words.data(), 0, 0);
        std::vector<std::uint8_t> bytes;
        if (!D3d10Tools::read_buffer(d, gpu.get(), bytes, error) || std::memcmp(bytes.data(), words.data(), 256) != 0) {
            problems.add("Map(READ) of a buffer written by UpdateSubresource returns other data " + error);
        }
    }
    {
        Com<ID3D10Buffer> vb = D3d10Tools::buffer(d, sizeof(PositionColor) * 4, D3D10_USAGE_DYNAMIC, D3D10_BIND_VERTEX_BUFFER, D3D10_CPU_ACCESS_WRITE, 0, nullptr);
        Com<ID3D10Texture2D> first = D3d10Tools::target(d, 32, 32);
        Com<ID3D10Texture2D> second = D3d10Tools::target(d, 32, 32);
        Com<ID3D10RenderTargetView> first_view = D3d10Tools::rtv(d, first.get());
        Com<ID3D10RenderTargetView> second_view = D3d10Tools::rtv(d, second.get());
        if (!vb) {
            return Outcome::fail("a dynamic vertex buffer cannot be created");
        }
        for (int pass = 0; pass < 2; ++pass) {
            void* memory = nullptr;
            const HRESULT mapped = vb->Map(D3D10_MAP_WRITE_DISCARD, 0, &memory);
            if (FAILED(mapped)) {
                problems.add("Map(WRITE_DISCARD) failed with " + Kit::hr(mapped));
                break;
            }
            std::memset(memory, 0xCD, sizeof(PositionColor) * 4);
            PositionColor quad[4];
            const float red[4] = {1, 0, 0, 1};
            const float blue[4] = {0, 0, 1, 1};
            QuadPipeline::quad(quad, -1, -1, 1, 1, pass == 0 ? red : blue);
            std::memcpy(memory, quad, sizeof(quad));
            vb->Unmap();
            D3d10Tools::clear(d, pass == 0 ? first_view.get() : second_view.get(), 0, 0, 0, 1);
            pipeline.draw(vb.get(), 0, pass == 0 ? first_view.get() : second_view.get(), 32, 32);
        }
        TexelImage a;
        TexelImage b;
        std::string where;
        if (!D3d10Tools::read_texture(d, first.get(), a, error) || !D3d10Tools::read_texture(d, second.get(), b, error)) {
            return Outcome::fail(error);
        }
        if (!Kit::uniform(a, D3d10Tools::bgra(255, 0, 0, 255), where)) {
            problems.add("a draw issued before a WRITE_DISCARD map shows the new data: " + where);
        }
        if (!Kit::uniform(b, D3d10Tools::bgra(0, 0, 255, 255), where)) {
            problems.add("a draw after a WRITE_DISCARD map does not show the new data: " + where);
        }
    }
    {
        constexpr int kSlots = 8;
        constexpr int kFrames = 200;
        const UINT slot_bytes = sizeof(PositionColor) * 4;
        Com<ID3D10Buffer> ring = D3d10Tools::buffer(d, slot_bytes * kSlots, D3D10_USAGE_DYNAMIC, D3D10_BIND_VERTEX_BUFFER, D3D10_CPU_ACCESS_WRITE, 0, nullptr);
        Com<ID3D10Texture2D> target = D3d10Tools::target(d, 128, 128);
        Com<ID3D10RenderTargetView> view = D3d10Tools::rtv(d, target.get());
        if (!ring) {
            return Outcome::fail("the ring buffer cannot be created");
        }
        D3d10Tools::clear(d, view.get(), 0, 0, 0, 1);
        for (int frame = 0; frame < kFrames; ++frame) {
            const int slot = frame % kSlots;
            const int tile = frame % 64;
            void* memory = nullptr;
            const HRESULT mapped = slot == 0 ? ring->Map(D3D10_MAP_WRITE_DISCARD, 0, &memory) : ring->Map(D3D10_MAP_WRITE_NO_OVERWRITE, 0, &memory);
            if (FAILED(mapped)) {
                problems.add("frame " + std::to_string(frame) + ": the ring Map failed with " + Kit::hr(mapped));
                break;
            }
            float color[4];
            Kit::frame_floats(frame, color);
            const float x0 = static_cast<float>((tile % 8) * 16) / 64.0f - 1.0f;
            const float y1 = 1.0f - static_cast<float>((tile / 8) * 16) / 64.0f;
            PositionColor quad[4];
            QuadPipeline::quad(quad, x0, y1 - 0.25f, x0 + 0.25f, y1, color);
            std::memcpy(static_cast<std::uint8_t*>(memory) + static_cast<std::size_t>(slot) * slot_bytes, quad, slot_bytes);
            ring->Unmap();
            pipeline.draw(ring.get(), static_cast<UINT>(slot) * 4, view.get(), 128, 128);
        }
        TexelImage image;
        if (!D3d10Tools::read_texture(d, target.get(), image, error)) {
            return Outcome::fail(error);
        }
        int wrong = 0;
        for (int tile = 0; tile < 64; ++tile) {
            const int last = tile + 64 * ((kFrames - 1 - tile) / 64);
            if (image.pixel(static_cast<UINT>(tile % 8) * 16 + 8, static_cast<UINT>(tile / 8) * 16 + 8) != Kit::frame_color(last)) {
                ++wrong;
            }
        }
        if (wrong != 0) {
            problems.add(std::to_string(wrong) + " of 64 tiles do not show the data their draw consumed through WRITE_NO_OVERWRITE");
        }
    }
    {
        Com<ID3D10Texture2D> heavy = D3d10Tools::target(d, 2048, 2048);
        Com<ID3D10RenderTargetView> view = D3d10Tools::rtv(d, heavy.get());
        Com<ID3D10Buffer> vb = D3d10Tools::buffer(d, sizeof(PositionColor) * 4, D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, 0, nullptr);
        Com<ID3D10Texture2D> staging = D3d10Tools::texture(d, D3d10Tools::desc2d(2048, 2048, DXGI_FORMAT_B8G8R8A8_UNORM, 0, D3D10_USAGE_STAGING, D3D10_CPU_ACCESS_READ));
        if (!view || !vb || !staging) {
            return Outcome::fail("the DO_NOT_WAIT resources cannot be created");
        }
        for (int k = 0; k < 120; ++k) {
            PositionColor quad[4];
            float color[4];
            Kit::frame_floats(k, color);
            QuadPipeline::quad(quad, -1, -1, 1, 1, color);
            device->UpdateSubresource(vb.get(), 0, nullptr, quad, 0, 0);
            pipeline.draw(vb.get(), 0, view.get(), 2048, 2048);
        }
        device->CopyResource(staging.get(), heavy.get());
        device->Flush();
        D3D10_MAPPED_TEXTURE2D mapped{};
        int still_drawing = 0;
        HRESULT result = S_OK;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while ((result = staging->Map(0, D3D10_MAP_READ, D3D10_MAP_FLAG_DO_NOT_WAIT, &mapped)) == DXGI_ERROR_WAS_STILL_DRAWING) {
            ++still_drawing;
            if (std::chrono::steady_clock::now() > deadline) {
                break;
            }
            ::Sleep(1);
        }
        context.measure("do_not_wait.still_drawing", still_drawing);
        if (result != S_OK) {
            problems.add("Map(READ, DO_NOT_WAIT) returned " + Kit::hr(result) + " instead of success or DXGI_ERROR_WAS_STILL_DRAWING followed by success");
        } else {
            std::uint32_t first = 0;
            std::memcpy(&first, mapped.pData, 4);
            if (first != Kit::frame_color(119)) {
                problems.add("Map(READ, DO_NOT_WAIT) succeeded with 0x" + Text::hex(first, 8) + " instead of the last frame 0x" + Text::hex(Kit::frame_color(119), 8));
            }
            staging->Unmap(0);
        }
    }
    device->ClearState();
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d10
