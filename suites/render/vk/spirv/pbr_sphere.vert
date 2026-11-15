#version 450
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_n;
layout(push_constant) uniform P { mat4 vp; vec4 center; vec4 material; vec4 eye; } pc;
layout(location = 0) out vec3 v_world;
layout(location = 1) out vec3 v_n;
void main() {
    v_world = pc.center.xyz + in_pos * pc.center.w;
    gl_Position = pc.vp * vec4(v_world, 1.0);
    v_n = in_n;
}
