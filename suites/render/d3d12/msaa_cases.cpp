#include "render/d3d12/textures.hpp"

namespace lsw::suites::render::d3d12 {

namespace {

std::string needs_samples(Kit& kit, UINT count) {
    for (DXGI_FORMAT format : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_TYPELESS}) {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{};
        levels.Format = format;
        levels.SampleCount = count;
        if (FAILED(kit.device()->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof levels)) || levels.NumQualityLevels == 0) {
            return std::to_string(count) + "x multisampling";
        }
    }
    return {};
}

bool resolve_to(Kit& kit, ID3D12GraphicsCommandList* list, Surface& source, Surface& destination) {
    source.to(list, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    destination.to(list, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    list->ResolveSubresource(destination.resource, 0, source.resource, 0, destination.format);
    (void)kit;
    return true;
}

struct MsaaScene {
    Stage stage;
    Surface resolved;

    bool open(Kit& kit, UINT samples, std::string& error, bool with_depth = true) {
        return stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, with_depth ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_UNKNOWN, 128, samples) &&
               resolved.color(kit, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, error);
    }

    void draw_shapes(ID3D12GraphicsCommandList* list, ID3D12PipelineState* state) {
        list->SetPipelineState(state);
        stage.mesh(list, stage.sphere, rr::Mat4::translate(-0.35f, 0.1f, 0), 0.9f, 0.5f, 0.2f);
        stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.1f) * rr::Mat4::translate(0.2f, 0, 0), 0.2f, 0.8f, 0.5f);
        stage.mesh(list, stage.cube, rr::Mat4::translate(0.45f, -0.4f, 0.2f) * rr::Mat4::rotate_y(0.6f) * rr::Mat4::rotate_x(0.3f) * rr::Mat4::scale(0.3f, 0.3f, 0.3f), 0.3f, 0.4f, 0.95f);
    }

    bool resolve_read(Kit& kit, rr::CaptureSet& out, std::string& error, const std::function<void(ID3D12GraphicsCommandList*)>& record) {
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                record(list);
                resolve_to(kit, list, stage.canvas.color, resolved);
            }, error)) {
            return false;
        }
        rr::Image image;
        if (!kit.read_image(resolved.resource, resolved.state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
            return false;
        }
        out.add("", std::move(image));
        return true;
    }
};

rr::Tolerance msaa_tolerance() {
    rr::Tolerance t = rr::Tolerance::blend(6).with_edge(1);
    t.name = "msaa(6,edge1)";
    return t;
}

