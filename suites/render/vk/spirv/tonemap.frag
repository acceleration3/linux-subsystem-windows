#version 450
layout(set = 0, binding = 0) uniform sampler2D u_hdr;
layout(set = 0, binding = 1) uniform sampler2D u_bloom;
layout(push_constant) uniform P { vec4 exposure; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
void main() {
    vec3 c = texture(u_hdr, v_uv).rgb * pc.exposure.x + texture(u_bloom, v_uv).rgb * 1.5;
    out_color = vec4(aces(c), 1.0);
}
