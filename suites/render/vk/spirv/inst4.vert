#version 450
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec4 in_inst;
layout(location = 0) out vec4 v_color;
void main() {
    gl_Position = vec4(in_pos * in_inst.z + in_inst.xy, 0.0, 1.0);
    v_color = vec4(0.5 + 0.5 * sin(in_inst.w * 6.0), 0.5 + 0.5 * sin(in_inst.w * 6.0 + 2.1), 0.5 + 0.5 * sin(in_inst.w * 6.0 + 4.2), 1.0);
}
