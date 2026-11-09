#include "render/d3d12/gfx.hpp"

#include <cmath>

namespace lsw::suites::render::d3d12 {

namespace {

const char* kFlatPs = "float4 main(float4 pos : SV_Position) : SV_Target { return p0; }\n";

bool run_shapes(Kit& kit, DXGI_FORMAT depth, const std::function<void(Pso&)>& tweak, const std::function<void(Stage&, ID3D12GraphicsCommandList*, ID3D12PipelineState*)>& body,
                rr::CaptureSet& out, std::string& error) {
    Stage stage;
    if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, depth)) {
        return false;
    }
    Pso builder = stage.pso();
    if (tweak) {
        tweak(builder);
    }
    ID3D12PipelineState* state = builder.build(error);
    if (state == nullptr) {
        return false;
    }
    const bool ok = kit.run([&](ID3D12GraphicsCommandList* list) { body(stage, list, state); }, error);
    return ok && stage.finish(out, error);
}

void register_fill_cull() {
    Cases::single(rr::CaseSpec("rs", "fill_solid").tolerance(rr::Tolerance::blend(2)).tag("raster"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        return run_shapes(kit, DXGI_FORMAT_D32_FLOAT, {}, [](Stage& s, ID3D12GraphicsCommandList* list, ID3D12PipelineState* state) {
            const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
            s.canvas.begin(list, clear);
            s.backdrop(list);
            list->SetPipelineState(state);
            s.mesh(list, s.torus, rr::Mat4::rotate_x(0.9f), 0.9f, 0.6f, 0.2f);
        }, out, error);
    });
    Cases::single(rr::CaseSpec("rs", "fill_wireframe").tolerance(rr::Tolerance::raster(2, 1)).tag("raster"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        return run_shapes(kit, DXGI_FORMAT_D32_FLOAT, [](Pso& p) { p.desc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME; },
                          [](Stage& s, ID3D12GraphicsCommandList* list, ID3D12PipelineState* state) {
                              const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                              s.canvas.begin(list, clear);
                              list->SetPipelineState(state);
                              s.mesh(list, s.torus, rr::Mat4::rotate_x(0.9f), 0.9f, 0.9f, 0.2f);
                          }, out, error);
    });
    struct Cull {
        const char* name;
        D3D12_CULL_MODE mode;
        BOOL ccw;
    };
    const Cull culls[] = {{"none", D3D12_CULL_MODE_NONE, TRUE}, {"back_ccw_front", D3D12_CULL_MODE_BACK, TRUE}, {"front_ccw_front", D3D12_CULL_MODE_FRONT, TRUE}};
    for (const Cull& cull : culls) {
        Cases::single(rr::CaseSpec("rs", std::string("cull_") + cull.name).tolerance(rr::Tolerance::blend(2)).tag("raster"),
                      [cull](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          return run_shapes(kit, DXGI_FORMAT_UNKNOWN,
                                            [cull](Pso& p) {
                                                p.desc.RasterizerState.CullMode = cull.mode;
                                                p.desc.RasterizerState.FrontCounterClockwise = cull.ccw;
                                            },
                                            [](Stage& s, ID3D12GraphicsCommandList* list, ID3D12PipelineState* state) {
                                                const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                                                s.canvas.begin(list, clear);
                                                s.backdrop(list);
                                                list->SetPipelineState(state);
                                                s.mesh(list, s.sphere, rr::Mat4::translate(-0.4f, 0.1f, 0.0f), 0.2f, 0.7f, 0.9f);
                                                s.mesh(list, s.cube, rr::Mat4::translate(0.45f, -0.2f, 0.0f) * rr::Mat4::rotate_y(0.6f) * rr::Mat4::rotate_x(0.4f) *
                                                                         rr::Mat4::scale(0.4f, 0.4f, 0.4f),
                                                       0.9f, 0.5f, 0.2f);
                                            },
                                            out, error);
                      });
    }
}

struct BiasCase {
    const char* name;
    DXGI_FORMAT format;
    INT bias;
    float slope;
    float clamp;
    float gap;
};

