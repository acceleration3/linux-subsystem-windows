#version 450
#extension GL_ARB_shader_stencil_export : require
layout(location = 0) in vec2 v_uv;
layout(push_constant) uniform P { vec4 rect; vec4 color; vec4 depth; } pc;
layout(location = 0) out vec4 out_color;
void main() {
    gl_FragStencilRefARB = 1 + int(v_uv.x * 3.99) + 4 * int(v_uv.y * 3.99);
    out_color = pc.color;
}
