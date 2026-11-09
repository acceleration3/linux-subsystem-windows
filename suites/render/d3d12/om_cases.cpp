#include "render/d3d12/gfx.hpp"

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
    {"one", D3D12_BLEND_ONE},
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
    if (!stage.open(kit, error)) {
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
        Cases::single(rr::CaseSpec("om", std::string("blend_src_") + f.name).tolerance(std::string(f.name).find("color") != std::string::npos ? rr::Tolerance::blend(8) : rr::Tolerance::blend(3)).tag("blend"),
                      [value = f.value](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          BlendSetup setup;
                          setup.src = value;
                          setup.dst = D3D12_BLEND_ONE;
                          return draw_blended(kit, setup, out, error);
                      });
    }
    for (const Factor& f : kDestFactors) {
        Cases::single(rr::CaseSpec("om", std::string("blend_dst_") + f.name).tolerance(std::string(f.name).find("color") != std::string::npos ? rr::Tolerance::blend(8) : rr::Tolerance::blend(3)).tag("blend"),
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
    const Op ops[] = {{"add", D3D12_BLEND_OP_ADD},
                      {"subtract", D3D12_BLEND_OP_SUBTRACT},
                      {"rev_subtract", D3D12_BLEND_OP_REV_SUBTRACT},
                      {"min", D3D12_BLEND_OP_MIN},
                      {"max", D3D12_BLEND_OP_MAX}};
    for (const Op& op : ops) {
        Cases::single(rr::CaseSpec("om", std::string("blend_op_") + op.name).tolerance(rr::Tolerance::blend(2)).tag("blend"),
                      [value = op.value](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          BlendSetup setup;
                          setup.src = D3D12_BLEND_ONE;
                          setup.dst = D3D12_BLEND_ONE;
                          setup.op = value;
                          setup.op_alpha = value;
                          return draw_blended(kit, setup, out, error);
                      });
        Cases::single(rr::CaseSpec("om", std::string("blend_alpha_op_") + op.name).tolerance(rr::Tolerance::blend(2)).tag("blend"),
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
    Cases::single(rr::CaseSpec("om", "blend_independent_mrt8").tolerance(rr::Tolerance::blend(3)).uniform_ok("rt2").tag("blend"),
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
                      const D3D12_BLEND src[8] = {D3D12_BLEND_ONE, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_ZERO, D3D12_BLEND_DEST_COLOR,
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
                if (!canvas.open(kit, 64, 64, DXGI_FORMAT_R8G8B8A8_UINT, error)) {
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
                Pso shapes(kit, canvas.binder.signature);
                shapes.vs(Hlsl12::with_root(Hlsl12::kLitVs)).ps(Hlsl12::with_root(uint_ps)).targets({DXGI_FORMAT_R8G8B8A8_UINT});
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
            stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.2f) * rr::Mat4::translate(0.1f, 0.0f, 0.0f), 0.2f, 0.8f, 0.5f);
            stage.mesh(list, stage.cube, rr::Mat4::translate(0.3f, -0.3f, 0.0f) * rr::Mat4::rotate_y(0.8f) * rr::Mat4::scale(0.35f, 0.35f, 0.9f), 0.3f,
                       0.4f, 0.95f);
        },
        error);
    return ok && stage.finish(out, error);
}

void register_depth() {
    for (const CompareFunc& f : kFuncs) {
        Cases::single(rr::CaseSpec("om", std::string("depth_func_") + f.name).tolerance(rr::Tolerance::blend(2)).tag("depth"),
                      [f](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          return draw_depth_pair(kit, DXGI_FORMAT_D32_FLOAT, f.value, 1.0f, out, error);
                      });
    }
    struct Format {
        const char* name;
        DXGI_FORMAT value;
    };
    const Format formats[] = {{"d16", DXGI_FORMAT_D16_UNORM}, {"d24s8", DXGI_FORMAT_D24_UNORM_S8_UINT}, {"d32s8", DXGI_FORMAT_D32_FLOAT_S8X24_UINT}};
    for (const Format& format : formats) {
        Cases::single(rr::CaseSpec("om", std::string("depth_format_") + format.name).tolerance(rr::Tolerance::blend(2)).tag("depth"),
                      [format](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          return draw_depth_pair(kit, format.value, D3D12_COMPARISON_FUNC_LESS, 1.0f, out, error);
                      });
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

bool draw_stencil_mask(Kit& kit, DXGI_FORMAT depth_format, D3D12_STENCIL_OP pass_op, D3D12_STENCIL_OP fail_op, D3D12_STENCIL_OP depth_fail_op,
                       D3D12_COMPARISON_FUNC func, UINT8 reference, UINT8 read_mask, UINT8 write_mask, rr::CaptureSet& out, std::string& error) {
    Stage stage;
    if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, depth_format)) {
        return false;
    }
    Pso writer = stage.pso();
    writer.desc.DepthStencilState.StencilEnable = TRUE;
    writer.desc.DepthStencilState.StencilReadMask = read_mask;
    writer.desc.DepthStencilState.StencilWriteMask = write_mask;
    writer.desc.DepthStencilState.FrontFace = {fail_op, depth_fail_op, pass_op, func};
    writer.desc.DepthStencilState.BackFace = writer.desc.DepthStencilState.FrontFace;
    writer.desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
    Pso tester = stage.pso();
    tester.desc.DepthStencilState.DepthEnable = FALSE;
    tester.desc.DepthStencilState.StencilEnable = TRUE;
    tester.desc.DepthStencilState.StencilReadMask = 0xff;
    tester.desc.DepthStencilState.StencilWriteMask = 0;
    tester.desc.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_EQUAL};
    tester.desc.DepthStencilState.BackFace = tester.desc.DepthStencilState.FrontFace;
    ID3D12PipelineState* state_writer = writer.build(error);
    ID3D12PipelineState* state_tester = tester.build(error);
    if (state_writer == nullptr || state_tester == nullptr) {
        return false;
    }
    const bool ok = kit.run(
        [&](ID3D12GraphicsCommandList* list) {
            const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
            stage.canvas.begin(list, clear);
            list->ClearDepthStencilView(stage.canvas.depth.view, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0x55, 0, nullptr);
            stage.backdrop(list);
            list->SetPipelineState(state_writer);
            list->OMSetStencilRef(reference);
            stage.mesh(list, stage.sphere, rr::Mat4::translate(-0.2f, 0.0f, 0.0f), 1, 1, 1);
            list->OMSetStencilRef(static_cast<UINT>(reference) + 1);
            stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.2f) * rr::Mat4::translate(0.2f, 0.0f, 0.3f), 1, 1, 1);
            list->SetPipelineState(state_tester);
            const UINT8 probes[3] = {0x55, reference, static_cast<UINT8>(reference + 1)};
            for (int k = 0; k < 3; ++k) {
                list->OMSetStencilRef(probes[k]);
                const float colors[3][4] = {{0.9f, 0.2f, 0.2f, 1}, {0.2f, 0.9f, 0.2f, 1}, {0.2f, 0.2f, 0.9f, 1}};
                const D3D12_RECT bar{0, static_cast<LONG>(10 + 40 * k), 128, static_cast<LONG>(40 + 40 * k)};
                list->RSSetScissorRects(1, &bar);
                set_cbv_root(list, rr::Mat4::identity(), colors[k]);
                stage.plane.bind(list);
                stage.plane.draw(list);
            }
        },
        error);
    return ok && stage.finish(out, error);
}

