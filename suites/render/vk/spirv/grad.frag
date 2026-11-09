#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = vec4(v_uv.x * 0.8 + 0.1, v_uv.y * 0.6 + 0.2, 0.7 - v_uv.x * 0.4, 0.5 + 0.4 * v_uv.y);
}
