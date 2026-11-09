#include "render/d3d12/scene_util.hpp"

namespace lsw::suites::render::d3d12 {

namespace {

struct LifeState {
    Cs step;
    Cs show;
    ID3D12Resource* cells[2] = {nullptr, nullptr};
    D3D12_RESOURCE_STATES states[2] = {D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    Binder binders[2];
    ID3D12Resource* view = nullptr;
};

void register_life() {
    Cases::frames(rr::CaseSpec("scene", "game_of_life_compute_ping_pong_12_frames").size(64, 64).frames(12, {0, 5, 11}).tolerance(rr::Tolerance::exact()).tag("scene"), [](Cases::Steps& steps) {
        auto s = std::make_shared<LifeState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            const char* source =
                "Texture2D<uint> src : register(t0); RWTexture2D<uint> dst : register(u0);\n"
                "[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) { int n = 0; for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) { if (x == 0 && y == 0) continue; n += src[(int2(id.xy) + int2(x, y) + 64) % 64]; }\n"
                "  uint alive = src[id.xy]; dst[id.xy] = (n == 3 || (alive == 1 && n == 2)) ? 1 : 0; }\n";
            for (int k = 0; k < 2; ++k) {
                TextureSpec spec;
                spec.format = DXGI_FORMAT_R32_UINT;
                spec.width = 64;
                spec.height = 64;
                spec.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                s->cells[k] = TextureFactory::create(kit, spec, false, error);
                if (s->cells[k] == nullptr) {
                    return false;
                }
            }
            std::vector<std::uint32_t> seed(64 * 64);
            std::uint32_t state = 4242;
            for (auto& v : seed) {
                v = rr::Pattern::unit_random(state) < 0.32f ? 1u : 0u;
            }
            if (!D3d12Tools::write_texture(kit.rig, s->cells[0], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 0, seed.data(), error) || !s->step.open(kit, source, error)) {
                return false;
            }
            for (int k = 0; k < 2; ++k) {
                if (!s->binders[k].create(kit, error)) {
                    return false;
                }
                s->binders[k].srv_texture(0, s->cells[k], DXGI_FORMAT_R32_UINT);
                s->binders[k].uav_texture(0, s->cells[1 - k], DXGI_FORMAT_R32_UINT);
            }
            return true;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            const unsigned from = frame % 2;
            const unsigned to = 1 - from;
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                D3d12Tools::move(list, s->cells[from], s->states[from], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                D3d12Tools::move(list, s->cells[to], s->states[to], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                s->binders[from].bind_compute(list);
                list->SetPipelineState(s->step.pso);
                list->Dispatch(8, 8, 1);
                s->states[from] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                s->states[to] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            }, error);
        };
        steps.on_capture = [s](Kit& kit, unsigned frame, rr::CaptureSet& out, std::string& error) {
            const unsigned latest = (frame + 1) % 2;
            TexelImage texels;
            if (!D3d12Tools::read_texture(kit.rig, s->cells[latest], s->states[latest], 0, texels, error)) {
                return false;
            }
            rr::Image image(64, 64, rr::PixelKind::Rgba8Unorm);
            for (UINT y = 0; y < 64; ++y) {
                for (UINT x = 0; x < 64; ++x) {
                    const std::uint32_t v = texels.pixel(x, y);
                    image.set_rgba8(x, y, v != 0 ? 0xffe0c040u : 0xff101020u);
                }
            }
            out.add("", std::move(image));
            return true;
        };
    });
}

struct RingState {
    Stage stage;
    UploadRing ring;
    ID3D12PipelineState* state = nullptr;
};

void register_ring() {
    Cases::frames(rr::CaseSpec("scene", "ring_buffer_constants_12_frames_in_flight").size(256, 256).frames(12, {0, 6, 11}).tolerance(rr::Tolerance::blend(4).with_edge(1)).tag("scene"), [](Cases::Steps& steps) {
        auto s = std::make_shared<RingState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!s->stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, 256) || !s->ring.create(kit, 256, 3 * 8, error)) {
                return false;
            }
            const std::string vs = std::string(Hlsl12::kRoot) + "cbuffer Obj : register(b1) { column_major float4x4 model; float4 tint; };\n"
                "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\nstruct VsOut { float4 pos : SV_Position; float4 col : COLOR; };\n"
                "VsOut main(VsIn i) { VsOut o; o.pos = mul(mvp, mul(model, float4(i.pos, 1))); float3 n = mul((float3x3)model, i.nrm); float l = saturate(dot(normalize(n), normalize(float3(0.5, 0.8, 0.6)))) * 0.8 + 0.2; o.col = float4(tint.rgb * l, 1); return o; }\n";
            Pso builder(kit, s->stage.canvas.binder.signature);
            builder.vs(vs).ps("struct VsOut { float4 pos : SV_Position; float4 col : COLOR; };\nfloat4 main(VsOut i) : SV_Target { return i.col; }\n").targets({DXGI_FORMAT_R8G8B8A8_UNORM}).depth(DXGI_FORMAT_D32_FLOAT);
            s->state = builder.build(error);
            return s->state != nullptr;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            for (UINT k = 0; k < 8; ++k) {
                const float a = static_cast<float>(frame) * 0.35f + static_cast<float>(k) * 0.785f;
                const rr::Mat4 model = rr::Mat4::translate(0.9f * std::cos(a), 0.9f * std::sin(a), 0.0f) * rr::Mat4::rotate_y(a * 2.0f) * rr::Mat4::scale(0.28f, 0.28f, 0.28f);
                std::memcpy(s->ring.at((frame % 3) * 8 + k), model.m, 64);
                const float tint[4] = {0.3f + 0.08f * static_cast<float>(k), 0.9f - 0.1f * static_cast<float>(k), 0.4f + 0.05f * static_cast<float>(frame % 7), 1};
                std::memcpy(s->ring.at((frame % 3) * 8 + k, 64), tint, 16);
            }
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                s->stage.canvas.begin(list, clear);
                list->SetPipelineState(s->state);
                const rr::Mat4 view = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -3.0f);
                list->SetGraphicsRoot32BitConstants(0, 16, view.m, 0);
                s->stage.sphere.bind(list);
                for (UINT k = 0; k < 8; ++k) {
                    list->SetGraphicsRootConstantBufferView(1, s->ring.address((frame % 3) * 8 + k));
                    s->stage.sphere.draw(list);
                }
            }, error);
        };
        steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
    });
}

