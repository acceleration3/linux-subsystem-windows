#include "render/d3d12/gfx.hpp"
#include <array>

namespace lsw::suites::render::d3d12 {

namespace {

struct Factor {
    const char* name;
    D3D12_BLEND value;
};

const Factor kSourceFactors[] = {
    {"zero", D3D12_BLEND_ZERO},
    {"one", D3D12_BLEND_ONE},
    {"src_color", D3D12_BLEND_SRC_COLOR},
    {"inv_src_color", D3D12_BLEND_INV_SRC_COLOR},
    {"src_alpha", D3D12_BLEND_SRC_ALPHA},
    {"inv_src_alpha", D3D12_BLEND_INV_SRC_ALPHA},
    {"dest_alpha", D3D12_BLEND_DEST_ALPHA},
    {"inv_dest_alpha", D3D12_BLEND_INV_DEST_ALPHA},
    {"dest_color", D3D12_BLEND_DEST_COLOR},
    {"inv_dest_color", D3D12_BLEND_INV_DEST_COLOR},
    {"src_alpha_sat", D3D12_BLEND_SRC_ALPHA_SAT},
    {"blend_factor", D3D12_BLEND_BLEND_FACTOR},
    {"inv_blend_factor", D3D12_BLEND_INV_BLEND_FACTOR},
};

const Factor kDestFactors[] = {
    {"zero", D3D12_BLEND_ZERO},
    {"src_color", D3D12_BLEND_SRC_COLOR},
    {"inv_src_color", D3D12_BLEND_INV_SRC_COLOR},
    {"src_alpha", D3D12_BLEND_SRC_ALPHA},
    {"inv_src_alpha", D3D12_BLEND_INV_SRC_ALPHA},
    {"dest_alpha", D3D12_BLEND_DEST_ALPHA},
    {"dest_color", D3D12_BLEND_DEST_COLOR},
    {"blend_factor", D3D12_BLEND_BLEND_FACTOR},
};

struct BlendSetup {
    D3D12_BLEND src = D3D12_BLEND_SRC_ALPHA;
    D3D12_BLEND dst = D3D12_BLEND_INV_SRC_ALPHA;
    D3D12_BLEND_OP op = D3D12_BLEND_OP_ADD;
    D3D12_BLEND src_alpha = D3D12_BLEND_ONE;
    D3D12_BLEND dst_alpha = D3D12_BLEND_INV_SRC_ALPHA;
    D3D12_BLEND_OP op_alpha = D3D12_BLEND_OP_ADD;
    UINT8 mask = D3D12_COLOR_WRITE_ENABLE_ALL;
};

bool draw_blended(Kit& kit, const BlendSetup& setup, rr::CaptureSet& out, std::string& error) {
    Stage stage;
    if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
        return false;
    }
    Pso builder = stage.pso();
    D3D12_RENDER_TARGET_BLEND_DESC& blend = builder.desc.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = setup.src;
    blend.DestBlend = setup.dst;
    blend.BlendOp = setup.op;
    blend.SrcBlendAlpha = setup.src_alpha;
    blend.DestBlendAlpha = setup.dst_alpha;
    blend.BlendOpAlpha = setup.op_alpha;
    blend.RenderTargetWriteMask = setup.mask;
    ID3D12PipelineState* state = builder.build(error);
    if (state == nullptr) {
        return false;
    }
    const bool ok = kit.run(
        [&](ID3D12GraphicsCommandList* list) {
            const float clear[4] = {0.1f, 0.2f, 0.3f, 0.5f};
            stage.canvas.begin(list, clear);
            stage.backdrop(list);
            list->SetPipelineState(state);
            const float factor[4] = {0.75f, 0.4f, 0.2f, 0.6f};
            list->OMSetBlendFactor(factor);
            stage.mesh(list, stage.sphere, rr::Mat4::translate(-0.25f, 0.1f, 0.0f), 0.9f, 0.35f, 0.2f, 0.6f);
            stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.1f) * rr::Mat4::translate(0.2f, 0.0f, 0.2f), 0.2f, 0.8f, 0.45f, 0.5f);
            stage.mesh(list, stage.cube, rr::Mat4::translate(0.35f, -0.4f, 0.3f) * rr::Mat4::rotate_y(0.7f) * rr::Mat4::scale(0.3f, 0.3f, 0.3f), 0.3f,
                       0.4f, 0.95f, 0.8f);
        },
        error);
    return ok && stage.finish(out, error);
}

const char* kDualPs =
    "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
    "struct Out { float4 c0 : SV_Target0; float4 c1 : SV_Target1; };\n"
    "Out main(VsOut i) { Out o; float l = saturate(dot(normalize(i.nrm), normalize(float3(0.5, 0.8, 0.6)))) * 0.8 + 0.2;\n"
    "  o.c0 = float4(p0.rgb * l, p0.a); o.c1 = float4(0.9, 0.5, 0.2, 1.0) * p1; return o; }\n";

