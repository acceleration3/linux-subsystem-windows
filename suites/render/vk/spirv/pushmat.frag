#version 450
layout(push_constant) uniform F { layout(offset = 64) vec4 tint; layout(offset = 80) vec4 add; } f;
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = v_color * f.tint + f.add;
}
