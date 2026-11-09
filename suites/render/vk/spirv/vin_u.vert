#version 450
layout(location = 0) in vec2 in_pos;
layout(location = 1) in uvec4 in_attr;
layout(push_constant) uniform P { vec4 scale; vec4 bias; } pc;
layout(location = 0) out vec4 v_color;
void main() {
    gl_Position = vec4(in_pos, 0.0, 1.0);
    gl_PointSize = 12.0;
    v_color = clamp(vec4(in_attr) * pc.scale + pc.bias, 0.0, 1.0);
}