void register_blend() {
    for (const Factor& f : kSourceFactors) {
        Cases::single(rr::CaseSpec("om", std::string("blend_src_") + f.name).tolerance(rr::Tolerance::blend(5)).tag("blend"),
                      [value = f.value](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          BlendSetup setup;
                          setup.src = value;
                          setup.dst = value == D3D12_BLEND_ZERO ? D3D12_BLEND_SRC_COLOR : D3D12_BLEND_ONE;
                          return draw_blended(kit, setup, out, error);
                      });
    }
    for (const Factor& f : kDestFactors) {
        Cases::single(rr::CaseSpec("om", std::string("blend_dst_") + f.name).tolerance(rr::Tolerance::blend(5)).tag("blend"),
                      [value = f.value](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          BlendSetup setup;
                          setup.src = D3D12_BLEND_SRC_ALPHA;
                          setup.dst = value;
                          return draw_blended(kit, setup, out, error);
                      });
    }
    struct Op {
        const char* name;
        D3D12_BLEND_OP value;
    };
    const Op ops[] = {{"subtract", D3D12_BLEND_OP_SUBTRACT},
                      {"rev_subtract", D3D12_BLEND_OP_REV_SUBTRACT},
                      {"min", D3D12_BLEND_OP_MIN},
                      {"max", D3D12_BLEND_OP_MAX}};
    for (const Op& op : ops) {
        Cases::single(rr::CaseSpec("om", std::string("blend_op_") + op.name).tolerance(rr::Tolerance::blend(3)).tag("blend"),
                      [value = op.value](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          BlendSetup setup;
                          setup.src = D3D12_BLEND_ONE;
                          setup.dst = D3D12_BLEND_ONE;
                          setup.op = value;
                          setup.op_alpha = value;
                          return draw_blended(kit, setup, out, error);
                      });
        Cases::single(rr::CaseSpec("om", std::string("blend_alpha_op_") + op.name).tolerance(rr::Tolerance::blend(3)).tag("blend"),
                      [value = op.value](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          BlendSetup setup;
                          setup.src_alpha = D3D12_BLEND_ONE;
                          setup.dst_alpha = D3D12_BLEND_ONE;
                          setup.op_alpha = value;
                          return draw_blended(kit, setup, out, error);
                      });
    }
    struct Mask {
        const char* name;
        UINT8 bits;
    };
    const Mask masks[] = {{"r", 1}, {"g", 2}, {"b", 4}, {"a", 8}, {"rg", 3}, {"gba", 14}, {"rb", 5}};
    for (const Mask& mask : masks) {
        Cases::single(rr::CaseSpec("om", std::string("write_mask_") + mask.name).tolerance(rr::Tolerance::blend(2)).tag("blend"),
                      [bits = mask.bits](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          BlendSetup setup;
                          setup.mask = bits;
                          return draw_blended(kit, setup, out, error);
                      });
    }
    Cases::single(rr::CaseSpec("om", "blend_disabled_overwrite").tolerance(rr::Tolerance::blend(2)).tag("blend"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) {
                      Stage stage;
                      if (!stage.open(kit, error)) {
                          return false;
                      }
                      Pso builder = stage.pso();
                      ID3D12PipelineState* state = builder.build(error);
                      if (state == nullptr) {
                          return false;
                      }
                      const bool ok = kit.run(
                          [&](ID3D12GraphicsCommandList* list) {
                              const float clear[4] = {0.1f, 0.2f, 0.3f, 0.5f};
                              stage.canvas.begin(list, clear);
                              stage.backdrop(list);
                              list->SetPipelineState(state);
                              stage.mesh(list, stage.sphere, rr::Mat4::translate(-0.2f, 0, 0), 0.9f, 0.35f, 0.2f, 0.3f);
                              stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.0f), 0.2f, 0.8f, 0.45f, 0.3f);
                          },
                          error);
                      return ok && stage.finish(out, error);
                  });
    struct Dual {
        const char* name;
        D3D12_BLEND src;
        D3D12_BLEND dst;
    };
    const Dual duals[] = {{"src1_color", D3D12_BLEND_SRC1_COLOR, D3D12_BLEND_INV_SRC1_COLOR},
                          {"src1_alpha", D3D12_BLEND_SRC1_ALPHA, D3D12_BLEND_INV_SRC1_ALPHA},
                          {"src_and_src1", D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC1_COLOR}};
    for (const Dual& dual : duals) {
        Cases::single(rr::CaseSpec("om", std::string("blend_dual_") + dual.name).tolerance(rr::Tolerance::blend(2)).tag("blend"),
                      [dual](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          Stage stage;
                          if (!stage.open(kit, error)) {
                              return false;
                          }
                          Pso builder = stage.pso(kDualPs);
                          D3D12_RENDER_TARGET_BLEND_DESC& blend = builder.desc.BlendState.RenderTarget[0];
                          blend.BlendEnable = TRUE;
                          blend.SrcBlend = dual.src;
                          blend.DestBlend = dual.dst;
                          blend.SrcBlendAlpha = D3D12_BLEND_ONE;
                          blend.DestBlendAlpha = D3D12_BLEND_ZERO;
                          ID3D12PipelineState* state = builder.build(error);
                          if (state == nullptr) {
                              return false;
                          }
                          const bool ok = kit.run(
                              [&](ID3D12GraphicsCommandList* list) {
                                  const float clear[4] = {0, 0, 0, 1};
                                  stage.canvas.begin(list, clear);
                                  stage.backdrop(list);
                                  list->SetPipelineState(state);
                                  const float p1[4] = {1, 1, 1, 1};
                                  const float p0[4] = {0.9f, 0.3f, 0.2f, 0.6f};
                                  set_cbv_root(list, stage.view_projection(), p0, p1);
                                  stage.sphere.bind(list);
                                  stage.sphere.draw(list);
                                  const float p0b[4] = {0.2f, 0.7f, 0.9f, 0.4f};
                                  set_cbv_root(list, stage.view_projection() * rr::Mat4::rotate_x(1.0f), p0b, p1);
                                  stage.torus.bind(list);
                                  stage.torus.draw(list);
                              },
                              error);
                          return ok && stage.finish(out, error);
                      });
    }
    Cases::single(rr::CaseSpec("om", "blend_independent_mrt8").tolerance(rr::Tolerance::blend(3)).tag("blend"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) {
                      Canvas canvas;
                      Surface targets[8];
                      if (!canvas.binder.create(kit, error)) {
                          return false;
                      }
                      for (Surface& target : targets) {
                          if (!target.color(kit, 32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, error)) {
                              return false;
                          }
                      }
                      const std::string pixel = Hlsl12::with_root(
                          "struct Out { float4 c[8] : SV_Target; };\n"
                          "Out main(float4 pos : SV_Position, float2 uv : TEXCOORD0) { Out o; [unroll] for (int k = 0; k < 8; ++k) o.c[k] = float4(uv.x, uv.y, k / 7.0, 0.5); return o; }\n");
                      Pso builder(kit, canvas.binder.signature);
                      builder.vs(Hlsl12::kFullVs).ps(pixel).layout({}).targets({DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM,
                                                                       DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM,
                                                                       DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM});
                      builder.desc.BlendState.IndependentBlendEnable = TRUE;
                      const D3D12_BLEND src[8] = {D3D12_BLEND_ONE, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_SRC_ALPHA_SAT, D3D12_BLEND_DEST_COLOR,
                                                  D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_SRC_COLOR, D3D12_BLEND_ONE};
                      const D3D12_BLEND_OP op[8] = {D3D12_BLEND_OP_ADD, D3D12_BLEND_OP_ADD, D3D12_BLEND_OP_ADD, D3D12_BLEND_OP_ADD,
                                                    D3D12_BLEND_OP_MAX, D3D12_BLEND_OP_REV_SUBTRACT, D3D12_BLEND_OP_ADD, D3D12_BLEND_OP_SUBTRACT};
                      for (int k = 0; k < 8; ++k) {
                          D3D12_RENDER_TARGET_BLEND_DESC& blend = builder.desc.BlendState.RenderTarget[k];
                          blend.BlendEnable = k != 3;
                          blend.SrcBlend = src[k];
                          blend.DestBlend = D3D12_BLEND_ONE;
                          blend.BlendOp = op[k];
                          blend.SrcBlendAlpha = D3D12_BLEND_ONE;
                          blend.DestBlendAlpha = D3D12_BLEND_ONE;
                          blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                          blend.RenderTargetWriteMask = k == 6 ? 0x7 : 0xf;
                      }
                      ID3D12PipelineState* state = builder.build(error);
                      if (state == nullptr) {
                          return false;
                      }
                      const bool ok = kit.run(
                          [&](ID3D12GraphicsCommandList* list) {
                              D3D12_CPU_DESCRIPTOR_HANDLE views[8];
                              for (int k = 0; k < 8; ++k) {
                                  views[k] = targets[k].view;
                                  const float clear[4] = {0.2f * static_cast<float>(k % 3), 0.3f, 0.1f * static_cast<float>(k), 0.25f};
                                  list->ClearRenderTargetView(targets[k].view, clear, 0, nullptr);
                              }
                              list->OMSetRenderTargets(8, views, FALSE, nullptr);
                              const D3D12_VIEWPORT vp = viewport_of(32, 32);
                              const D3D12_RECT scissor = rect_of(32, 32);
                              list->RSSetViewports(1, &vp);
                              list->RSSetScissorRects(1, &scissor);
                              canvas.binder.bind_graphics(list);
                              list->SetPipelineState(state);
                              list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                              list->DrawInstanced(3, 1, 0, 0);
                          },
                          error);
                      if (!ok) {
                          return false;
                      }
                      for (int k = 0; k < 8; ++k) {
                          rr::Image image;
                          if (!kit.read_image(targets[k].resource, targets[k].state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
                              return false;
                          }
                          out.add("rt" + std::to_string(k), std::move(image));
                      }
                      return true;
                  });
    struct Logic {
        const char* name;
        D3D12_LOGIC_OP value;
    };
    const Logic logics[] = {{"clear", D3D12_LOGIC_OP_CLEAR}, {"and", D3D12_LOGIC_OP_AND}, {"xor", D3D12_LOGIC_OP_XOR},
                            {"or", D3D12_LOGIC_OP_OR},       {"invert", D3D12_LOGIC_OP_INVERT}, {"equiv", D3D12_LOGIC_OP_EQUIV},
                            {"copy_inverted", D3D12_LOGIC_OP_COPY_INVERTED}, {"nand", D3D12_LOGIC_OP_NAND}};
    for (const Logic& logic : logics) {
        Cases::single(
            rr::CaseSpec("om", std::string("logic_op_") + logic.name).tolerance(rr::Tolerance::exact()).tag("blend"),
            [logic](Kit& kit, rr::CaptureSet& out, std::string& error) {
                Canvas canvas;
                if (!canvas.open(kit, 64, 64, DXGI_FORMAT_R8G8B8A8_UINT, error, DXGI_FORMAT_D32_FLOAT)) {
                    return false;
                }
                Geometry sphere;
                Geometry torus;
                if (!sphere.create(kit, Shapes::sphere(16, 24, 0.8f), true, error) || !torus.create(kit, Shapes::torus(24, 12, 0.9f, 0.3f), true, error)) {
                    return false;
                }
                const char* uint_ps =
                    "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                    "uint4 main(VsOut i) : SV_Target { float l = saturate(dot(normalize(i.nrm), float3(0.5, 0.8, 0.6))) * 0.8 + 0.2;\n"
                    "  return uint4(saturate(p0.rgb * l) * 255.0, 255); }\n";
                const char* uint_backdrop =
                    "uint4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
                    "  uint ch = ((uint)(pos.x / 8) + (uint)(pos.y / 8)) & 1;\n"
                    "  return uint4(lerp(50, 200, uv.x) * (0.6 + 0.4 * ch), lerp(90, 180, uv.y), 120, 255); }\n";
                Pso backdrop(kit, canvas.binder.signature);
                backdrop.vs(Hlsl12::kFullVs).ps(uint_backdrop).layout({}).targets({DXGI_FORMAT_R8G8B8A8_UINT});
                backdrop.desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
                Pso shapes(kit, canvas.binder.signature);
                shapes.vs(Hlsl12::with_root(Hlsl12::kLitVs)).ps(Hlsl12::with_root(uint_ps)).targets({DXGI_FORMAT_R8G8B8A8_UINT}).depth(DXGI_FORMAT_D32_FLOAT);
                D3D12_RENDER_TARGET_BLEND_DESC& blend = shapes.desc.BlendState.RenderTarget[0];
                blend.BlendEnable = FALSE;
                blend.LogicOpEnable = TRUE;
                blend.LogicOp = logic.value;
                ID3D12PipelineState* state_backdrop = backdrop.build(error);
                ID3D12PipelineState* state = shapes.build(error);
                if (state == nullptr || state_backdrop == nullptr) {
                    return false;
                }
                const bool ok = kit.run(
                    [&](ID3D12GraphicsCommandList* list) {
                        const float clear[4] = {10, 20, 30, 255};
                        canvas.begin(list, clear);
                        list->SetPipelineState(state_backdrop);
                        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                        list->DrawInstanced(3, 1, 0, 0);
                        list->SetPipelineState(state);
                        const rr::Mat4 base = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -3.0f);
                        const float a[4] = {0.9f, 0.35f, 0.2f, 1.0f};
                        set_cbv_root(list, base * rr::Mat4::translate(-0.2f, 0, 0), a);
                        sphere.bind(list);
                        sphere.draw(list);
                        const float b[4] = {0.2f, 0.8f, 0.45f, 1.0f};
                        set_cbv_root(list, base * rr::Mat4::rotate_x(1.0f), b);
                        torus.bind(list);
                        torus.draw(list);
                    },
                    error);
                rr::Image image;
                if (!ok || !canvas.read(kit, image, error)) {
                    return false;
                }
                out.add("", std::move(image));
                return true;
            },
            [](Kit& kit) {
                D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
                const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS, options);
                return SUCCEEDED(result) && options.OutputMergerLogicOp ? std::string() : std::string("logic ops (OutputMergerLogicOp)");
            });
    }
}