void register_stencil() {
    for (const StencilOp& op : kStencilOps) {
        Cases::single(rr::CaseSpec("om", std::string("stencil_pass_op_") + op.name).tolerance(rr::Tolerance::blend(2)).tag("stencil"),
                      [op](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          return draw_stencil_mask(kit, DXGI_FORMAT_D24_UNORM_S8_UINT, op.value, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
                                                   D3D12_COMPARISON_FUNC_ALWAYS, 0x55, 0xff, 0xff, out, error);
                      });
    }
    const StencilOp fail_ops[] = {kStencilOps[1], kStencilOps[2], kStencilOps[5], kStencilOps[6]};
    for (const StencilOp& op : fail_ops) {
        Cases::single(rr::CaseSpec("om", std::string("stencil_fail_op_") + op.name).tolerance(rr::Tolerance::blend(2)).tag("stencil"),
                      [op](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          return draw_stencil_mask(kit, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, D3D12_STENCIL_OP_KEEP, op.value, D3D12_STENCIL_OP_KEEP,
                                                   D3D12_COMPARISON_FUNC_NOT_EQUAL, 0x55, 0xff, 0xff, out, error);
                      });
        Cases::single(rr::CaseSpec("om", std::string("stencil_depth_fail_op_") + op.name).tolerance(rr::Tolerance::blend(2)).tag("stencil"),
                      [op](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          return draw_stencil_mask(kit, DXGI_FORMAT_D24_UNORM_S8_UINT, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, op.value,
                                                   D3D12_COMPARISON_FUNC_ALWAYS, 0x55, 0xff, 0xff, out, error);
                      });
    }
    for (const CompareFunc& f : kFuncs) {
        Cases::single(rr::CaseSpec("om", std::string("stencil_func_") + f.name).tolerance(rr::Tolerance::blend(2)).tag("stencil"),
                      [f](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          return draw_stencil_mask(kit, DXGI_FORMAT_D24_UNORM_S8_UINT, D3D12_STENCIL_OP_REPLACE, D3D12_STENCIL_OP_KEEP,
                                                   D3D12_STENCIL_OP_KEEP, f.value, 0x55, 0xff, 0xff, out, error);
                      });
    }
    Cases::single(rr::CaseSpec("om", "stencil_masks_partial").tolerance(rr::Tolerance::blend(2)).tag("stencil"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) {
                      return draw_stencil_mask(kit, DXGI_FORMAT_D24_UNORM_S8_UINT, D3D12_STENCIL_OP_REPLACE, D3D12_STENCIL_OP_KEEP,
                                               D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_EQUAL, 0x0f, 0x0f, 0xf0, out, error);
                  });
}

void register_two_sided() {
    Cases::single(rr::CaseSpec("om", "stencil_two_sided_counting").tolerance(rr::Tolerance::blend(2)).tag("stencil"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) {
                      Stage stage;
                      if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT)) {
                          return false;
                      }
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
                      ID3D12PipelineState* state_counter = counter.build(error);
                      ID3D12PipelineState* state_shade = shade.build(error);
                      if (state_counter == nullptr || state_shade == nullptr) {
                          return false;
                      }
                      const bool ok = kit.run(
                          [&](ID3D12GraphicsCommandList* list) {
                              const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                              stage.canvas.begin(list, clear);
                              stage.backdrop(list);
                              list->SetPipelineState(state_counter);
                              list->OMSetStencilRef(0);
                              stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.2f), 1, 1, 1);
                              stage.mesh(list, stage.sphere, rr::Mat4::translate(0.3f, 0.2f, 0.0f), 1, 1, 1);
                              list->SetPipelineState(state_shade);
                              list->OMSetStencilRef(0);
                              stage.mesh(list, stage.plane, rr::Mat4::identity(), 0.95f, 0.8f, 0.1f);
                          },
                          error);
                      return ok && stage.finish(out, error);
                  });
}

const struct Registrar {
    Registrar() {
        register_blend();
        register_depth();
        register_stencil();
        register_two_sided();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