void register_bias() {
    const BiasCase cases[] = {{"constant_d32_wins", DXGI_FORMAT_D32_FLOAT, -16000, 0.0f, 0.0f, 3e-4f},
                              {"constant_d32_loses", DXGI_FORMAT_D32_FLOAT, -2000, 0.0f, 0.0f, 3e-4f},
                              {"constant_d32_loses_where_d24_wins", DXGI_FORMAT_D32_FLOAT, -16000, 0.0f, 0.0f, 7e-4f},
                              {"constant_d16_wins", DXGI_FORMAT_D16_UNORM, -80, 0.0f, 0.0f, 5e-4f},
                              {"constant_d16_loses", DXGI_FORMAT_D16_UNORM, -8, 0.0f, 0.0f, 5e-4f},
                              {"constant_d24_wins", DXGI_FORMAT_D24_UNORM_S8_UINT, -16000, 0.0f, 0.0f, 7e-4f},
                              {"constant_d24_loses", DXGI_FORMAT_D24_UNORM_S8_UINT, -2000, 0.0f, 0.0f, 3e-4f},
                              {"slope_d32_large", DXGI_FORMAT_D32_FLOAT, 0, -40.0f, 0.0f, 0.0f},
                              {"slope_d32_small", DXGI_FORMAT_D32_FLOAT, 0, -0.5f, 0.0f, 0.0f},
                              {"clamp_limits_large_bias_d32", DXGI_FORMAT_D32_FLOAT, -16000, 0.0f, -0.00001f, 0.0f}};
    int row = -1;
    for (const BiasCase& item : cases) {
        ++row;
        Cases::single(rr::CaseSpec("rs", std::string("depth_bias_") + item.name).tolerance(rr::Tolerance::blend(2)).tag("raster"),
                      [item, row](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          Stage stage;
                          if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, item.format)) {
                              return false;
                          }
                          Pso base = stage.pso();
                          Pso biased = stage.pso();
                          biased.desc.RasterizerState.DepthBias = item.bias;
                          biased.desc.RasterizerState.SlopeScaledDepthBias = item.slope;
                          biased.desc.RasterizerState.DepthBiasClamp = item.clamp;
                          ID3D12PipelineState* state_base = base.build(error);
                          ID3D12PipelineState* state_biased = biased.build(error);
                          if (state_base == nullptr || state_biased == nullptr) {
                              return false;
                          }
                          if (item.gap > 0.0f) {
                              const float hue = static_cast<float>(row) / 7.0f;
                              const float lower[4] = {0.2f + 0.5f * hue, 0.4f, 0.9f - 0.6f * hue, 1};
                              const float upper[4] = {0.95f - 0.4f * hue, 0.8f, 0.2f + 0.5f * hue, 1};
                              const bool flat_ok = kit.run(
                                  [&](ID3D12GraphicsCommandList* list) {
                                      const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                                      stage.canvas.begin(list, clear);
                                      list->SetPipelineState(state_base);
                                      set_cbv_root(list, rr::Mat4::translate(0.0f, 0.0f, 0.3f) * rr::Mat4::scale(0.7f, 0.7f, 1.0f), lower);
                                      stage.plane.bind(list);
                                      stage.plane.draw(list);
                                      list->SetPipelineState(state_biased);
                                      set_cbv_root(list, rr::Mat4::translate(0.0f, 0.0f, 0.3f + item.gap) * rr::Mat4::scale(0.4f, 0.4f, 1.0f), upper);
                                      stage.plane.bind(list);
                                      stage.plane.draw(list);
                                  },
                                  error);
                              return flat_ok && stage.finish(out, error);
                          }
                          const bool ok = kit.run(
                              [&](ID3D12GraphicsCommandList* list) {
                                  const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                                  stage.canvas.begin(list, clear);
                                  list->SetPipelineState(state_base);
                                  stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.0f), 0.2f, 0.4f, 0.9f);
                                  list->SetPipelineState(state_biased);
                                  stage.mesh(list, stage.torus, rr::Mat4::translate(0.0f, 0.0f, -0.01f) * rr::Mat4::rotate_x(1.0f), 0.95f, 0.8f, 0.2f);
                              },
                              error);
                          return ok && stage.finish(out, error);
                      });
    }
}