struct CompareFunc {
    const char* name;
    D3D12_COMPARISON_FUNC value;
};

const CompareFunc kFuncs[] = {{"never", D3D12_COMPARISON_FUNC_NEVER},
                              {"less", D3D12_COMPARISON_FUNC_LESS},
                              {"equal", D3D12_COMPARISON_FUNC_EQUAL},
                              {"less_equal", D3D12_COMPARISON_FUNC_LESS_EQUAL},
                              {"greater", D3D12_COMPARISON_FUNC_GREATER},
                              {"not_equal", D3D12_COMPARISON_FUNC_NOT_EQUAL},
                              {"greater_equal", D3D12_COMPARISON_FUNC_GREATER_EQUAL},
                              {"always", D3D12_COMPARISON_FUNC_ALWAYS}};

bool draw_depth_pair(Kit& kit, DXGI_FORMAT depth_format, D3D12_COMPARISON_FUNC func, float clear_depth, rr::CaptureSet& out, std::string& error) {
    Stage stage;
    if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, depth_format)) {
        return false;
    }
    Pso first = stage.pso();
    first.desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    Pso second = stage.pso();
    second.desc.DepthStencilState.DepthFunc = func;
    ID3D12PipelineState* state_first = first.build(error);
    ID3D12PipelineState* state_second = second.build(error);
    if (state_first == nullptr || state_second == nullptr) {
        return false;
    }
    const bool ok = kit.run(
        [&](ID3D12GraphicsCommandList* list) {
            const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
            stage.canvas.begin(list, clear, true, clear_depth);
            list->SetPipelineState(state_first);
            stage.mesh(list, stage.sphere, rr::Mat4::translate(-0.1f, 0.0f, 0.0f), 0.9f, 0.4f, 0.2f);
            list->SetPipelineState(state_second);
            stage.mesh(list, stage.sphere, rr::Mat4::translate(-0.1f, 0.0f, 0.0f), 0.95f, 0.9f, 0.2f);
            stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.2f) * rr::Mat4::translate(0.1f, 0.0f, 0.0f), 0.2f, 0.8f, 0.5f);
            stage.mesh(list, stage.cube, rr::Mat4::translate(0.3f, -0.3f, 0.0f) * rr::Mat4::rotate_y(0.8f) * rr::Mat4::scale(0.35f, 0.35f, 0.9f), 0.3f,
                       0.4f, 0.95f);
        },
        error);
    return ok && stage.finish(out, error);
}

