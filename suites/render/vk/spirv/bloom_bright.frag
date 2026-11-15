#version 450
layout(set = 0, binding = 0) uniform sampler2D u_src;
layout(set = 0, binding = 1) uniform sampler2D u_other;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    vec3 c = texture(u_src, v_uv).rgb;
    out_color = vec4(max(c - vec3(1.0), vec3(0.0)), 1.0);
}
