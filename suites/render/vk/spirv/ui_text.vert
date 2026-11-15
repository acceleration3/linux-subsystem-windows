#version 450
layout(location = 0) in vec4 in_pos_uv;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;
void main() {
    gl_Position = vec4(in_pos_uv.xy, 0.0, 1.0);
    v_uv = in_pos_uv.zw;
    v_color = in_color;
}
