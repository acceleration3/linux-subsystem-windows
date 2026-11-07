#version 450
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
layout(push_constant) uniform PC { vec4 rect; vec4 color; float depth; } pc;
void main() {
    gl_Position = vec4(pc.rect.x, pc.rect.y, pc.depth, 1.0);
    EmitVertex();
    gl_Position = vec4(pc.rect.z, pc.rect.y, pc.depth, 1.0);
    EmitVertex();
    gl_Position = vec4(pc.rect.x, pc.rect.w, pc.depth, 1.0);
    EmitVertex();
    gl_Position = vec4(pc.rect.z, pc.rect.w, pc.depth, 1.0);
    EmitVertex();
    EndPrimitive();
}
