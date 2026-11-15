#version 450
layout(push_constant) uniform V { vec4 rect; } v;
layout(location = 0) out vec2 v_uv;
void main() {
    vec2 c = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    v_uv = c;
    gl_Position = vec4(v.rect.xy + c * v.rect.zw, 0.0, 1.0);
}
