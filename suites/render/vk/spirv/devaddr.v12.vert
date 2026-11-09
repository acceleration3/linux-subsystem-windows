#version 450
#extension GL_EXT_buffer_reference : require
layout(buffer_reference, std430) readonly buffer Verts { vec4 v[]; };
layout(push_constant) uniform P { Verts verts; } pc;
layout(location = 0) out vec4 v_color;
void main() {
    vec4 pos = pc.verts.v[gl_VertexIndex * 2];
    vec4 col = pc.verts.v[gl_VertexIndex * 2 + 1];
    gl_Position = vec4(pos.xy, 0.0, 1.0);
    v_color = col;
}
