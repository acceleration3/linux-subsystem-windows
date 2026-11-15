#version 450
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec3 in_inst;
layout(push_constant) uniform P { vec4 wind; } pc;
layout(location = 0) out vec4 v_color;
void main() {
    float sway = sin(pc.wind.x + in_inst.x * 5.0) * 0.06 * (in_pos.y + 1.0);
    vec2 p = vec2(in_pos.x * in_inst.z + sway, in_pos.y * in_inst.z * 1.8) + in_inst.xy;
    gl_Position = vec4(p, in_inst.y * 0.3 + 0.5, 1.0);
    v_color = in_color * (0.6 + 0.4 * in_inst.z / 0.15);
    v_color.a = 1.0;
}
