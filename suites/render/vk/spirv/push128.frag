#version 450
layout(push_constant) uniform P { vec4 a; vec4 b; vec4 c; vec4 d; vec4 e; vec4 f; vec4 g; vec4 h; } pc;
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = v_color + (pc.e + pc.f + pc.g + pc.h) * 0.2;
}
