#version 450
layout(push_constant) uniform P { vec4 f; vec4 r; vec4 u; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
vec3 sky(vec3 d) {
    float h = d.y;
    vec3 horizon = vec3(0.95, 0.8, 0.62);
    vec3 zenith = vec3(0.18, 0.38, 0.95);
    vec3 ground = vec3(0.16, 0.13, 0.11);
    vec3 c = h > 0.0 ? mix(horizon, zenith, pow(h, 0.45)) : mix(horizon * 0.55, ground, pow(-h, 0.35));
    float s = max(dot(d, normalize(vec3(0.5, 0.6, 0.4))), 0.0);
    c += vec3(14.0, 12.0, 9.0) * pow(s, 400.0) + vec3(1.0, 0.65, 0.35) * pow(s, 10.0) * 0.7;
    float strip = smoothstep(0.93, 0.96, d.x) * smoothstep(-0.3, 0.1, d.y) * smoothstep(0.45, 0.2, d.y);
    c += vec3(0.4, 1.8, 2.2) * strip;
    return c;
}
void main() {
    vec2 ndc = v_uv * 2.0 - 1.0;
    vec3 dir = normalize(pc.f.xyz + ndc.x * pc.r.xyz - ndc.y * pc.u.xyz);
    out_color = vec4(sky(dir), 1.0);
}
