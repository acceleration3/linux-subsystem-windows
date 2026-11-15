#include "render/d3d12/scene_util.hpp"

namespace lsw::suites::render::d3d12 {

namespace {

struct SkinVertex {
    float pos[3];
    float normal[3];
    float weight[2];
    UINT bone[2];
};

struct SkinState {
    Stage stage;
    Geometry mesh;
    UploadRing ring;
    ID3D12PipelineState* state = nullptr;
};

void register_skinning() {
    Cases::frames(rr::CaseSpec("scene", "gpu_skinning_bone_palette_animation").size(256, 256).frames(8, {0, 3, 7}).tolerance(rr::Tolerance::blend(5).with_edge(1)).tag("scene"), [](Cases::Steps& steps) {
        auto s = std::make_shared<SkinState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!s->stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, 256) || !s->ring.create(kit, 256, 3, error)) {
                return false;
            }
            std::vector<SkinVertex> vertices;
            const unsigned rings = 24;
            const unsigned slices = 16;
            auto make = [&](unsigned r, unsigned k) {
                const float y = -1.0f + 2.0f * static_cast<float>(r) / rings;
                const float a = 6.2831853f * static_cast<float>(k) / slices;
                SkinVertex v{};
                v.pos[0] = 0.28f * std::cos(a);
                v.pos[1] = y;
                v.pos[2] = 0.28f * std::sin(a);
                v.normal[0] = std::cos(a);
                v.normal[2] = std::sin(a);
                const float seg = (y + 1.0f) * 0.5f * 3.0f;
                const unsigned i0 = seg >= 3.0f ? 2u : static_cast<unsigned>(seg);
                v.bone[0] = i0;
                v.bone[1] = i0 + 1;
                v.weight[1] = seg - static_cast<float>(i0);
                v.weight[0] = 1.0f - v.weight[1];
                return v;
            };
            for (unsigned r = 0; r < rings; ++r) {
                for (unsigned k = 0; k < slices; ++k) {
                    const SkinVertex q[4] = {make(r, k), make(r, k + 1), make(r + 1, k + 1), make(r + 1, k)};
                    const int order[6] = {0, 1, 2, 0, 2, 3};
                    for (int idx : order) {
                        vertices.push_back(q[idx]);
                    }
                }
            }
            if (!s->mesh.create_vertices(kit, vertices.data(), static_cast<UINT>(vertices.size()), sizeof(SkinVertex), error)) {
                return false;
            }
            const std::vector<D3D12_INPUT_ELEMENT_DESC> layout = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"BLENDINDICES", 0, DXGI_FORMAT_R32G32_UINT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
            const std::string vs = std::string(Hlsl12::kRoot) + "cbuffer Bones : register(b1) { column_major float4x4 bones[4]; };\n"
                "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 w : BLENDWEIGHT; uint2 b : BLENDINDICES; };\nstruct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; };\n"
                "VsOut main(VsIn i) { VsOut o; float4 p = mul(bones[i.b.x], float4(i.pos, 1)) * i.w.x + mul(bones[i.b.y], float4(i.pos, 1)) * i.w.y;\n"
                "  float3 n = mul((float3x3)bones[i.b.x], i.nrm) * i.w.x + mul((float3x3)bones[i.b.y], i.nrm) * i.w.y; o.pos = mul(mvp, p); o.nrm = n; return o; }\n";
            const char* ps =
                "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; };\nfloat4 main(VsOut i) : SV_Target { float l = saturate(dot(normalize(i.nrm), normalize(float3(0.5, 0.6, 0.7)))) * 0.8 + 0.2; return float4(p0.rgb * l, 1); }\n";
            Pso builder(kit, s->stage.canvas.binder.signature);
            builder.vs(vs).ps(Hlsl12::with_root(ps)).layout(layout).targets({DXGI_FORMAT_R8G8B8A8_UNORM}).depth(DXGI_FORMAT_D32_FLOAT);
            s->state = builder.build(error);
            return s->state != nullptr;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            rr::Mat4 bones[4];
            for (int k = 1; k < 4; ++k) {
                const float pivot = -1.0f + 2.0f * static_cast<float>(k) / 3.0f;
                const float angle = 0.55f * std::sin(0.9f * static_cast<float>(frame) + static_cast<float>(k));
                bones[k] = rr::Mat4::translate(0, pivot, 0) * rr::Mat4::rotate_z(angle) * rr::Mat4::translate(0, -pivot, 0);
            }
            for (int k = 0; k < 4; ++k) {
                std::memcpy(s->ring.at(frame % 3, static_cast<UINT>(k) * 64), bones[k].m, 64);
            }
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                s->stage.canvas.begin(list, clear);
                list->SetPipelineState(s->state);
                const float color[4] = {0.9f, 0.6f, 0.3f, 1};
                set_cbv_root(list, s->stage.view_projection() * rr::Mat4::rotate_y(0.4f), color);
                list->SetGraphicsRootConstantBufferView(1, s->ring.address(frame % 3));
                s->mesh.bind(list);
                s->mesh.draw_vertices(list);
            }, error);
        };
        steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
    });
}