void register_clip_viewport() {
    for (int clip = 0; clip < 2; ++clip) {
        Cases::single(rr::CaseSpec("rs", clip != 0 ? "depth_clip_enabled" : "depth_clip_disabled").tolerance(rr::Tolerance::blend(2)).tag("raster"),
                      [clip](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          return run_shapes(kit, DXGI_FORMAT_D32_FLOAT, [clip](Pso& p) { p.desc.RasterizerState.DepthClipEnable = clip != 0; },
                                            [](Stage& s, ID3D12GraphicsCommandList* list, ID3D12PipelineState* state) {
                                                const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                                                s.canvas.begin(list, clear);
                                                s.backdrop(list);
                                                list->SetPipelineState(state);
                                                s.mesh(list, s.sphere, rr::Mat4::translate(0.0f, 0.0f, 1.9f), 0.9f, 0.6f, 0.2f);
                                                s.mesh(list, s.torus, rr::Mat4::translate(0.0f, 0.0f, -34.0f) * rr::Mat4::rotate_x(0.8f) * rr::Mat4::scale(14, 14, 14), 0.2f,
                                                       0.7f, 0.5f);
                                            },
                                            out, error);
                      });
    }
    Cases::single(rr::CaseSpec("rs", "scissor_rect").tolerance(rr::Tolerance::blend(2)).tag("raster"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        return run_shapes(kit, DXGI_FORMAT_D32_FLOAT, {}, [](Stage& s, ID3D12GraphicsCommandList* list, ID3D12PipelineState* state) {
            const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
            s.canvas.begin(list, clear);
            list->SetPipelineState(state);
            const D3D12_RECT scissor{20, 30, 100, 90};
            list->RSSetScissorRects(1, &scissor);
            s.mesh(list, s.torus, rr::Mat4::rotate_x(0.9f), 0.9f, 0.6f, 0.2f);
            const D3D12_RECT second{70, 10, 120, 50};
            list->RSSetScissorRects(1, &second);
            s.mesh(list, s.sphere, rr::Mat4::translate(0.3f, 0.5f, 0), 0.2f, 0.9f, 0.4f);
        }, out, error);
    });
    Cases::single(rr::CaseSpec("rs", "viewport_subrect").tolerance(rr::Tolerance::blend(2)).tag("raster"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        return run_shapes(kit, DXGI_FORMAT_D32_FLOAT, {}, [](Stage& s, ID3D12GraphicsCommandList* list, ID3D12PipelineState* state) {
            const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
            s.canvas.begin(list, clear);
            list->SetPipelineState(state);
            const D3D12_VIEWPORT quads[4] = {{0, 0, 64, 64, 0, 1}, {64, 0, 64, 64, 0, 1}, {0, 64, 64, 64, 0, 1}, {64, 64, 64, 64, 0, 1}};
            const float tints[4][3] = {{0.9f, 0.3f, 0.2f}, {0.2f, 0.8f, 0.3f}, {0.3f, 0.4f, 0.95f}, {0.9f, 0.85f, 0.2f}};
            for (int k = 0; k < 4; ++k) {
                list->RSSetViewports(1, &quads[k]);
                s.mesh(list, s.torus, rr::Mat4::rotate_x(0.4f + 0.3f * static_cast<float>(k)) * rr::Mat4::rotate_y(0.2f * static_cast<float>(k)), tints[k][0],
                       tints[k][1], tints[k][2]);
            }
        }, out, error);
    });
    Cases::single(rr::CaseSpec("rs", "viewport_depth_range").tolerance(rr::Tolerance::blend(2)).tag("raster"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
            return false;
        }
        Pso builder = stage.pso();
        ID3D12PipelineState* state = builder.build(error);
        if (state == nullptr) {
            return false;
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                stage.canvas.begin(list, clear);
                list->SetPipelineState(state);
                D3D12_VIEWPORT vp = viewport_of(128, 128);
                vp.MinDepth = 0.2f;
                vp.MaxDepth = 0.6f;
                list->RSSetViewports(1, &vp);
                stage.mesh(list, stage.torus, rr::Mat4::rotate_x(0.9f), 0.9f, 0.6f, 0.2f);
                stage.canvas.depth.to(list, D3D12_RESOURCE_STATE_COPY_SOURCE);
            },
            error);
        rr::Image depth;
        if (!ok || !stage.finish(out, error) || !stage.canvas.read_depth(kit, depth, error)) {
            return false;
        }
        out.add("depth", std::move(depth));
        return true;
    });
}

