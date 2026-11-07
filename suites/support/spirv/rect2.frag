#version 450
layout(push_constant) uniform PC { vec4 rect; vec4 color; float depth; } pc;
layout(location = 0) out vec4 c0;
layout(location = 1) out vec4 c1;
void main() {
    c0 = pc.color;
    c1 = vec4(1.0) - pc.color;
}