struct DeferredState {
    Stage stage;
    Surface albedo;
    Surface normals;
    Geometry ground;
    UploadRing ring;
    ID3D12PipelineState* geometry = nullptr;
    ID3D12PipelineState* lighting = nullptr;
};

void register_deferred() {
    Cases::frames(rr::CaseSpec("scene", "deferred_gbuffer_point_lights_6_frames").size(256, 256).frames(6, {0, 2, 5}).tolerance(rr::Tolerance::blend(8).with_edge(1)).tag("scene"), [](Cases::Steps& steps) {
        auto s = std::make_shared<DeferredState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!s->stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, 256) || !s->albedo.color(kit, 256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, error) ||
                !s->normals.color(kit, 256, 256, DXGI_FORMAT_R16G16B16A16_FLOAT, error) || !s->ground.create(kit, Shapes::grid(20, 1.8f), true, error) || !s->ring.create(kit, 256, 3, error)) {
                return false;
            }
            Binder& binder = s->stage.canvas.binder;
            binder.srv_texture(0, s->albedo.resource, DXGI_FORMAT_R8G8B8A8_UNORM);
            binder.srv_texture(1, s->normals.resource, DXGI_FORMAT_R16G16B16A16_FLOAT);
            binder.srv_texture(2, s->stage.canvas.depth.resource, DXGI_FORMAT_R32_FLOAT);
            const char* gps =
                "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\nstruct Out { float4 c0 : SV_Target0; float4 c1 : SV_Target1; };\n"
                "Out main(VsOut i) { Out o; float2 g = floor(i.uv * 8.0); float ch = fmod(g.x + g.y, 2.0); o.c0 = float4(p0.rgb * (0.6 + 0.4 * ch), 1); o.c1 = float4(normalize(i.nrm) * 0.5 + 0.5, 1); return o; }\n";
            Pso geometry = s->stage.pso(gps);
            geometry.targets({DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT});
            const char* lps =
                "Texture2D<float4> albedo : register(t0); Texture2D<float4> normals : register(t1); Texture2D<float> depth : register(t2);\ncbuffer L : register(b1) { float4 lights[12]; };\n"
                "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { int3 p = int3(pos.xy, 0); float d = depth.Load(p); if (d >= 1.0) return float4(0.05, 0.05, 0.1, 1);\n"
                "  float2 ndc = (pos.xy + 0.5) / 256.0 * float2(2, -2) + float2(-1, 1); float4 w = mul(mvp, float4(ndc, d, 1)); float3 wp = w.xyz / w.w; float3 n = normalize(normals.Load(p).xyz * 2 - 1);\n"
                "  float3 sum = float3(0.12, 0.12, 0.14); for (int k = 0; k < 6; ++k) { float3 L = lights[k].xyz - wp; float dist = length(L); float att = saturate(1 - dist / lights[k].w); sum += lights[6 + k].rgb * att * att * saturate(dot(n, L / dist)); }\n"
                "  return float4(albedo.Load(p).rgb * sum, 1); }\n";
            Pso lighting(kit, binder.signature);
            lighting.vs(Hlsl12::kFullVs).ps(Hlsl12::with_root(lps)).layout({}).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
            s->geometry = geometry.build(error);
            s->lighting = lighting.build(error);
            return s->geometry != nullptr && s->lighting != nullptr;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            float lights[12][4];
            for (int k = 0; k < 6; ++k) {
                const float a = static_cast<float>(frame) * 0.5f + static_cast<float>(k) * 1.047f;
                lights[k][0] = 1.1f * std::cos(a);
                lights[k][1] = 0.25f + 0.4f * std::sin(a * 1.7f);
                lights[k][2] = 1.1f * std::sin(a);
                lights[k][3] = 1.5f;
                lights[6 + k][0] = 0.5f + 0.5f * std::sin(static_cast<float>(k) * 1.3f);
                lights[6 + k][1] = 0.5f + 0.5f * std::sin(static_cast<float>(k) * 2.1f + 1.0f);
                lights[6 + k][2] = 0.5f + 0.5f * std::sin(static_cast<float>(k) * 0.7f + 2.0f);
                lights[6 + k][3] = 1.0f;
            }
            std::memcpy(s->ring.at(frame % 3), lights, sizeof lights);
            const float eye[3] = {0.0f, 1.6f, 3.0f};
            const float zero[3] = {0, 0, 0};
            const float up[3] = {0, 1, 0};
            const rr::Mat4 vp = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * SceneMath::look_at(eye, zero, up);
            const rr::Mat4 inverse = SceneMath::inverse(vp);
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                Canvas& canvas = s->stage.canvas;
                canvas.depth.to(list, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                s->albedo.to(list, D3D12_RESOURCE_STATE_RENDER_TARGET);
                s->normals.to(list, D3D12_RESOURCE_STATE_RENDER_TARGET);
                const D3D12_CPU_DESCRIPTOR_HANDLE views[2] = {s->albedo.view, s->normals.view};
                list->OMSetRenderTargets(2, views, FALSE, &canvas.depth.view);
                const float clear[4] = {0, 0, 0, 1};
                list->ClearRenderTargetView(s->albedo.view, clear, 0, nullptr);
                list->ClearRenderTargetView(s->normals.view, clear, 0, nullptr);
                list->ClearDepthStencilView(canvas.depth.view, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                canvas.set_viewport(list);
                canvas.binder.bind_graphics(list);
                list->SetPipelineState(s->geometry);
                struct Object {
                    const Geometry* geometry;
                    rr::Mat4 model;
                    float color[4];
                };
                const float t = static_cast<float>(frame);
                const Object objects[3] = {{&s->ground, rr::Mat4::translate(0, -0.5f, 0), {0.7f, 0.75f, 0.65f, 1}},
                                           {&s->stage.torus, rr::Mat4::translate(-0.5f, 0.1f, 0) * rr::Mat4::rotate_x(0.8f + 0.3f * t), {0.9f, 0.5f, 0.3f, 1}},
                                           {&s->stage.sphere, rr::Mat4::translate(0.6f, 0.0f, 0.2f) * rr::Mat4::scale(0.8f, 0.8f, 0.8f), {0.3f, 0.5f, 0.95f, 1}}};
                for (const Object& o : objects) {
                    set_cbv_root(list, vp * o.model, o.color);
                    o.geometry->bind(list);
                    o.geometry->draw(list);
                }
                s->albedo.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                s->normals.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                canvas.depth.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                canvas.color.bind_target(list);
                canvas.binder.bind_graphics(list);
                const float none[4] = {0, 0, 0, 0};
                set_cbv_root(list, inverse, none);
                list->SetGraphicsRootConstantBufferView(1, s->ring.address(frame % 3));
                draw_fullscreen(list, s->lighting);
            }, error);
        };
        steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
    });
}

const struct Registrar {
    Registrar() {
        register_skinning();
        register_deferred();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
