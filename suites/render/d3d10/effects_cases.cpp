#include "render/d3d10/util.hpp"

namespace lsw::suites::render::d3d10 {
namespace {

const char* kFxBase =
    "cbuffer PerFrame { float4x4 vp; float4 tint; float time; };\n"
    "Texture2D diffuse; \n"
    "SamplerState linearWrap { Filter = MIN_MAG_MIP_LINEAR; AddressU = WRAP; AddressV = WRAP; };\n"
    "RasterizerState NoCull { CullMode = NONE; FillMode = SOLID; };\n"
    "RasterizerState Wire { CullMode = NONE; FillMode = WIREFRAME; };\n"
    "DepthStencilState DepthOn { DepthEnable = TRUE; DepthFunc = LESS_EQUAL; DepthWriteMask = ALL; };\n"
    "DepthStencilState DepthRead { DepthEnable = TRUE; DepthFunc = LESS_EQUAL; DepthWriteMask = ZERO; };\n"
    "BlendState Opaque { BlendEnable[0] = FALSE; };\n"
    "BlendState Additive { BlendEnable[0] = TRUE; SrcBlend = ONE; DestBlend = ONE; BlendOp = ADD; SrcBlendAlpha = ONE; DestBlendAlpha = ONE; BlendOpAlpha = ADD; RenderTargetWriteMask[0] = 0x0F; };\n"
    "struct VSIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
    "struct VSOut { float4 p : SV_Position; float3 n : NORMAL; float2 uv : TEXCOORD0; float3 w : TEXCOORD1; };\n"
    "VSOut VS(VSIn i) { VSOut o; o.p = mul(vp, float4(i.p, 1)); o.n = i.n; o.uv = i.uv; o.w = i.p; return o; }\n"
    "float4 PSLit(VSOut i) : SV_Target { float l = saturate(dot(normalize(i.n), normalize(float3(0.4, 0.8, 0.5)))) * 0.75 + 0.25; return float4(l * (0.55 + 0.45 * sin(i.w * 3 + 1)), 1) * tint; }\n"
    "float4 PSFlat(VSOut i) : SV_Target { return tint; }\n"
    "float4 PSTex(VSOut i) : SV_Target { return diffuse.Sample(linearWrap, i.uv * 4) * tint; }\n";

struct FxScene {
    Com<ID3D10Effect> fx;
    Com<ID3D10InputLayout> layout;
    MeshBuffers mesh;
    ID3D10EffectTechnique* tech = nullptr;
    ID3D10EffectMatrixVariable* vp = nullptr;
    ID3D10EffectVectorVariable* tint = nullptr;
    ID3D10EffectScalarVariable* time = nullptr;

    bool open(Rig& r, const std::string& source, const char* technique, const char* profile = "fx_4_0", ID3D10EffectPool* pool = nullptr, const MeshData* data = nullptr) {
        fx = r.effect(source, profile, pool);
        if (!r.ok()) return false;
        tech = fx->GetTechniqueByName(technique);
        if (tech == nullptr || !tech->IsValid()) {
            r.fail(std::string("technique ") + technique + " is not valid");
            return false;
        }
        vp = fx->GetVariableByName("vp") != nullptr ? fx->GetVariableByName("vp")->AsMatrix() : nullptr;
        tint = fx->GetVariableByName("tint") != nullptr ? fx->GetVariableByName("tint")->AsVector() : nullptr;
        time = fx->GetVariableByName("time") != nullptr ? fx->GetVariableByName("time")->AsScalar() : nullptr;
        D3D10_PASS_DESC pd{};
        r.check(tech->GetPassByIndex(0)->GetDesc(&pd), "GetDesc of the pass");
        if (!r.ok()) return false;
        r.check(r.d->CreateInputLayout(mesh_elements().data(), 3, pd.pIAInputSignature, pd.IAInputSignatureSize, layout.put()), "CreateInputLayout from the effect signature");
        mesh = upload(r, data != nullptr ? *data : Geo::torus(20, 12, 1.0f, 0.4f));
        return r.ok();
    }