void register_lines_points() {
    struct Mode {
        const char* name;
        bool aa;
        bool strip;
    };
    const Mode modes[] = {{"aliased_list", false, false}, {"aliased_strip", false, true}, {"antialiased_list", true, false}};
    for (const Mode& mode : modes) {
        rr::CaseSpec line_spec("rs", std::string("lines_") + mode.name);
        line_spec.tolerance(mode.aa ? rr::Tolerance::psnr(8) : rr::Tolerance::raster(4, 1)).tag("raster");
        if (std::string(mode.name) == "aliased_list") {
            line_spec.same_as({"rs.lines_desc2_aliased"}, "the legacy rasterizer desc and the rasterizer desc 2 stream draw the same star of line segments in the aliased mode, so both must give one image");
        } else if (std::string(mode.name) == "antialiased_list") {
            line_spec.same_as({"rs.lines_desc2_alpha_antialiased"}, "AntialiasedLineEnable and the rasterizer desc 2 alpha antialiased mode draw the same star of line segments, so both must give one image");
        }
        Cases::single(line_spec,
                      [mode](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          Stage stage;
                          if (!stage.open(kit, error)) {
                              return false;
                          }
                          std::vector<Vertex> points;
                          for (int k = 0; k < 24; ++k) {
                              const float a = static_cast<float>(k) * 0.2618f;
                              const float r = (k & 1) != 0 ? 0.9f : 0.35f;
                              points.push_back({{r * std::cos(a), r * std::sin(a), 0.0f}, {0, 0, 1}, {0, 0}});
                          }
                          Geometry geometry;
                          if (!geometry.create_vertices(kit, points.data(), static_cast<UINT>(points.size()), sizeof(Vertex), error)) {
                              return false;
                          }
                          Pso builder(kit, stage.canvas.binder.signature);
                          builder.vs(Hlsl12::with_root(Hlsl12::kFlatVs)).ps(Hlsl12::with_root(kFlatPs)).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
                          builder.desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                          builder.desc.RasterizerState.AntialiasedLineEnable = mode.aa && !kit.broken("line_mode");
                          if (mode.aa) {
                              builder.desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
                              builder.desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
                              builder.desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                          }
                          ID3D12PipelineState* state = builder.build(error);
                          if (state == nullptr) {
                              return false;
                          }
                          const bool ok = kit.run(
                              [&](ID3D12GraphicsCommandList* list) {
                                  const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                                  stage.canvas.begin(list, clear);
                                  list->SetPipelineState(state);
                                  const float color[4] = {0.9f, 0.9f, 0.3f, 1.0f};
                                  set_cbv_root(list, rr::Mat4::identity(), color);
                                  geometry.bind(list, mode.strip ? D3D_PRIMITIVE_TOPOLOGY_LINESTRIP : D3D_PRIMITIVE_TOPOLOGY_LINELIST);
                                  geometry.draw_vertices(list);
                              },
                              error);
                          if (!ok || !stage.finish(out, error)) {
                              return false;
                          }
                          if (mode.aa) {
                              const std::string judgement = LineJudge::antialiased(out.items().back().second, points);
                              if (!judgement.empty()) {
                                  return fail_with(error, judgement);
                              }
                          }
                          return true;
                      });
    }
    Cases::single(rr::CaseSpec("rs", "point_list_grid").tolerance(rr::Tolerance::tight(1)).tag("raster"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error)) {
            return false;
        }
        std::vector<Vertex> points;
        for (int y = 0; y < 40; ++y) {
            for (int x = 0; x < 40; ++x) {
                points.push_back({{(static_cast<float>(x) + 0.5f) / 20.0f - 1.0f + 0.003f * static_cast<float>(y), (static_cast<float>(y) + 0.5f) / 20.0f - 1.0f, 0.0f}, {0, 0, 1}, {0, 0}});
            }
        }
        Geometry geometry;
        if (!geometry.create_vertices(kit, points.data(), static_cast<UINT>(points.size()), sizeof(Vertex), error)) {
            return false;
        }
        Pso builder(kit, stage.canvas.binder.signature);
        builder.vs(Hlsl12::with_root(Hlsl12::kFlatVs)).ps(Hlsl12::with_root(kFlatPs)).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
        builder.desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        ID3D12PipelineState* state = builder.build(error);
        if (state == nullptr) {
            return false;
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                stage.canvas.begin(list, clear);
                list->SetPipelineState(state);
                const float color[4] = {0.3f, 0.95f, 0.5f, 1.0f};
                set_cbv_root(list, rr::Mat4::identity(), color);
                geometry.bind(list, D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
                geometry.draw_vertices(list);
            },
            error);
        return ok && stage.finish(out, error);
    });
}