bool draw_depth_strips(Kit& kit, DXGI_FORMAT depth_format, D3D12_COMPARISON_FUNC func, bool precision, rr::CaptureSet& out, std::string& error) {
    Stage stage;
    if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, depth_format)) {
        return false;
    }
    Pso base = stage.pso();
    Pso test = stage.pso();
    test.desc.DepthStencilState.DepthFunc = func;
    test.desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    ID3D12PipelineState* state_base = base.build(error);
    ID3D12PipelineState* state_test = test.build(error);
    if (state_base == nullptr || state_test == nullptr) {
        return false;
    }
    const bool ok = kit.run(
        [&](ID3D12GraphicsCommandList* list) {
            const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
            stage.canvas.begin(list, clear);
            const float offsets[3] = {0.0f, precision ? 0.0001f : 0.02f, precision ? -0.0001f : -0.02f};
            for (int k = 0; k < 3; ++k) {
                const float x = -0.75f + 0.75f * static_cast<float>(k);
                const rr::Mat4 strip = rr::Mat4::translate(x, 0.0f, 0.0f) * rr::Mat4::scale(0.3f, 0.8f, 1.0f);
                list->SetPipelineState(state_base);
                stage.mesh(list, stage.plane, strip, 0.2f, 0.3f, 0.8f);
                list->SetPipelineState(state_test);
                stage.mesh(list, stage.plane, rr::Mat4::translate(x, 0.0f, offsets[k]) * rr::Mat4::scale(0.3f, 0.8f, 1.0f) * rr::Mat4::scale(0.8f, 0.8f, 1.0f), 0.95f, 0.85f, 0.2f);
            }
        },
        error);
    return ok && stage.finish(out, error);
}

