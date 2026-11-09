#include "render/d3d12/scene_util.hpp"

namespace lsw::suites::render::d3d12 {

namespace {

constexpr UINT kSize = 256;

struct ShadowState {
    Stage stage;
    Surface shadow;
    Geometry ground;
    ID3D12PipelineState* depth_pso = nullptr;
    ID3D12PipelineState* lit_pso = nullptr;
    UploadRing ring;
};

void register_shadow() {
    Cases::frames(rr::CaseSpec("scene", "shadow_map_pcf_moving_objects").size(kSize, kSize).frames(6, {0, 3, 5}).tolerance(rr::Tolerance::blend(6).with_edge(1)).tag("scene"), [](Cases::Steps& steps) {
        auto s = std::make_shared<ShadowState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!s->stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, kSize) || !s->shadow.depth(kit, 256, 256, DXGI_FORMAT_D32_FLOAT, error) ||
                !s->ground.create(kit, Shapes::grid(24, 1.6f), true, error) || !s->ring.create(kit, 256, 12, error)) {
                return false;
            }
            Binder& binder = s->stage.canvas.binder;
            binder.srv_texture(0, s->shadow.resource, DXGI_FORMAT_R32_FLOAT);
            D3D12_SAMPLER_DESC cmp = Binder::sampler_desc(D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
            cmp.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            binder.sampler(0, cmp);
            Pso depth_builder(kit, binder.signature);
            depth_builder.vs(Hlsl12::with_root(Hlsl12::kFlatVs)).targets({}).depth(DXGI_FORMAT_D32_FLOAT);
            depth_builder.desc.NumRenderTargets = 0;
            depth_builder.desc.RasterizerState.DepthBias = 200;
            depth_builder.desc.RasterizerState.SlopeScaledDepthBias = 2.0f;
            s->depth_pso = depth_builder.build(error);
            const char* vs =
                "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float4 lpos : LPOS; };\n"
                "VsOut main(VsIn i) { VsOut o; o.pos = mul(mvp, float4(i.pos, 1)); o.nrm = i.nrm; o.lpos = mul(lightMvp, float4(i.pos, 1)); return o; }\n";
            const char* ps =
                "Texture2D<float> shadowmap : register(t0); SamplerComparisonState cmp : register(s0);\n"
                "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float4 lpos : LPOS; };\n"
                "float4 main(VsOut i) : SV_Target { float3 p = i.lpos.xyz / i.lpos.w; float2 uv = p.xy * float2(0.5, -0.5) + 0.5; float sh = 0;\n"
                "  for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) sh += shadowmap.SampleCmp(cmp, uv + float2(x, y) / 256.0, p.z);\n"
                "  sh /= 9.0; float l = saturate(dot(normalize(i.nrm), normalize(float3(0.4, 0.9, 0.3)))); return float4(p0.rgb * (0.25 + 0.75 * l * sh), 1); }\n";
            const std::string vs_text = std::string(Hlsl12::kRoot) + "cbuffer L : register(b1) { column_major float4x4 lightMvp; };\n" + vs;
            Pso lit(kit, binder.signature);
            lit.vs(vs_text).ps(Hlsl12::with_root(ps)).targets({DXGI_FORMAT_R8G8B8A8_UNORM}).depth(DXGI_FORMAT_D32_FLOAT);
            s->lit_pso = lit.build(error);
            return s->depth_pso != nullptr && s->lit_pso != nullptr;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            const float eye_light[3] = {2.2f, 3.2f, 1.6f};
            const float zero[3] = {0, 0, 0};
            const float up[3] = {0, 1, 0};
            const rr::Mat4 light_vp = SceneMath::ortho_d3d(4.2f, 4.2f, 0.5f, 8.0f) * SceneMath::look_at(eye_light, zero, up);
            const float eye[3] = {0.0f, 2.2f, 3.6f};
            const rr::Mat4 camera = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * SceneMath::look_at(eye, zero, up);
            const float t = static_cast<float>(frame);
            struct Object {
                const Geometry* geometry;
                rr::Mat4 model;
                float color[4];
            };
            const Object objects[3] = {{&s->ground, rr::Mat4::translate(0, -0.55f, 0), {0.55f, 0.6f, 0.5f, 1}},
                                       {&s->stage.torus, rr::Mat4::translate(-0.45f + 0.18f * t, 0.1f, 0.2f) * rr::Mat4::rotate_x(0.7f + 0.35f * t), {0.9f, 0.5f, 0.2f, 1}},
                                       {&s->stage.sphere, rr::Mat4::translate(0.7f - 0.15f * t, 0.1f + 0.1f * std::sin(t), -0.3f) * rr::Mat4::scale(0.7f, 0.7f, 0.7f), {0.3f, 0.6f, 0.95f, 1}}};
            for (UINT k = 0; k < 3; ++k) {
                const rr::Mat4 lm = light_vp * objects[k].model;
                std::memcpy(s->ring.at((frame % 3) * 4 + k), lm.m, 64);
            }
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                Binder& binder = s->stage.canvas.binder;
                s->shadow.to(list, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                list->OMSetRenderTargets(0, nullptr, FALSE, &s->shadow.view);
                list->ClearDepthStencilView(s->shadow.view, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                const D3D12_VIEWPORT vp = viewport_of(256, 256);
                const D3D12_RECT scissor = rect_of(256, 256);
                list->RSSetViewports(1, &vp);
                list->RSSetScissorRects(1, &scissor);
                binder.bind_graphics(list);
                list->SetPipelineState(s->depth_pso);
                for (const Object& o : objects) {
                    list->SetGraphicsRoot32BitConstants(0, 16, (light_vp * o.model).m, 0);
                    o.geometry->bind(list);
                    o.geometry->draw(list);
                }
                s->shadow.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                const float clear[4] = {0.1f, 0.12f, 0.18f, 1};
                s->stage.canvas.begin(list, clear);
                list->SetPipelineState(s->lit_pso);
                for (UINT k = 0; k < 3; ++k) {
                    set_cbv_root(list, camera * objects[k].model, objects[k].color);
                    list->SetGraphicsRootConstantBufferView(1, s->ring.address((frame % 3) * 4 + k));
                    objects[k].geometry->bind(list);
                    objects[k].geometry->draw(list);
                }
            }, error);
        };
        steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
    });
}

