#version 450
layout(set = 0, binding = 0) uniform sampler2D u_albedo;
layout(set = 0, binding = 1) uniform sampler2D u_ao;
layout(set = 0, binding = 2) uniform sampler2D u_g;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    vec4 albedo = texture(u_albedo, v_uv);
    if (albedo.a < 0.5) {
        out_color = vec4(0.55, 0.65, 0.8, 1.0);
        return;
    }
    float ao = texture(u_ao, v_uv).r;
    vec3 n = normalize(texture(u_g, v_uv).xyz);
    float ndl = max(dot(n, normalize(vec3(0.5, 0.7, 0.6))), 0.0);
    out_color = vec4(albedo.rgb * (0.65 * ao + 0.45 * ndl * (0.4 + 0.6 * ao)), 1.0);
}
