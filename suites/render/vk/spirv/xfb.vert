#version 450
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec4 in_color;
layout(location = 0, xfb_buffer = 0, xfb_offset = 0, xfb_stride = 24) out vec2 o_pos;
layout(location = 1, xfb_buffer = 0, xfb_offset = 8, xfb_stride = 24) out vec4 o_color;
void main() {
    vec2 p = vec2(in_pos.x * 0.8 + 0.1, in_pos.y * 0.7 - 0.1);
    o_pos = p;
    o_color = in_color.bgra;
    gl_Position = vec4(p, 0.0, 1.0);
}