void register_depth() {
    for (const CompareFunc& f : kFuncs) {
        Cases::single(rr::CaseSpec("om", std::string("depth_func_") + f.name).tolerance(rr::Tolerance::blend(2)).tag("depth"),
                      [f](Kit& kit, rr::CaptureSet& out, std::string& error) { return draw_depth_strips(kit, DXGI_FORMAT_D32_FLOAT, f.value, false, out, error); });
    }
    struct Format {
        const char* name;
        DXGI_FORMAT value;
    };
    const Format formats[] = {{"d16", DXGI_FORMAT_D16_UNORM}, {"d24s8", DXGI_FORMAT_D24_UNORM_S8_UINT}, {"d32", DXGI_FORMAT_D32_FLOAT}};
    for (const Format& format : formats) {
        Cases::single(rr::CaseSpec("om", std::string("depth_precision_less_") + format.name).tolerance(rr::Tolerance::blend(2)).tag("depth"),
                      [format](Kit& kit, rr::CaptureSet& out, std::string& error) { return draw_depth_strips(kit, format.value, D3D12_COMPARISON_FUNC_LESS, true, out, error); });
    }
    Cases::single(rr::CaseSpec("om", "depth_clear_zero_greater").tolerance(rr::Tolerance::blend(2)).tag("depth"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) {
                      return draw_depth_pair(kit, DXGI_FORMAT_D32_FLOAT, D3D12_COMPARISON_FUNC_GREATER, 0.0f, out, error);
                  });
    Cases::single(rr::CaseSpec("om", "depth_write_mask_zero").tolerance(rr::Tolerance::blend(2)).tag("depth"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) {
                      Stage stage;
                      if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
                          return false;
                      }
                      Pso first = stage.pso();
                      first.desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
                      Pso second = stage.pso();
                      ID3D12PipelineState* state_first = first.build(error);
                      ID3D12PipelineState* state_second = second.build(error);
                      if (state_first == nullptr || state_second == nullptr) {
                          return false;
                      }
                      const bool ok = kit.run(
                          [&](ID3D12GraphicsCommandList* list) {
                              const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                              stage.canvas.begin(list, clear);
                              list->SetPipelineState(state_first);
                              stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.2f), 0.9f, 0.4f, 0.2f);
                              list->SetPipelineState(state_second);
                              stage.mesh(list, stage.sphere, rr::Mat4::translate(0.0f, 0.0f, -0.5f), 0.2f, 0.8f, 0.5f);
                          },
                          error);
                      return ok && stage.finish(out, error);
                  });
    Cases::single(rr::CaseSpec("om", "depth_disabled_painters_order").tolerance(rr::Tolerance::blend(2)).tag("depth"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) {
                      Stage stage;
                      if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
                          return false;
                      }
                      Pso builder = stage.pso();
                      builder.desc.DepthStencilState.DepthEnable = FALSE;
                      ID3D12PipelineState* state = builder.build(error);
                      if (state == nullptr) {
                          return false;
                      }
                      const bool ok = kit.run(
                          [&](ID3D12GraphicsCommandList* list) {
                              const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                              stage.canvas.begin(list, clear);
                              list->SetPipelineState(state);
                              stage.mesh(list, stage.sphere, rr::Mat4::translate(0.0f, 0.0f, -0.5f), 0.2f, 0.8f, 0.5f);
                              stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.2f), 0.9f, 0.4f, 0.2f);
                          },
                          error);
                      return ok && stage.finish(out, error);
                  });
}

struct StencilOp {
    const char* name;
    D3D12_STENCIL_OP value;
};

const StencilOp kStencilOps[] = {{"keep", D3D12_STENCIL_OP_KEEP},     {"zero", D3D12_STENCIL_OP_ZERO},     {"replace", D3D12_STENCIL_OP_REPLACE},
                                 {"incr_sat", D3D12_STENCIL_OP_INCR_SAT}, {"decr_sat", D3D12_STENCIL_OP_DECR_SAT}, {"invert", D3D12_STENCIL_OP_INVERT},
                                 {"incr", D3D12_STENCIL_OP_INCR},     {"decr", D3D12_STENCIL_OP_DECR}};

struct StencilCase {
    D3D12_STENCIL_OP pass = D3D12_STENCIL_OP_KEEP;
    D3D12_STENCIL_OP fail = D3D12_STENCIL_OP_KEEP;
    D3D12_STENCIL_OP depth_fail = D3D12_STENCIL_OP_KEEP;
    D3D12_COMPARISON_FUNC func = D3D12_COMPARISON_FUNC_ALWAYS;
    UINT8 initial = 0x55;
    UINT8 refs[3] = {0x58, 0x59, 0x57};
    UINT8 read_mask = 0xff;
    UINT8 write_mask = 0xff;
    bool occluder = false;
    int pre[3] = {-1, -1, -1};
};

const UINT8 kProbes[10] = {0x00, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0xA5, 0xAA, 0xFF};

