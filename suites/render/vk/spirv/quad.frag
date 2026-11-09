#version 450
layout(push_constant) uniform P { vec4 rect; vec4 color; } pc;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = pc.color;
}
