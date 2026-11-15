#version 450
#extension GL_ARB_shader_viewport_layer_array : require
layout(push_constant) uniform P { vec4 rect; vec4 color; } pc;
layout(location = 0) out vec4 v_color;
void main() {
    vec2 c = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    gl_Position = vec4(pc.rect.xy + c * pc.rect.zw, 0.0, 1.0);
    gl_ViewportIndex = gl_InstanceIndex;
    v_color = pc.color * (0.5 + 0.25 * float(gl_InstanceIndex));
}