bool draw_stencil_case(Kit& kit, const StencilCase& c, rr::CaptureSet& out, std::string& error) {
    Stage stage;
    if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT)) {
        return false;
    }
    Pso occluder = stage.pso();
    occluder.desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
    occluder.desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    Pso pre = stage.pso();
    pre.desc.DepthStencilState.StencilEnable = TRUE;
    pre.desc.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_REPLACE, D3D12_COMPARISON_FUNC_ALWAYS};
    pre.desc.DepthStencilState.BackFace = pre.desc.DepthStencilState.FrontFace;
    pre.desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pre.desc.RasterizerState.FrontCounterClockwise = TRUE;
    pre.desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pre.desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
    ID3D12PipelineState* state_pre = pre.build(error);
    Pso writer = stage.pso();
    writer.desc.DepthStencilState.StencilEnable = TRUE;
    writer.desc.DepthStencilState.StencilReadMask = c.read_mask;
    writer.desc.DepthStencilState.StencilWriteMask = c.write_mask;
    writer.desc.DepthStencilState.FrontFace = {c.fail, c.depth_fail, c.pass, c.func};
    writer.desc.DepthStencilState.BackFace = writer.desc.DepthStencilState.FrontFace;
    writer.desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    writer.desc.RasterizerState.FrontCounterClockwise = TRUE;
    writer.desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    writer.desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
    Pso tester = stage.pso();
    tester.desc.DepthStencilState.DepthEnable = FALSE;
    tester.desc.DepthStencilState.StencilEnable = TRUE;
    tester.desc.DepthStencilState.StencilReadMask = 0xff;
    tester.desc.DepthStencilState.StencilWriteMask = 0;
    tester.desc.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_EQUAL};
    tester.desc.DepthStencilState.BackFace = tester.desc.DepthStencilState.FrontFace;
    ID3D12PipelineState* state_occluder = occluder.build(error);
    ID3D12PipelineState* state_writer = writer.build(error);
    ID3D12PipelineState* state_tester = tester.build(error);
    if (state_pre == nullptr || state_occluder == nullptr || state_writer == nullptr || state_tester == nullptr) {
        return false;
    }
    const bool ok = kit.run(
        [&](ID3D12GraphicsCommandList* list) {
            const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
            stage.canvas.begin(list, clear);
            list->ClearDepthStencilView(stage.canvas.depth.view, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, c.initial, 0, nullptr);
            if (c.pre[0] >= 0) {
                list->SetPipelineState(state_pre);
                const rr::Mat4 models[3] = {rr::Mat4::translate(-0.45f, 0.1f, 0.0f) * rr::Mat4::scale(0.7f, 0.7f, 0.7f), rr::Mat4::translate(0.45f, 0.1f, 0.0f) * rr::Mat4::rotate_x(1.0f) * rr::Mat4::scale(0.6f, 0.6f, 0.6f), rr::Mat4::translate(0.0f, -0.55f, 0.0f) * rr::Mat4::rotate_y(0.6f) * rr::Mat4::scale(0.22f, 0.22f, 0.22f)};
                const Geometry* geometries[3] = {&stage.sphere, &stage.torus, &stage.cube};
                for (int k = 0; k < 3; ++k) {
                    list->OMSetStencilRef(static_cast<UINT>(c.pre[k]));
                    stage.mesh(list, *geometries[k], models[k], 1, 1, 1);
                }
            }
            if (c.occluder) {
                list->SetPipelineState(state_occluder);
                stage.mesh(list, stage.plane, rr::Mat4::translate(-1.1f, 0.0f, 0.5f), 1, 1, 1);
            }
            list->SetPipelineState(state_writer);
            list->OMSetStencilRef(c.refs[0]);
            stage.mesh(list, stage.sphere, rr::Mat4::translate(-0.45f, 0.1f, 0.0f) * rr::Mat4::scale(0.7f, 0.7f, 0.7f), 1, 1, 1);
            list->OMSetStencilRef(c.refs[1]);
            stage.mesh(list, stage.torus, rr::Mat4::translate(0.45f, 0.1f, 0.0f) * rr::Mat4::rotate_x(1.0f) * rr::Mat4::scale(0.6f, 0.6f, 0.6f), 1, 1, 1);
            list->OMSetStencilRef(c.refs[2]);
            stage.mesh(list, stage.cube, rr::Mat4::translate(0.0f, -0.55f, 0.0f) * rr::Mat4::rotate_y(0.6f) * rr::Mat4::scale(0.22f, 0.22f, 0.22f), 1, 1, 1);
            list->SetPipelineState(state_tester);
            static const float palette[10][3] = {{0.9f, 0.2f, 0.2f}, {0.9f, 0.6f, 0.2f}, {0.2f, 0.25f, 0.3f}, {0.9f, 0.9f, 0.2f}, {0.3f, 0.9f, 0.3f},
                                                 {0.2f, 0.9f, 0.8f}, {0.2f, 0.5f, 0.95f}, {0.6f, 0.3f, 0.95f}, {0.95f, 0.3f, 0.8f}, {1.0f, 1.0f, 1.0f}};
            for (UINT k = 0; k < 10; ++k) {
                list->OMSetStencilRef(kProbes[k]);
                set_cbv_root(list, rr::Mat4::identity(), std::array<float, 4>{palette[k][0], palette[k][1], palette[k][2], 1.0f}.data());
                stage.plane.bind(list);
                stage.plane.draw(list);
            }
        },
        error);
    return ok && stage.finish(out, error);
}

void add_stencil(const std::string& name, const StencilCase& c) {
    Cases::single(rr::CaseSpec("om", name).tolerance(rr::Tolerance::blend(2)).tag("stencil"), [c](Kit& kit, rr::CaptureSet& out, std::string& error) { return draw_stencil_case(kit, c, out, error); });
}

