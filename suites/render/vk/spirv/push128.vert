#version 450
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec4 in_color;
layout(push_constant) uniform P { vec4 a; vec4 b; vec4 c; vec4 d; vec4 e; vec4 f; vec4 g; vec4 h; } pc;
layout(location = 0) out vec4 v_color;
void main() {
    gl_Position = vec4(in_pos * (pc.a.x + pc.h.w) * 0.8, 0.0, 1.0);
    v_color = in_color * (pc.b + pc.c + pc.d);
}
