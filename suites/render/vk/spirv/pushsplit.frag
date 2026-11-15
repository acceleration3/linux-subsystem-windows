#version 450
layout(push_constant) uniform F { layout(offset = 16) vec4 color; } f;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = f.color * (0.6 + 0.4 * v_uv.x);
}