void register_stencil() {
    for (const StencilOp& op : kStencilOps) {
        StencilCase c;
        c.pass = op.value;
        const std::string n = op.name;
        if (n == "keep") {
            continue;
        }
        if (n == "incr_sat" || n == "incr") {
            c.initial = 0xFF;
        } else if (n == "decr_sat" || n == "decr") {
            c.initial = 0x00;
        }
        if (n == "keep") {
            c.pre[0] = 0x56; c.pre[1] = 0x57; c.pre[2] = 0x58;
        } else if (n == "incr_sat") {
            c.pre[0] = 0xFE; c.pre[1] = 0xFF; c.pre[2] = 0x10;
        } else if (n == "decr_sat") {
            c.pre[0] = 0x01; c.pre[1] = 0x00; c.pre[2] = 0x10;
        }
        add_stencil(std::string("stencil_pass_op_") + op.name, c);
    }
    for (int k : {1, 2, 5, 6}) {
        StencilCase c;
        c.fail = kStencilOps[k].value;
        c.func = D3D12_COMPARISON_FUNC_EQUAL;
        c.refs[0] = 0x55;
        add_stencil(std::string("stencil_fail_op_") + kStencilOps[k].name, c);
        StencilCase d;
        d.depth_fail = kStencilOps[k].value;
        d.occluder = true;
        add_stencil(std::string("stencil_depth_fail_op_") + kStencilOps[k].name, d);
    }
    for (const CompareFunc& f : kFuncs) {
        StencilCase c;
        c.func = f.value;
        c.pass = D3D12_STENCIL_OP_INCR;
        c.refs[0] = 0x54;
        c.refs[1] = 0x56;
        c.refs[2] = 0x55;
        if (std::string(f.name) == "never") {
            c.pre[0] = 0x56; c.pre[1] = 0x57; c.pre[2] = 0x58;
        }
        add_stencil(std::string("stencil_func_") + f.name, c);
    }
    StencilCase masks;
    masks.pass = D3D12_STENCIL_OP_REPLACE;
    masks.func = D3D12_COMPARISON_FUNC_EQUAL;
    masks.read_mask = 0x0f;
    masks.write_mask = 0xf0;
    masks.refs[0] = 0xA5;
    masks.refs[1] = 0xA6;
    masks.refs[2] = 0x55;
    add_stencil("stencil_masks_partial", masks);
}

void register_two_sided() {
    Cases::single(rr::CaseSpec("om", "stencil_two_sided_counting").tolerance(rr::Tolerance::blend(2)).tag("stencil"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT)) {
            return false;
        }
        Pso occluder = stage.pso();
        occluder.desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
        Pso counter = stage.pso();
        counter.desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        counter.desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        counter.desc.DepthStencilState.StencilEnable = TRUE;
        counter.desc.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_INCR, D3D12_COMPARISON_FUNC_ALWAYS};
        counter.desc.DepthStencilState.BackFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_DECR, D3D12_COMPARISON_FUNC_ALWAYS};
        counter.desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
        Pso shade = stage.pso();
        shade.desc.DepthStencilState.DepthEnable = FALSE;
        shade.desc.DepthStencilState.StencilEnable = TRUE;
        shade.desc.DepthStencilState.StencilWriteMask = 0;
        shade.desc.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_NOT_EQUAL};
        shade.desc.DepthStencilState.BackFace = shade.desc.DepthStencilState.FrontFace;
        ID3D12PipelineState* state_occluder = occluder.build(error);
        ID3D12PipelineState* state_counter = counter.build(error);
        ID3D12PipelineState* state_shade = shade.build(error);
        if (state_occluder == nullptr || state_counter == nullptr || state_shade == nullptr) {
            return false;
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                stage.canvas.begin(list, clear);
                stage.backdrop(list);
                list->SetPipelineState(state_occluder);
                stage.mesh(list, stage.plane, rr::Mat4::scale(1.0f, 0.5f, 1.0f) * rr::Mat4::translate(0.0f, 0.0f, 0.0f), 1, 1, 1);
                list->SetPipelineState(state_counter);
                list->OMSetStencilRef(0);
                stage.mesh(list, stage.sphere, rr::Mat4::translate(-0.2f, 0.1f, 0.0f), 1, 1, 1);
                stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.2f) * rr::Mat4::translate(0.3f, 0.0f, 0.0f), 1, 1, 1);
                list->SetPipelineState(state_shade);
                list->OMSetStencilRef(0);
                stage.mesh(list, stage.plane, rr::Mat4::identity(), 0.95f, 0.8f, 0.1f);
            },
            error);
        return ok && stage.finish(out, error);
    });
}

struct AccumState {
    Stage stage;
    ID3D12PipelineState* state = nullptr;
};

void add_blend_frames(const std::string& name, const BlendSetup& setup, const rr::Tolerance& tolerance) {
    Cases::frames(rr::CaseSpec("om", name + "_accumulate").frames(4, {1, 3}).tolerance(tolerance).tag("blend"), [setup](Cases::Steps& steps) {
        auto s = std::make_shared<AccumState>();
        steps.on_setup = [s, setup](Kit& kit, std::string& error) {
            if (!s->stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
                return false;
            }
            Pso builder = s->stage.pso();
            D3D12_RENDER_TARGET_BLEND_DESC& blend = builder.desc.BlendState.RenderTarget[0];
            blend.BlendEnable = TRUE;
            blend.SrcBlend = setup.src;
            blend.DestBlend = setup.dst;
            blend.BlendOp = setup.op;
            blend.SrcBlendAlpha = setup.src_alpha;
            blend.DestBlendAlpha = setup.dst_alpha;
            blend.BlendOpAlpha = setup.op_alpha;
            blend.RenderTargetWriteMask = setup.mask;
            s->state = builder.build(error);
            return s->state != nullptr;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.1f, 0.2f, 0.3f, 0.5f};
                if (frame == 0) {
                    s->stage.canvas.begin(list, clear);
                    s->stage.backdrop(list);
                } else {
                    s->stage.canvas.begin_keep(list);
                    list->ClearDepthStencilView(s->stage.canvas.depth.view, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                }
                list->SetPipelineState(s->state);
                const float factor[4] = {0.75f, 0.4f, 0.2f, 0.6f};
                list->OMSetBlendFactor(factor);
                const float shift = -0.45f + 0.3f * static_cast<float>(frame);
                s->stage.mesh(list, s->stage.sphere, rr::Mat4::translate(shift, 0.15f - 0.1f * static_cast<float>(frame), 0.0f) * rr::Mat4::scale(0.7f, 0.7f, 0.7f), 0.9f, 0.35f + 0.1f * static_cast<float>(frame), 0.2f, 0.6f);
                s->stage.mesh(list, s->stage.torus, rr::Mat4::rotate_x(0.5f + 0.3f * static_cast<float>(frame)) * rr::Mat4::scale(0.7f, 0.7f, 0.7f), 0.2f, 0.8f, 0.45f, 0.5f);
            }, error);
        };
        steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
    });
}

