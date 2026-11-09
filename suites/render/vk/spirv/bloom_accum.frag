#version 450
layout(set = 0, binding = 0) uniform sampler2D u_new;
layout(set = 0, binding = 1) uniform sampler2D u_history;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = vec4(mix(texture(u_history, v_uv).rgb, texture(u_new, v_uv).rgb, 0.5), 1.0);
}