    void set_vp(const rr::Mat4& m) {
        if (vp != nullptr) vp->SetMatrix(const_cast<float*>(m.m));
    }
    void set_tint(float a, float b, float c, float d) {
        const float t[4] = {a, b, c, d};
        if (tint != nullptr) tint->SetFloatVector(const_cast<float*>(t));
    }

    void draw_passes(Rig& r, const rr::Mat4& m, bool instanced_passes = true) {
        (void)instanced_passes;
        set_vp(m);
        r.d->IASetInputLayout(layout.get());
        D3D10_TECHNIQUE_DESC td{};
        tech->GetDesc(&td);
        for (UINT p = 0; p < td.Passes; ++p) {
            r.check(tech->GetPassByIndex(p)->Apply(0), "pass Apply");
            draw_mesh(r, mesh);
        }
    }
};

const Registrar kEffects{[] {
    const rr::Tolerance tol = scene_tol(8);
    single(spec("effects", "single_technique_constants").tolerance(tol), [](Rig& r, rr::CaptureSet& o) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        FxScene s;
        if (!s.open(r, std::string(kFxBase) + "technique10 Main { pass P0 { SetVertexShader(CompileShader(vs_4_0, VS())); SetGeometryShader(NULL); SetPixelShader(CompileShader(ps_4_0, PSLit()));"
                                               " SetRasterizerState(NoCull); SetDepthStencilState(DepthOn, 0); SetBlendState(Opaque, float4(0, 0, 0, 0), 0xFFFFFFFF); } }\n", "Main")) return;
        s.set_tint(1.0f, 0.9f, 0.8f, 1.0f);
        s.draw_passes(r, camera(0.5f, 0.6f, 4.0f));
        r.cap_main(o);
    });
    single(spec("effects", "multi_pass_solid_wire_additive").tolerance(lines_tol()), [](Rig& r, rr::CaptureSet& o) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        FxScene s;
        if (!s.open(r, std::string(kFxBase) +
                           "technique10 Layers {\n"
                           " pass Solid { SetVertexShader(CompileShader(vs_4_0, VS())); SetPixelShader(CompileShader(ps_4_0, PSLit())); SetRasterizerState(NoCull); SetDepthStencilState(DepthOn, 0); SetBlendState(Opaque, float4(0, 0, 0, 0), 0xFFFFFFFF); }\n"
                           " pass Wire { SetVertexShader(CompileShader(vs_4_0, VS())); SetPixelShader(CompileShader(ps_4_0, PSFlat())); SetRasterizerState(Wire); SetDepthStencilState(DepthRead, 0); SetBlendState(Additive, float4(0, 0, 0, 0), 0xFFFFFFFF); }\n"
                           "}\n", "Layers")) return;
        s.set_tint(0.8f, 0.8f, 0.8f, 1.0f);
        s.draw_passes(r, camera(0.7f, 0.5f, 4.0f));
        r.cap_main(o);
    });
    single(spec("effects", "state_blocks_blend_and_depth").tolerance(rr::Tolerance::raster(8, 1)), [](Rig& r, rr::CaptureSet& o) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        FxScene s;
        if (!s.open(r, std::string(kFxBase) +
                           "BlendState Alpha { BlendEnable[0] = TRUE; SrcBlend = SRC_ALPHA; DestBlend = INV_SRC_ALPHA; BlendOp = ADD; SrcBlendAlpha = ONE; DestBlendAlpha = INV_SRC_ALPHA; BlendOpAlpha = ADD; RenderTargetWriteMask[0] = 0x0F; };\n"
                           "technique10 Glass { pass P { SetVertexShader(CompileShader(vs_4_0, VS())); SetPixelShader(CompileShader(ps_4_0, PSLit())); SetRasterizerState(NoCull); SetDepthStencilState(DepthRead, 0); SetBlendState(Alpha, float4(0, 0, 0, 0), 0xFFFFFFFF); } }\n",
                       "Glass")) return;
        s.set_tint(1.0f, 0.8f, 0.5f, 0.45f);
        s.draw_passes(r, camera(0.2f, 0.4f, 4.0f));
        r.cap_main(o);
    });
    single(spec("effects", "texture_variable_and_sampler_state").tolerance(scene_tol(40, 5e-3, 128)), [](Rig& r, rr::CaptureSet& o) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        FxScene s;
        if (!s.open(r, std::string(kFxBase) + "technique10 Tex { pass P { SetVertexShader(CompileShader(vs_4_0, VS())); SetPixelShader(CompileShader(ps_4_0, PSTex())); SetRasterizerState(NoCull); SetDepthStencilState(DepthOn, 0); SetBlendState(Opaque, float4(0, 0, 0, 0), 0xFFFFFFFF); } }\n", "Tex")) return;
        Com<ID3D10Texture2D> tex = r.rgba_tex(64, 64, tile_pattern(64, 64, 1), true);
        Com<ID3D10ShaderResourceView> view = r.srv(tex.get());
        if (!r.ok()) return;
        s.fx->GetVariableByName("diffuse")->AsShaderResource()->SetResource(view.get());
        s.set_tint(1, 1, 1, 1);
        s.draw_passes(r, camera(0.9f, 0.5f, 4.0f));
        r.cap_main(o);
    });
    single(spec("effects", "two_techniques_selected_by_index").tolerance(tol), [](Rig& r, rr::CaptureSet& o) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        FxScene s;
        if (!s.open(r, std::string(kFxBase) +
                           "technique10 Flat { pass P { SetVertexShader(CompileShader(vs_4_0, VS())); SetPixelShader(CompileShader(ps_4_0, PSFlat())); SetRasterizerState(NoCull); SetDepthStencilState(DepthOn, 0); } }\n"
                           "technique10 Lit { pass P { SetVertexShader(CompileShader(vs_4_0, VS())); SetPixelShader(CompileShader(ps_4_0, PSLit())); SetRasterizerState(NoCull); SetDepthStencilState(DepthOn, 0); } }\n",
                       "Flat")) return;
        D3D10_EFFECT_DESC ed{};
        s.fx->GetDesc(&ed);
        if (ed.Techniques != 2) {
            r.fail("the effect reports " + std::to_string(ed.Techniques) + " techniques instead of 2");
            return;
        }
        s.set_tint(0.3f, 0.6f, 1.0f, 1.0f);
        s.draw_passes(r, camera(0.4f, 0.5f, 4.0f) * rr::Mat4::translate(-0.9f, 0, 0) * rr::Mat4::scale(0.6f, 0.6f, 0.6f));
        s.tech = s.fx->GetTechniqueByIndex(1);
        s.set_tint(1.0f, 0.8f, 0.6f, 1.0f);
        s.draw_passes(r, camera(0.4f, 0.5f, 4.0f) * rr::Mat4::translate(0.9f, 0, 0) * rr::Mat4::scale(0.6f, 0.6f, 0.6f));
        r.cap_main(o);
    });
    single(spec("effects", "struct_array_constants_by_reflection").tolerance(tol), [](Rig& r, rr::CaptureSet& o) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        FxScene s;
        const std::string fx =
            "struct Light { float3 dir; float4 color; };\n"
            "cbuffer Frame { float4x4 vp; Light lights[3]; float4 ambient; };\n"
            "struct VSIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
            "struct VSOut { float4 p : SV_Position; float3 n : NORMAL; };\n"
            "VSOut VS(VSIn i) { VSOut o; o.p = mul(vp, float4(i.p, 1)); o.n = i.n; return o; }\n"
            "float4 PS(VSOut i) : SV_Target { float4 c = ambient; [unroll] for (int k = 0; k < 3; ++k) c += lights[k].color * saturate(dot(normalize(i.n), normalize(lights[k].dir))); return float4(c.rgb, 1); }\n"
            "DepthStencilState DepthOn { DepthEnable = TRUE; DepthFunc = LESS_EQUAL; };\n"
            "technique10 T { pass P { SetVertexShader(CompileShader(vs_4_0, VS())); SetPixelShader(CompileShader(ps_4_0, PS())); SetDepthStencilState(DepthOn, 0); } }\n";
        s.fx = r.effect(fx);
        if (!r.ok()) return;
        s.tech = s.fx->GetTechniqueByName("T");
        D3D10_PASS_DESC pd{};
        s.tech->GetPassByIndex(0)->GetDesc(&pd);
        r.check(r.d->CreateInputLayout(mesh_elements().data(), 3, pd.pIAInputSignature, pd.IAInputSignatureSize, s.layout.put()), "layout");
        s.mesh = upload(r, Geo::sphere(16, 24, 1.2f));
        ID3D10EffectVariable* lights = s.fx->GetVariableByName("lights");
        D3D10_EFFECT_VARIABLE_DESC vd{};
        if (lights == nullptr || FAILED(lights->GetDesc(&vd))) {
            r.fail("the lights variable is missing");
            return;
        }
        const float dirs[3][3] = {{1, 0.3f, 0.2f}, {-0.5f, 0.9f, 0.2f}, {0.0f, -0.4f, 1.0f}};
        const float cols[3][4] = {{0.9f, 0.3f, 0.2f, 1}, {0.2f, 0.7f, 0.3f, 1}, {0.3f, 0.3f, 0.9f, 1}};
        for (UINT k = 0; k < 3; ++k) {
            ID3D10EffectVariable* element = lights->GetElement(k);
            element->GetMemberByName("dir")->AsVector()->SetFloatVector(const_cast<float*>(dirs[k]));
            element->GetMemberByName("color")->AsVector()->SetFloatVector(const_cast<float*>(cols[k]));
        }
        const float amb[4] = {0.08f, 0.08f, 0.1f, 1};
        s.fx->GetVariableByName("ambient")->AsVector()->SetFloatVector(const_cast<float*>(amb));
        s.vp = s.fx->GetVariableByName("vp")->AsMatrix();
        s.draw_passes(r, camera(0.4f, 0.3f, 4.0f));
        r.cap_main(o);
    });
    single(spec("effects", "annotations_and_semantics_drive_state").tolerance(tol), [](Rig& r, rr::CaptureSet& o) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        FxScene s;
        const std::string fx =
            "cbuffer C { float4x4 vp : WORLDVIEWPROJECTION; float4 tint < string UIName = \"Tint\"; float UIMin = 0.0; > = float4(1, 1, 1, 1); float time; };\n"
            "struct VSIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
            "struct VSOut { float4 p : SV_Position; float3 n : NORMAL; };\n"
            "VSOut VS(VSIn i) { VSOut o; o.p = mul(vp, float4(i.p, 1)); o.n = i.n; return o; }\n"
            "float4 PS(VSOut i) : SV_Target { return float4(0.5 + 0.5 * normalize(i.n), 1) * tint; }\n"
            "DepthStencilState DepthOn { DepthEnable = TRUE; DepthFunc = LESS_EQUAL; };\n"
            "technique10 T < string Style = \"matte\"; > { pass P { SetVertexShader(CompileShader(vs_4_0, VS())); SetPixelShader(CompileShader(ps_4_0, PS())); SetDepthStencilState(DepthOn, 0); } }\n";
        if (!s.open(r, fx, "T")) return;
        ID3D10EffectVariable* semantic_var = s.fx->GetVariableBySemantic("WORLDVIEWPROJECTION");
        if (semantic_var == nullptr || !semantic_var->IsValid()) {
            r.fail("GetVariableBySemantic did not find WORLDVIEWPROJECTION");
            return;
        }
        ID3D10EffectVariable* annotation = s.fx->GetVariableByName("tint")->GetAnnotationByName("UIName");
        LPCSTR text = nullptr;
        if (annotation == nullptr || !annotation->IsValid() || FAILED(annotation->AsString()->GetString(&text)) || text == nullptr || std::string(text) != "Tint") {
            r.fail("the UIName annotation cannot be read back");
            return;
        }
        s.vp = semantic_var->AsMatrix();
        s.draw_passes(r, camera(1.1f, 0.5f, 4.0f));
        r.cap_main(o);
    });
    single(spec("effects", "geometry_shader_pass_point_sprites").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        const std::string fx =
            "cbuffer C { float4 tint; };\n"
            "struct VI { float3 p : POSITION; float4 c : COLOR; float2 uv : TEXCOORD0; };\n"
            "struct VO { float4 p : SV_Position; float4 c : COLOR; float2 uv : TEXCOORD0; };\n"
            "VO VS(VI i) { VO o; o.p = float4(i.p, 1); o.c = i.c; o.uv = i.uv; return o; }\n"
            "[maxvertexcount(4)] void GS(point VO v[1], inout TriangleStream<VO> s) { VO o = v[0]; float h = 0.07; o.p = v[0].p + float4(-h, h, 0, 0); s.Append(o); o.p = v[0].p + float4(h, h, 0, 0); s.Append(o); o.p = v[0].p + float4(-h, -h, 0, 0); s.Append(o); o.p = v[0].p + float4(h, -h, 0, 0); s.Append(o); }\n"
            "float4 PS(VO i) : SV_Target { return i.c * tint; }\n"
            "technique10 Sprites { pass P { SetVertexShader(CompileShader(vs_4_0, VS())); SetGeometryShader(CompileShader(gs_4_0, GS())); SetPixelShader(CompileShader(ps_4_0, PS())); } }\n";
        Com<ID3D10Effect> effect = r.effect(fx);
        if (!r.ok()) return;
        ID3D10EffectTechnique* tech = effect->GetTechniqueByName("Sprites");
        D3D10_PASS_DESC pd{};
        tech->GetPassByIndex(0)->GetDesc(&pd);
        Com<ID3D10InputLayout> layout;
        const D3D10_INPUT_ELEMENT_DESC elems[] = {{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
                                                  {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0},
                                                  {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D10_INPUT_PER_VERTEX_DATA, 0}};
        r.check(r.d->CreateInputLayout(elems, 3, pd.pIAInputSignature, pd.IAInputSignatureSize, layout.put()), "layout");
        if (!r.ok()) return;
        const float tint[4] = {1, 0.9f, 0.8f, 1};
        effect->GetVariableByName("tint")->AsVector()->SetFloatVector(const_cast<float*>(tint));
        r.no_cull();
        r.d->IASetInputLayout(layout.get());
        r.check(tech->GetPassByIndex(0)->Apply(0), "Apply");
        r.draw_std(ring_points(40, 0.8f), D3D10_PRIMITIVE_TOPOLOGY_POINTLIST);
        r.cap_main(o);
    });
    single(spec("effects", "pool_shared_constant_buffer").tolerance(tol), [](Rig& r, rr::CaptureSet& o) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        const std::string pool_fx = "shared cbuffer Globals { float4x4 vp; float4 tint; };\n";
        Com<ID3D10EffectPool> pool = r.effect_pool(pool_fx);
        if (!r.ok()) return;
        const std::string fx = std::string(
                                   "shared cbuffer Globals { float4x4 vp; float4 tint; };\n"
                                   "struct VSIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
                                   "struct VSOut { float4 p : SV_Position; float3 n : NORMAL; float3 w : TEXCOORD1; };\n"
                                   "VSOut VS(VSIn i) { VSOut o; o.p = mul(vp, float4(i.p, 1)); o.n = i.n; o.w = i.p; return o; }\n"
                                   "float4 PS(VSOut i) : SV_Target { float l = saturate(dot(normalize(i.n), float3(0.3, 0.8, 0.5))) * 0.7 + 0.3; return float4(l * (0.5 + 0.5 * sin(i.w * 3)), 1) * tint; }\n"
                                   "DepthStencilState DepthOn { DepthEnable = TRUE; DepthFunc = LESS_EQUAL; };\n"
                                   "technique10 T { pass P { SetVertexShader(CompileShader(vs_4_0, VS())); SetPixelShader(CompileShader(ps_4_0, PS())); SetDepthStencilState(DepthOn, 0); } }\n");
        FxScene s;
        if (!s.open(r, fx, "T", "fx_4_0", pool.get())) return;
        ID3D10EffectVariable* vp = pool->AsEffect()->GetVariableByName("vp");
        ID3D10EffectVariable* tint = pool->AsEffect()->GetVariableByName("tint");
        if (vp == nullptr || tint == nullptr) {
            r.fail("the pool does not expose the shared variables");
            return;
        }
        const rr::Mat4 m = camera(0.6f, 0.4f, 4.0f);
        vp->AsMatrix()->SetMatrix(const_cast<float*>(m.m));
        const float t[4] = {1, 0.85f, 0.7f, 1};
        tint->AsVector()->SetFloatVector(const_cast<float*>(t));
        r.d->IASetInputLayout(s.layout.get());
        r.check(s.tech->GetPassByIndex(0)->Apply(0), "Apply");
        draw_mesh(r, s.mesh);
        r.cap_main(o);
    });
    single(spec("effects", "fx_4_1_gather_technique", 1).tolerance(rr::Tolerance::filter(8, 0.01)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        const std::string fx =
            "Texture2D t; SamplerState s { Filter = MIN_MAG_MIP_POINT; AddressU = CLAMP; AddressV = CLAMP; };\n"
            "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
            "VO VS(uint id : SV_VertexID) { VO o; float2 q = float2((id << 1) & 2, id & 2); o.p = float4(q * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = q; return o; }\n"
            "float4 PS(VO i) : SV_Target { float4 g = t.Gather(s, i.uv * 0.9 + 0.05); return float4(g.x, g.y, g.z * g.w, 1); }\n"
            "technique10 G { pass P { SetVertexShader(CompileShader(vs_4_1, VS())); SetPixelShader(CompileShader(ps_4_1, PS())); } }\n";
        Com<ID3D10Effect> effect = r.effect(fx, "fx_4_1");
        Com<ID3D10Texture2D> tex = r.rgba_tex(16, 16, tile_pattern(16, 16, 2));
        Com<ID3D10ShaderResourceView> view = r.srv(tex.get());
        if (!r.ok()) return;
        effect->GetVariableByName("t")->AsShaderResource()->SetResource(view.get());
        r.d->IASetInputLayout(nullptr);
        r.d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        r.no_cull();
        r.check(effect->GetTechniqueByName("G")->GetPassByIndex(0)->Apply(0), "Apply");
        r.d->Draw(3, 0);
        r.cap_main(o);
    });
}};

