#version 450
layout(set = 0, binding = 0) uniform sampler2DShadow u_shadow;
layout(location = 0) in vec3 v_color;
layout(location = 1) in vec4 v_shadow;
layout(location = 0) out vec4 out_color;
void main() {
    vec3 p = v_shadow.xyz / v_shadow.w;
    vec2 uv = p.xy * 0.5 + 0.5;
    float lit = texture(u_shadow, vec3(uv, p.z - 0.004));
    out_color = vec4(v_color * (0.35 + 0.65 * lit), 1.0);
}