void register_resolve() {
    for (UINT samples : {2u, 4u, 8u}) {
        Cases::single(
            rr::CaseSpec("msaa", "resolve_color_depth_" + std::to_string(samples) + "x").tolerance(msaa_tolerance()).tag("msaa"),
            [samples](Kit& kit, rr::CaptureSet& out, std::string& error) {
                MsaaScene scene;
                if (!scene.open(kit, samples, error)) {
                    return false;
                }
                Pso builder = scene.stage.pso();
                ID3D12PipelineState* state = builder.build(error);
                if (state == nullptr) {
                    return false;
                }
                return scene.resolve_read(kit, out, error, [&](ID3D12GraphicsCommandList* list) {
                    const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                    scene.stage.canvas.begin(list, clear);
                    scene.stage.backdrop(list);
                    scene.draw_shapes(list, state);
                });
            },
            [samples](Kit& kit) { return needs_samples(kit, samples); });
    }
    Cases::single(
        rr::CaseSpec("msaa", "per_sample_shading_sv_sampleindex_4x").tolerance(msaa_tolerance()).tag("msaa"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            MsaaScene scene;
            if (!scene.open(kit, 4, error)) {
                return false;
            }
            const char* ps =
                "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                "float4 main(VsOut i, uint sample : SV_SampleIndex) : SV_Target { float l = saturate(dot(normalize(i.nrm), normalize(float3(0.5, 0.8, 0.6)))) * 0.8 + 0.2;\n"
                "  float3 tint = sample == 0 ? float3(1, 0.5, 0.5) : (sample == 1 ? float3(0.5, 1, 0.5) : (sample == 2 ? float3(0.5, 0.5, 1) : float3(1, 1, 0.5))); return float4(p0.rgb * l * tint, 1); }\n";
            Pso builder = scene.stage.pso(ps);
            ID3D12PipelineState* state = builder.build(error);
            if (state == nullptr) {
                return false;
            }
            return scene.resolve_read(kit, out, error, [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                scene.stage.canvas.begin(list, clear);
                scene.draw_shapes(list, state);
            });
        },
        [](Kit& kit) { return needs_samples(kit, 4); });
    for (int centroid = 0; centroid < 2; ++centroid) {
        Cases::single(
            rr::CaseSpec("msaa", centroid != 0 ? "centroid_interpolation_4x" : "center_interpolation_4x").tolerance(msaa_tolerance()).tag("msaa"),
            [centroid](Kit& kit, rr::CaptureSet& out, std::string& error) {
                MsaaScene scene;
                if (!scene.open(kit, 4, error)) {
                    return false;
                }
                const std::string ps = std::string("struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; ") + (centroid != 0 ? "centroid " : "") +
                                       "float2 uv : TEXCOORD0; };\nfloat4 main(VsOut i) : SV_Target { float2 g = frac(i.uv * float2(6, 8)); float v = (g.x > 0.5) != (g.y > 0.5) ? 1.0 : 0.15; return float4(v * float3(0.9, 0.7, 0.3), 1); }\n";
                Pso builder = scene.stage.pso(ps.c_str());
                ID3D12PipelineState* state = builder.build(error);
                if (state == nullptr) {
                    return false;
                }
                return scene.resolve_read(kit, out, error, [&](ID3D12GraphicsCommandList* list) {
                    const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                    scene.stage.canvas.begin(list, clear);
                    scene.draw_shapes(list, state);
                });
            },
            [](Kit& kit) { return needs_samples(kit, 4); });
    }
    Cases::single(
        rr::CaseSpec("msaa", "alpha_to_coverage_4x").tolerance(rr::Tolerance::psnr(30).with_edge(1)).tag("msaa"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            MsaaScene scene;
            if (!scene.open(kit, 4, error, false)) {
                return false;
            }
            const char* ps =
                "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                "float4 main(VsOut i) : SV_Target { return float4(p0.rgb, saturate(i.uv.x * 1.2 - 0.1)); }\n";
            Pso builder = scene.stage.pso(ps);
            builder.desc.BlendState.AlphaToCoverageEnable = TRUE;
            ID3D12PipelineState* state = builder.build(error);
            if (state == nullptr) {
                return false;
            }
            return scene.resolve_read(kit, out, error, [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                scene.stage.canvas.begin(list, clear);
                scene.stage.backdrop(list);
                list->SetPipelineState(state);
                scene.stage.mesh(list, scene.stage.plane, rr::Mat4::rotate_z(0.2f) * rr::Mat4::scale(0.8f, 0.8f, 0.8f), 0.9f, 0.6f, 0.2f);
            });
        },
        [](Kit& kit) { return needs_samples(kit, 4); });
    Cases::single(
        rr::CaseSpec("msaa", "sample_mask_two_of_four_4x").tolerance(msaa_tolerance()).tag("msaa"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            MsaaScene scene;
            if (!scene.open(kit, 4, error)) {
                return false;
            }
            Pso builder = scene.stage.pso();
            builder.desc.SampleMask = 0x5;
            ID3D12PipelineState* state = builder.build(error);
            if (state == nullptr) {
                return false;
            }
            return scene.resolve_read(kit, out, error, [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                scene.stage.canvas.begin(list, clear);
                scene.draw_shapes(list, state);
            });
        },
        [](Kit& kit) { return needs_samples(kit, 4); });
}

void register_shader_access() {
    Cases::single(
        rr::CaseSpec("msaa", "texture2dms_load_each_sample_4x").tolerance(msaa_tolerance()).tag("msaa"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            MsaaScene scene;
            if (!scene.open(kit, 4, error)) {
                return false;
            }
            Pso builder = scene.stage.pso();
            ID3D12PipelineState* draw_state = builder.build(error);
            const char* ps =
                "Texture2DMS<float4, 4> src : register(t0);\n"
                "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { int2 p = int2(pos.xy) % 64 * 2; uint s = (uint)(pos.x / 64) + 2 * (uint)(pos.y / 64); return float4(src.Load(p, s).rgb, 1); }\n";
            Pso reader(kit, scene.stage.canvas.binder.signature);
            reader.vs(Hlsl12::kFullVs).ps(ps).layout({}).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
            ID3D12PipelineState* read_state = reader.build(error);
            if (draw_state == nullptr || read_state == nullptr) {
                return false;
            }
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            scene.stage.canvas.binder.srv(0, scene.stage.canvas.color.resource, &srv);
            if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                    const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                    scene.stage.canvas.begin(list, clear);
                    scene.draw_shapes(list, draw_state);
                    scene.stage.canvas.color.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    scene.resolved.bind_target(list);
                    list->SetPipelineState(read_state);
                    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    list->DrawInstanced(3, 1, 0, 0);
                }, error)) {
                return false;
            }
            rr::Image image;
            if (!kit.read_image(scene.resolved.resource, scene.resolved.state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
                return false;
            }
            out.add("", std::move(image));
            return true;
        },
        [](Kit& kit) { return needs_samples(kit, 4); });
    Cases::single(
        rr::CaseSpec("msaa", "texture2dms_depth_load_average_4x").tolerance(rr::Tolerance::blend(6).with_edge(1)).tag("msaa"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            MsaaScene scene;
            if (!scene.open(kit, 4, error)) {
                return false;
            }
            Pso builder = scene.stage.pso();
            ID3D12PipelineState* draw_state = builder.build(error);
            const char* ps =
                "Texture2DMS<float, 4> depth : register(t0);\n"
                "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { float lo = 1, hi = 0, sum = 0; for (uint s = 0; s < 4; ++s) { float d = depth.Load(int2(pos.xy), s); lo = min(lo, d); hi = max(hi, d); sum += d; } return float4(pow(lo, 10.0), pow(hi, 10.0), pow(sum * 0.25, 10.0), 1); }\n";
            Pso reader(kit, scene.stage.canvas.binder.signature);
            reader.vs(Hlsl12::kFullVs).ps(ps).layout({}).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
            ID3D12PipelineState* read_state = reader.build(error);
            if (draw_state == nullptr || read_state == nullptr) {
                return false;
            }
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = DXGI_FORMAT_R32_FLOAT;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            scene.stage.canvas.binder.srv(0, scene.stage.canvas.depth.resource, &srv);
            if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                    const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                    scene.stage.canvas.begin(list, clear);
                    scene.draw_shapes(list, draw_state);
                    scene.stage.canvas.depth.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    scene.resolved.bind_target(list);
                    list->SetPipelineState(read_state);
                    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    list->DrawInstanced(3, 1, 0, 0);
                }, error)) {
                return false;
            }
            rr::Image image;
            if (!kit.read_image(scene.resolved.resource, scene.resolved.state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
                return false;
            }
            out.add("", std::move(image));
            return true;
        },
        [](Kit& kit) { return needs_samples(kit, 4); });
    Cases::single(
        rr::CaseSpec("msaa", "resolve_subresource_region_offset_rect_4x").tolerance(msaa_tolerance()).tag("msaa"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            MsaaScene scene;
            if (!scene.open(kit, 4, error)) {
                return false;
            }
            Pso builder = scene.stage.pso();
            ID3D12PipelineState* state = builder.build(error);
            if (state == nullptr) {
                return false;
            }
            bool supported = true;
            if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                    const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                    scene.stage.canvas.begin(list, clear);
                    scene.draw_shapes(list, state);
                    scene.stage.canvas.color.to(list, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
                    scene.resolved.to(list, D3D12_RESOURCE_STATE_RESOLVE_DEST);
                    ID3D12GraphicsCommandList1* list1 = nullptr;
                    if (FAILED(list->QueryInterface(__uuidof(ID3D12GraphicsCommandList1), reinterpret_cast<void**>(&list1)))) {
                        supported = false;
                        return;
                    }
                    D3D12_RECT rect{16, 24, 100, 90};
                    list1->ResolveSubresourceRegion(scene.resolved.resource, 0, 8, 4, scene.stage.canvas.color.resource, 0, &rect, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_AVERAGE);
                    list1->Release();
                }, error)) {
                return false;
            }
            if (!supported) {
                return fail_with(error, "ID3D12GraphicsCommandList1 is not available");
            }
            rr::Image image;
            if (!kit.read_image(scene.resolved.resource, scene.resolved.state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
                return false;
            }
            out.add("", std::move(image));
            return true;
        },
        [](Kit& kit) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS2 options{};
            const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS2, options);
            if (FAILED(result) || options.ProgrammableSamplePositionsTier == D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_NOT_SUPPORTED) {
                return std::string("ResolveSubresourceRegion (programmable sample positions)");
            }
            return needs_samples(kit, 4);
        });
}

const struct Registrar {
    Registrar() {
        register_resolve();
        register_shader_access();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