const Registrar kEffectFrames{[] {
    struct S {
        FxScene fx;
        float angle = 0;
    };
    MultiBody<S> real;
    real.setup = [](Rig& r, S& s) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        const std::string src = std::string(kFxBase) +
                                "float4 PSAnim(VSOut i) : SV_Target { float l = saturate(dot(normalize(i.n), normalize(float3(0.4, 0.8, 0.5)))) * 0.7 + 0.3; float k = 0.5 + 0.5 * sin(time * 2 + i.w.y * 4); return float4(l * k, l * (1 - k) * 0.8, l * 0.6, 1) * tint; }\n"
                                "technique10 Anim { pass P { SetVertexShader(CompileShader(vs_4_0, VS())); SetPixelShader(CompileShader(ps_4_0, PSAnim())); SetRasterizerState(NoCull); SetDepthStencilState(DepthOn, 0); } }\n";
        s.fx.open(r, src, "Anim");
    };
    real.frame = [](Rig& r, S& s, unsigned f) {
        if (f == 0) {
            r.clear(r.main, 0.06f, 0.06f, 0.1f, 1);
            r.clear_depth(r.depth_main);
        }
        if (s.fx.time != nullptr) s.fx.time->SetFloat(0.45f * static_cast<float>(f));
        s.fx.set_tint(1.0f, 1.0f - 0.1f * static_cast<float>(f), 0.5f + 0.07f * static_cast<float>(f), 1.0f);
        r.clear_depth(r.depth_main);
        s.fx.draw_passes(r, camera(0.0f, 0.3f, 7.0f, 1.0f, 0.9f) * rr::Mat4::translate(-2.8f + 0.8f * static_cast<float>(f), 0.3f * std::sin(static_cast<float>(f)), 0) * rr::Mat4::rotate_x(0.3f * static_cast<float>(f)) * rr::Mat4::scale(0.55f, 0.55f, 0.55f));
    };
    multi<S>(spec("effects", "per_frame_variables_leave_a_trail").frames(8, {0, 3, 7}).tolerance(rr::Tolerance::raster(6, 1)), real);
}};

}  // namespace
}  // namespace lsw::suites::render::d3d10
