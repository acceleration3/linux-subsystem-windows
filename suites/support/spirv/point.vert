#version 450
layout(push_constant) uniform PC { vec4 rect; vec4 color; float depth; } pc;
void main() {
    gl_Position = vec4(pc.rect.x, pc.rect.y, pc.depth, 1.0);
    gl_PointSize = pc.rect.z;
}
