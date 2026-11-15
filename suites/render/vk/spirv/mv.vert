#version 450
#extension GL_EXT_multiview : require
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 v_color;
void main() {
    float view = float(gl_ViewIndex);
    gl_Position = vec4(in_pos.x + 0.12 * (view - 0.5), in_pos.y, 0.0, 1.0);
    v_color = in_color * (0.55 + 0.15 * view);
    v_color.a = 1.0;
}
