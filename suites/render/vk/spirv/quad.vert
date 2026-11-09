#version 450
layout(push_constant) uniform P { vec4 rect; vec4 color; } pc;
layout(location = 0) out vec2 v_uv;
void main() {
    vec2 c = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    v_uv = c;
    gl_Position = vec4(pc.rect.xy + c * pc.rect.zw, 0.0, 1.0);
}