void register_om_frames() {
    BlendSetup dest_color;
    dest_color.dst = D3D12_BLEND_DEST_COLOR;
    add_blend_frames("blend_dst_dest_color", dest_color, rr::Tolerance::blend(8).with_outliers(0.001));
    BlendSetup inv_dest;
    inv_dest.src = D3D12_BLEND_INV_DEST_COLOR;
    inv_dest.dst = D3D12_BLEND_ONE;
    add_blend_frames("blend_src_inv_dest_color", inv_dest, rr::Tolerance::blend(8).with_outliers(0.001));
    BlendSetup reverse;
    reverse.src = D3D12_BLEND_ONE;
    reverse.dst = D3D12_BLEND_ONE;
    reverse.op = D3D12_BLEND_OP_REV_SUBTRACT;
    reverse.op_alpha = D3D12_BLEND_OP_REV_SUBTRACT;
    add_blend_frames("blend_op_rev_subtract", reverse, rr::Tolerance::blend(3));
    BlendSetup mask;
    mask.mask = 5;
    add_blend_frames("write_mask_rb", mask, rr::Tolerance::blend(3));
    Cases::frames(rr::CaseSpec("om", "stencil_pass_op_decr_persistent_stencil").frames(4, {1, 3}).tolerance(rr::Tolerance::blend(2)).tag("stencil"), [](Cases::Steps& steps) {
        auto s = std::make_shared<AccumState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!s->stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT)) {
                return false;
            }
            Pso writer = s->stage.pso();
            writer.desc.DepthStencilState.StencilEnable = TRUE;
            writer.desc.DepthStencilState.DepthEnable = FALSE;
            writer.desc.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_DECR, D3D12_COMPARISON_FUNC_ALWAYS};
            writer.desc.DepthStencilState.BackFace = writer.desc.DepthStencilState.FrontFace;
            writer.desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
            writer.desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
            writer.desc.RasterizerState.FrontCounterClockwise = TRUE;
            Pso tester = s->stage.pso();
            tester.desc.DepthStencilState.DepthEnable = FALSE;
            tester.desc.DepthStencilState.StencilEnable = TRUE;
            tester.desc.DepthStencilState.StencilWriteMask = 0;
            tester.desc.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_EQUAL};
            tester.desc.DepthStencilState.BackFace = tester.desc.DepthStencilState.FrontFace;
            s->state = writer.build(error);
            ID3D12PipelineState* tester_state = tester.build(error);
            if (s->state == nullptr || tester_state == nullptr) {
                return false;
            }
            s->stage.backdrop_pso = tester_state;
            return true;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                s->stage.canvas.begin_keep(list);
                list->ClearRenderTargetView(s->stage.canvas.color.view, clear, 0, nullptr);
                if (frame == 0) {
                    list->ClearDepthStencilView(s->stage.canvas.depth.view, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0x03, 0, nullptr);
                }
                list->SetPipelineState(s->state);
                s->stage.mesh(list, s->stage.sphere, rr::Mat4::translate(-0.4f + 0.25f * static_cast<float>(frame), 0.0f, 0.0f) * rr::Mat4::scale(0.8f, 0.8f, 0.8f), 1, 1, 1);
                list->SetPipelineState(s->stage.backdrop_pso);
                const float colors[5][4] = {{0.9f, 0.2f, 0.2f, 1}, {0.9f, 0.7f, 0.2f, 1}, {0.4f, 0.9f, 0.2f, 1}, {0.2f, 0.8f, 0.8f, 1}, {0.3f, 0.3f, 0.95f, 1}};
                for (UINT k = 0; k < 5; ++k) {
                    list->OMSetStencilRef(k);
                    set_cbv_root(list, rr::Mat4::identity(), colors[k]);
                    s->stage.plane.bind(list);
                    s->stage.plane.draw(list);
                }
            }, error);
        };
        steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
    });
    for (const CompareFunc& f : kFuncs) {
        if (std::string(f.name) != "less") {
            continue;
        }
        Cases::frames(rr::CaseSpec("om", std::string("depth_func_") + f.name + "_persistent_depth").frames(4, {1, 3}).tolerance(rr::Tolerance::blend(3).with_edge(1)).tag("depth"), [f](Cases::Steps& steps) {
            auto s = std::make_shared<AccumState>();
            steps.on_setup = [s, f](Kit& kit, std::string& error) {
                if (!s->stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
                    return false;
                }
                Pso builder = s->stage.pso();
                builder.desc.DepthStencilState.DepthFunc = f.value;
                s->state = builder.build(error);
                return s->state != nullptr;
            };
            steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
                return kit.submit([&](ID3D12GraphicsCommandList* list) {
                    const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                    s->stage.canvas.begin_keep(list);
                    list->ClearRenderTargetView(s->stage.canvas.color.view, clear, 0, nullptr);
                    if (frame == 0) {
                        list->ClearDepthStencilView(s->stage.canvas.depth.view, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                    }
                    list->SetPipelineState(s->state);
                    for (unsigned k = 0; k <= frame; ++k) {
                        const float z = 0.5f - 0.35f * static_cast<float>(k);
                        s->stage.mesh(list, s->stage.sphere, rr::Mat4::translate(-0.4f + 0.27f * static_cast<float>(k), 0.0f, z) * rr::Mat4::scale(0.8f, 0.8f, 0.8f), 0.9f - 0.2f * static_cast<float>(k),
                                      0.3f + 0.2f * static_cast<float>(k), 0.3f);
                    }
                }, error);
            };
            steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
        });
    }
}

const struct Registrar {
    Registrar() {
        register_blend();
        register_depth();
        register_stencil();
        register_two_sided();
        register_om_frames();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
