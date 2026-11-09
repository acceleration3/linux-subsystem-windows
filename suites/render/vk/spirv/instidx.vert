#version 450
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 v_color;
void main() {
    float inst = float(gl_InstanceIndex);
    vec2 off = vec2(mod(inst, 4.0) * 0.46 - 0.69, floor(inst / 4.0) * 0.46 - 0.69);
    gl_Position = vec4(in_pos * 0.2 + off, 0.0, 1.0);
    v_color = in_color * (0.55 + 0.15 * mod(inst, 4.0));
    v_color.a = 1.0;
}
