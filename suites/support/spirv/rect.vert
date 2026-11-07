#version 450
layout(push_constant) uniform PC { vec4 rect; vec4 color; float depth; } pc;
void main() {
    vec2 c[4] = vec2[](vec2(pc.rect.x, pc.rect.y), vec2(pc.rect.z, pc.rect.y), vec2(pc.rect.x, pc.rect.w), vec2(pc.rect.z, pc.rect.w));
    gl_Position = vec4(c[gl_VertexIndex], pc.depth, 1.0);
}
