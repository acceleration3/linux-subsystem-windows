#version 450
layout(set = 0, binding = 0) uniform samplerCube u_env;
layout(push_constant) uniform P { vec4 f; vec4 r; vec4 u; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    vec2 ndc = v_uv * 2.0 - 1.0;
    vec3 dir = normalize(pc.f.xyz + ndc.x * pc.r.xyz - ndc.y * pc.u.xyz);
    vec3 c = textureLod(u_env, dir, 0.0).rgb;
    c = c / (1.0 + c);
    out_color = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
}
