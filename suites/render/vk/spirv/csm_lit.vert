#version 450
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_n;
layout(location = 2) in vec3 in_c;
layout(location = 3) in vec2 in_uv;
layout(push_constant) uniform P { mat4 vp; } pc;
layout(location = 0) out vec3 v_world;
layout(location = 1) out vec3 v_n;
layout(location = 2) out vec3 v_c;
layout(location = 3) out float v_depth;
void main() {
    gl_Position = pc.vp * vec4(in_pos, 1.0);
    v_world = in_pos;
    v_n = in_n;
    v_c = in_c;
    v_depth = gl_Position.w;
}