struct ParticleState {
    Stage stage;
    Cs update;
    ID3D12Resource* particles = nullptr;
    ID3D12Resource* alive = nullptr;
    ID3D12Resource* args = nullptr;
    ID3D12Resource* zero = nullptr;
    ID3D12CommandSignature* signature = nullptr;
    ID3D12PipelineState* draw = nullptr;
    Binder draw_binder;
};

void register_particles() {
    Cases::frames(rr::CaseSpec("scene", "gpu_particles_append_counter_execute_indirect").size(kSize, kSize).frames(8, {1, 4, 7}).tolerance(rr::Tolerance::blend(8).with_edge(1)).tag("scene"), [](Cases::Steps& steps) {
        auto s = std::make_shared<ParticleState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!s->stage.open(kit, error)) {
                return false;
            }
            const char* source =
                "struct P { float4 a; float4 b; };\nRWStructuredBuffer<P> particles : register(u0); AppendStructuredBuffer<uint> alive : register(u1);\n"
                "uint hash(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }\n"
                "[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
                "  P p = particles[id.x]; float dt = 1.0 / 20.0; p.a.zw += float2(0, -0.9) * dt; p.a.xy += p.a.zw * dt; p.b.x -= dt;\n"
                "  if (p.b.x <= 0 || p.a.y < -1.2) { uint h = hash(id.x * 7919u + (uint)p0.x * 104729u); p.a.xy = float2(((h & 255) / 255.0 - 0.5) * 0.3, -0.7); p.a.zw = float2((((h >> 8) & 255) / 255.0 - 0.5) * 0.9, 0.9 + ((h >> 16) & 255) / 255.0 * 0.8); p.b.x = 0.5 + ((h >> 24) / 255.0) * 1.2; p.b.y = (h & 1023) / 1023.0; }\n"
                "  particles[id.x] = p; if (p.b.x > 0.0 && (id.x % 3) != 2) alive.Append(id.x); }\n";
            if (!s->update.open(kit, source, error)) {
                return false;
            }
            std::vector<float> data;
            std::uint32_t seed = 99;
            for (UINT k = 0; k < 1024; ++k) {
                const float r0 = rr::Pattern::unit_random(seed);
                const float r1 = rr::Pattern::unit_random(seed);
                const float r2 = rr::Pattern::unit_random(seed);
                const float values[8] = {(r0 - 0.5f) * 0.3f, -0.7f + r1 * 0.5f, (r1 - 0.5f) * 0.6f, 0.9f + r2, 0.2f + r0 * 1.5f, r2, 0, 0};
                data.insert(data.end(), values, values + 8);
            }
            HRESULT result = S_OK;
            s->particles = D3d12Tools::device_buffer(kit.rig, data.size() * 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &result);
            s->alive = D3d12Tools::device_buffer(kit.rig, 8192 + 256, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &result);
            s->args = D3d12Tools::device_buffer(kit.rig, 16, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_NONE, &result);
            const std::uint32_t zero_word = 0;
            s->zero = kit.make_upload(&zero_word, 4, error);
            if (s->particles == nullptr || s->alive == nullptr || s->args == nullptr || s->zero == nullptr) {
                return fail_with(error, error.empty() ? "particle buffers: " + D3d12Tools::hr(result) : error);
            }
            const D3D12_DRAW_ARGUMENTS args{6, 0, 0, 0};
            if (!D3d12Tools::write_buffer(kit.rig, s->particles, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, data.data(), data.size() * 4, error) ||
                !D3d12Tools::write_buffer(kit.rig, s->args, D3D12_RESOURCE_STATE_COMMON, 0, &args, sizeof args, error)) {
                return false;
            }
            s->update.binder.uav_buffer(0, s->particles, DXGI_FORMAT_UNKNOWN, 0, 1024, 32);
            D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
            desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            desc.Buffer.NumElements = 1024;
            desc.Buffer.StructureByteStride = 4;
            desc.Buffer.CounterOffsetInBytes = 4096 * 2;
            kit.device()->CreateUnorderedAccessView(s->alive, s->alive, &desc, D3d12Tools::cpu_at(s->update.binder.heap, Binder::kSrvSlots + 1, s->update.binder.increment));
            D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
            (void)flags;
            Binder& binder = s->stage.canvas.binder;
            binder.srv_buffer(0, s->particles, DXGI_FORMAT_UNKNOWN, 0, 1024, 32);
            binder.srv_buffer(1, s->alive, DXGI_FORMAT_UNKNOWN, 0, 1024, 4);
            const char* vs =
                "struct P { float4 a; float4 b; };\nStructuredBuffer<P> particles : register(t0); StructuredBuffer<uint> alive : register(t1);\n"
                "struct VsOut { float4 pos : SV_Position; float4 col : COLOR; float2 q : TEXCOORD0; };\n"
                "VsOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID) { VsOut o; P p = particles[alive[iid]]; uint c = vid % 6; float2 q = float2((c == 1 || c == 4 || c == 5) ? 1 : -1, (c == 2 || c == 3 || c == 5) ? 1 : -1);\n"
                "  float size = 0.012 + 0.02 * saturate(p.b.x); o.pos = float4(p.a.xy + q * size, 0, 1); o.q = q; o.col = float4(lerp(float3(1, 0.4, 0.1), float3(0.2, 0.5, 1), p.b.y) * saturate(p.b.x + 0.2), 1); return o; }\n";
            const char* ps =
                "struct VsOut { float4 pos : SV_Position; float4 col : COLOR; float2 q : TEXCOORD0; };\n"
                "float4 main(VsOut i) : SV_Target { float d = saturate(1 - dot(i.q, i.q)); return float4(i.col.rgb * d, d); }\n";
            Pso builder(kit, binder.signature);
            builder.vs(Hlsl12::with_root(vs)).ps(ps).layout({}).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
            D3D12_RENDER_TARGET_BLEND_DESC& blend = builder.desc.BlendState.RenderTarget[0];
            blend.BlendEnable = TRUE;
            blend.SrcBlend = D3D12_BLEND_ONE;
            blend.DestBlend = D3D12_BLEND_ONE;
            blend.SrcBlendAlpha = D3D12_BLEND_ONE;
            blend.DestBlendAlpha = D3D12_BLEND_ONE;
            s->draw = builder.build(error);
            D3D12_INDIRECT_ARGUMENT_DESC argument{};
            argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
            D3D12_COMMAND_SIGNATURE_DESC signature_desc{};
            signature_desc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
            signature_desc.NumArgumentDescs = 1;
            signature_desc.pArgumentDescs = &argument;
            Com<ID3D12CommandSignature> signature;
            const HRESULT created = kit.device()->CreateCommandSignature(&signature_desc, nullptr, __uuidof(ID3D12CommandSignature), signature.put_void());
            if (FAILED(created)) {
                return fail_with(error, "CreateCommandSignature: " + D3d12Tools::hr(created));
            }
            s->signature = kit.rig.hold(std::move(signature));
            return s->draw != nullptr;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                D3d12Tools::move(list, s->alive, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
                list->CopyBufferRegion(s->alive, 8192, s->zero, 0, 4);
                D3d12Tools::move(list, s->alive, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                D3d12Tools::move(list, s->args, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
                s->update.bind(list);
                const float frame_value[4] = {static_cast<float>(frame + 1), 0, 0, 0};
                list->SetComputeRoot32BitConstants(0, 4, frame_value, 16);
                list->Dispatch(16, 1, 1);
                const D3D12_RESOURCE_BARRIER uav = D3d12Tools::uav_barrier(s->alive);
                list->ResourceBarrier(1, &uav);
                D3d12Tools::move(list, s->alive, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
                list->CopyBufferRegion(s->args, 4, s->alive, 8192, 4);
                D3d12Tools::move(list, s->alive, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                D3d12Tools::move(list, s->particles, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                D3d12Tools::move(list, s->args, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
                const float clear[4] = {0.01f, 0.01f, 0.03f, 1};
                s->stage.canvas.begin(list, clear);
                list->SetPipelineState(s->draw);
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                list->ExecuteIndirect(s->signature, 1, s->args, 0, nullptr, 0);
                D3d12Tools::move(list, s->args, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);
                D3d12Tools::move(list, s->alive, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                D3d12Tools::move(list, s->particles, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }, error);
        };
        steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
    });
}

const struct Registrar {
    Registrar() {
        register_shadow();
        register_particles();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
