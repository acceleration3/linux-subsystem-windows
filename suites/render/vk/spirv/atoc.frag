#version 450
layout(location = 0) in vec2 v_uv;
layout(push_constant) uniform P { vec4 rect; vec4 color; vec4 depth; } pc;
layout(location = 0) out vec4 out_color;
void main() {
    float a = 0.1 + 0.8 * v_uv.x;
    out_color = vec4(pc.color.rgb, a);
}