struct TaaState {
    Stage stage;
    Surface history[2];
    Surface current;
    ID3D12PipelineState* draw = nullptr;
    ID3D12PipelineState* resolve = nullptr;
};

void register_taa() {
    Cases::frames(rr::CaseSpec("scene", "taa_jittered_history_resolve_8_frames").size(128, 128).frames(8, {0, 3, 7}).tolerance(rr::Tolerance::blend(6).with_edge(1)).tag("scene"), [](Cases::Steps& steps) {
        auto s = std::make_shared<TaaState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!s->stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT) || !s->history[0].color(kit, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, error) || !s->history[1].color(kit, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, error)) {
                return false;
            }
            Binder& binder = s->stage.canvas.binder;
            binder.srv_texture(0, s->stage.canvas.color.resource, DXGI_FORMAT_R8G8B8A8_UNORM);
            binder.srv_texture(1, s->history[0].resource, DXGI_FORMAT_R8G8B8A8_UNORM);
            binder.srv_texture(2, s->history[1].resource, DXGI_FORMAT_R8G8B8A8_UNORM);
            Pso mesh = s->stage.pso();
            const char* resolve =
                "Texture2D<float4> cur : register(t0); Texture2D<float4> ha : register(t1); Texture2D<float4> hb : register(t2);\n"
                "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { int2 p = int2(pos.xy); float4 c = cur.Load(int3(p, 0)); float4 h = p1.x < 0.5 ? ha.Load(int3(p, 0)) : hb.Load(int3(p, 0));\n"
                "  float4 lo = c, hi = c; for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) { float4 n = cur.Load(int3(clamp(p + int2(x, y), 0, 127), 0)); lo = min(lo, n); hi = max(hi, n); }\n"
                "  h = clamp(h, lo, hi); return p1.y > 0.5 ? c : lerp(c, h, 0.85); }\n";
            Pso resolve_pso(kit, binder.signature);
            resolve_pso.vs(Hlsl12::kFullVs).ps(Hlsl12::with_root(resolve)).layout({}).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
            s->draw = mesh.build(error);
            s->resolve = resolve_pso.build(error);
            if (s->draw == nullptr || s->resolve == nullptr) {
                return false;
            }
            return kit.run([&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0, 0, 0, 1};
                for (Surface& h : s->history) {
                    h.bind_target(list);
                    list->ClearRenderTargetView(h.view, clear, 0, nullptr);
                }
            }, error);
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            static const float halton[8][2] = {{0.5f, 0.333f}, {0.25f, 0.667f}, {0.75f, 0.111f}, {0.125f, 0.444f}, {0.625f, 0.778f}, {0.375f, 0.222f}, {0.875f, 0.556f}, {0.0625f, 0.889f}};
            const float jx = (halton[frame][0] - 0.5f) * 2.0f / 128.0f;
            const float jy = (halton[frame][1] - 0.5f) * 2.0f / 128.0f;
            Surface& previous = s->history[frame % 2];
            Surface& output = s->history[(frame + 1) % 2];
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                s->stage.canvas.begin(list, clear);
                list->SetPipelineState(s->draw);
                rr::Mat4 jitter;
                jitter.m[12] = jx;
                jitter.m[13] = jy;
                const rr::Mat4 projection = jitter * rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -3.0f);
                const float color[4] = {0.9f, 0.5f, 0.2f, 1};
                set_cbv_root(list, projection * rr::Mat4::rotate_x(0.5f) * rr::Mat4::rotate_y(0.15f * static_cast<float>(frame)), color);
                s->stage.torus.bind(list);
                s->stage.torus.draw(list);
                const float blue[4] = {0.3f, 0.5f, 0.95f, 1};
                set_cbv_root(list, projection * rr::Mat4::translate(0.15f * static_cast<float>(frame) - 0.5f, -0.2f, 0.2f) * rr::Mat4::scale(0.5f, 0.5f, 0.5f), blue);
                s->stage.cube.bind(list);
                s->stage.cube.draw(list);
                s->stage.canvas.color.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                previous.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                output.bind_target(list);
                s->stage.canvas.binder.bind_graphics(list);
                const float zero[4] = {0, 0, 0, 0};
                const float params[4] = {(frame % 2) != 0 ? 1.0f : 0.0f, frame == 0 ? 1.0f : 0.0f, 0, 0};
                set_cbv_root(list, rr::Mat4::identity(), zero, params);
                draw_fullscreen(list, s->resolve);
            }, error);
        };
        steps.on_capture = [s](Kit& kit, unsigned frame, rr::CaptureSet& out, std::string& error) {
            Surface& output = s->history[(frame + 1) % 2];
            rr::Image image;
            if (!kit.read_image(output.resource, output.state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
                return false;
            }
            out.add("", std::move(image));
            return true;
        };
    });
}

const struct Registrar {
    Registrar() {
        register_life();
        register_ring();
        register_taa();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
