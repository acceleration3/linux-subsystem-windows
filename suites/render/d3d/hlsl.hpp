#pragma once

namespace lsw::suites::render {
class Hlsl {
public:
    static constexpr const char* kMeshVs =
        "cbuffer Frame : register(b0) { column_major float4x4 transform; };\n"
        "struct VsIn { float3 pos : POSITION; float3 col : COLOR; };\n"
        "struct VsOut { float4 pos : SV_Position; float3 col : COLOR; };\n"
        "VsOut main(VsIn i) { VsOut o; o.pos = mul(transform, float4(i.pos, 1.0)); o.col = i.col; return o; }\n";
    static constexpr const char* kMeshPs =
        "struct VsOut { float4 pos : SV_Position; float3 col : COLOR; };\n"
        "float4 main(VsOut i) : SV_Target { return float4(i.col, 1.0); }\n";
    static constexpr const char* kMrtPs =
        "struct VsOut { float4 pos : SV_Position; float3 col : COLOR; };\n"
        "struct PsOut { float4 c0 : SV_Target0; float4 c1 : SV_Target1; };\n"
        "PsOut main(VsOut i) { PsOut o; o.c0 = float4(i.col, 1.0); o.c1 = float4(i.col.bgr * 0.5 + 0.25, 0.75); return o; }\n";
    static constexpr const char* kTexVs =
        "struct VsIn { float2 pos : POSITION; float2 uv : TEXCOORD0; };\n"
        "struct VsOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
        "VsOut main(VsIn i) { VsOut o; o.pos = float4(i.pos, 0.0, 1.0); o.uv = i.uv; return o; }\n";
    static constexpr const char* kTexPs =
        "cbuffer Tint : register(b0) { float4 tint; };\n"
        "Texture2D tex : register(t0);\n"
        "SamplerState smp : register(s0);\n"
        "struct VsOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
        "float4 main(VsOut i) : SV_Target { return tex.Sample(smp, i.uv) * tint; }\n";
};
}  // namespace lsw::suites::render
