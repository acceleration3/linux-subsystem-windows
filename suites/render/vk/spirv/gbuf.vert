#version 450
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(push_constant) uniform P { mat4 vp; } pc;
layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_color;
void main() {
    gl_Position = pc.vp * vec4(in_pos, 1.0);
    v_normal = in_normal;
    v_color = in_color;
}