void register_fill_rules() {
    Cases::single(rr::CaseSpec("rs", "top_left_fan_additive").tolerance(rr::Tolerance::exact()).tag("raster"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error)) {
            return false;
        }
        std::vector<Vertex> fan;
        const int n = 9;
        for (int k = 0; k < n; ++k) {
            const float a0 = static_cast<float>(k) * 6.2831853f / n + 0.13f;
            const float a1 = static_cast<float>(k + 1) * 6.2831853f / n + 0.13f;
            fan.push_back({{0.07f, -0.03f, 0}, {0, 0, 1}, {0, 0}});
            fan.push_back({{4.0f * std::cos(a0), 4.0f * std::sin(a0), 0}, {0, 0, 1}, {0, 0}});
            fan.push_back({{4.0f * std::cos(a1), 4.0f * std::sin(a1), 0}, {0, 0, 1}, {0, 0}});
        }
        Geometry geometry;
        if (!geometry.create_vertices(kit, fan.data(), static_cast<UINT>(fan.size()), sizeof(Vertex), error)) {
            return false;
        }
        const char* ps = "float4 main(float4 pos : SV_Position) : SV_Target { return float4(pos.x / 256.0, pos.y / 256.0, 0.2, 0.0) + p0; }\n";
        Pso builder(kit, stage.canvas.binder.signature);
        builder.vs(Hlsl12::with_root(Hlsl12::kFlatVs)).ps(Hlsl12::with_root(ps)).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
        D3D12_RENDER_TARGET_BLEND_DESC& blend = builder.desc.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_ONE;
        blend.DestBlend = D3D12_BLEND_ONE;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_ONE;
        ID3D12PipelineState* state = builder.build(error);
        if (state == nullptr) {
            return false;
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0, 0, 0, 0};
                stage.canvas.begin(list, clear);
                list->SetPipelineState(state);
                const float color[4] = {0.0f, 0.0f, 0.1f, 0.0f};
                set_cbv_root(list, rr::Mat4::identity(), color);
                geometry.bind(list);
                geometry.draw_vertices(list);
            },
            error);
        return ok && stage.finish(out, error);
    });
    Cases::single(rr::CaseSpec("rs", "top_left_tile_strip_additive").tolerance(rr::Tolerance::exact()).tag("raster"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error)) {
            return false;
        }
        std::vector<Vertex> tiles;
        const int n = 7;
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                const float x0 = -1.0f + 2.0f * static_cast<float>(x) / n + 0.003f * static_cast<float>(x);
                const float x1 = -1.0f + 2.0f * static_cast<float>(x + 1) / n + 0.003f * static_cast<float>(x + 1);
                const float y0 = -1.0f + 2.0f * static_cast<float>(y) / n;
                const float y1 = -1.0f + 2.0f * static_cast<float>(y + 1) / n;
                const Vertex corner[4] = {{{x0, y0, 0}, {0, 0, 1}, {0, 0}}, {{x1, y0, 0}, {0, 0, 1}, {0, 0}}, {{x1, y1, 0}, {0, 0, 1}, {0, 0}}, {{x0, y1, 0}, {0, 0, 1}, {0, 0}}};
                const int order[6] = {0, 1, 2, 0, 2, 3};
                for (int k : order) {
                    tiles.push_back(corner[k]);
                }
            }
        }
        Geometry geometry;
        if (!geometry.create_vertices(kit, tiles.data(), static_cast<UINT>(tiles.size()), sizeof(Vertex), error)) {
            return false;
        }
        const char* ps = "float4 main(float4 pos : SV_Position) : SV_Target { return float4(pos.x / 512.0, pos.y / 512.0, 0.1, 0.0) + p0; }\n";
        Pso builder(kit, stage.canvas.binder.signature);
        builder.vs(Hlsl12::with_root(Hlsl12::kFlatVs)).ps(Hlsl12::with_root(ps)).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
        D3D12_RENDER_TARGET_BLEND_DESC& blend = builder.desc.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_ONE;
        blend.DestBlend = D3D12_BLEND_ONE;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_ONE;
        ID3D12PipelineState* state = builder.build(error);
        if (state == nullptr) {
            return false;
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0, 0, 0, 0};
                stage.canvas.begin(list, clear);
                list->SetPipelineState(state);
                const float color[4] = {0.0f, 0.0f, 0.1f, 0.0f};
                set_cbv_root(list, rr::Mat4::identity(), color);
                geometry.bind(list);
                geometry.draw_vertices(list);
            },
            error);
        return ok && stage.finish(out, error);
    });
}

