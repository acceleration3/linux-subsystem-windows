#version 450
layout(location = 0) in vec3 in_pos;
layout(push_constant) uniform P { mat4 vp; } pc;
void main() {
    gl_Position = pc.vp * vec4(in_pos, 1.0);
}
