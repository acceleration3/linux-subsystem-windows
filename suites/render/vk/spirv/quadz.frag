#version 450
layout(push_constant) uniform P { vec4 rect; vec4 color; vec4 depth; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = pc.color;
}