void register_caps_gated() {
    Cases::single(
        rr::CaseSpec("rs", "conservative_raster_tier1").tolerance(rr::Tolerance::raster(2, 1)).tag("raster"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            return run_shapes(kit, DXGI_FORMAT_UNKNOWN, [](Pso& p) { p.desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON; },
                              [](Stage& s, ID3D12GraphicsCommandList* list, ID3D12PipelineState* state) {
                                  const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                                  s.canvas.begin(list, clear);
                                  list->SetPipelineState(state);
                                  s.mesh(list, s.torus, rr::Mat4::rotate_x(0.9f), 0.9f, 0.6f, 0.2f);
                              },
                              out, error);
        },
        [](Kit& kit) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
            const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS, options);
            return SUCCEEDED(result) && options.ConservativeRasterizationTier != D3D12_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED
                       ? std::string()
                       : std::string("conservative rasterization");
        });
    struct Rate {
        const char* name;
        D3D12_SHADING_RATE value;
    };
    const Rate rates[] = {{"2x2", D3D12_SHADING_RATE_2X2}, {"1x2", D3D12_SHADING_RATE_1X2}, {"2x1", D3D12_SHADING_RATE_2X1}};
    for (const Rate& rate : rates) {
        Cases::single(
            rr::CaseSpec("rs", std::string("vrs_per_draw_") + rate.name).tolerance(rr::Tolerance::filter(6, 0.02)).tag("raster"),
            [rate](Kit& kit, rr::CaptureSet& out, std::string& error) {
                return run_shapes(kit, DXGI_FORMAT_UNKNOWN, {}, [rate](Stage& s, ID3D12GraphicsCommandList* list, ID3D12PipelineState* state) {
                    const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                    s.canvas.begin(list, clear);
                    list->SetPipelineState(state);
                    ID3D12GraphicsCommandList5* list5 = nullptr;
                    if (SUCCEEDED(list->QueryInterface(__uuidof(ID3D12GraphicsCommandList5), reinterpret_cast<void**>(&list5)))) {
                        list5->RSSetShadingRate(rate.value, nullptr);
                        list5->Release();
                    }
                    s.mesh(list, s.torus, rr::Mat4::rotate_x(0.9f), 0.9f, 0.6f, 0.2f);
                }, out, error);
            },
            [](Kit& kit) {
                D3D12_FEATURE_DATA_D3D12_OPTIONS6 options{};
                const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS6, options);
                return SUCCEEDED(result) && options.VariableShadingRateTier != D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED
                           ? std::string()
                           : std::string("variable rate shading");
            });
    }
}

const struct Registrar {
    Registrar() {
        register_fill_cull();
        register_bias();
        register_clip_viewport();
        register_lines_points();
        register_fill_rules();
        register_caps_gated();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
