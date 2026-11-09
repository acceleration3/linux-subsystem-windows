#version 450
layout(location = 0) in vec3 in_pos;
layout(push_constant) uniform P { mat4 vp; vec4 light; } pc;
layout(location = 0) out vec3 v_world;
void main() {
    gl_Position = pc.vp * vec4(in_pos, 1.0);
    v_world = in_pos;
}
