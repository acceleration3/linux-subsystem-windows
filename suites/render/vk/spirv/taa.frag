#version 450
layout(set = 0, binding = 0) uniform sampler2D u_current;
layout(set = 0, binding = 1) uniform sampler2D u_history;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = mix(texture(u_history, v_uv), texture(u_current, v_uv), 0.25);
}
