#version 450
layout(push_constant) uniform P { vec4 rect; vec4 color; } pc;
layout(location = 0, index = 0) out vec4 out_color;
layout(location = 0, index = 1) out vec4 out_blend;
void main() {
    out_color = pc.color;
    out_blend = vec4(pc.color.a, pc.color.r * 0.5 + 0.2, 0.6, pc.color.g * 0.8 + 0.1);
}
