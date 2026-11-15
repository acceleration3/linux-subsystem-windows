#version 450
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_n;
layout(location = 2) in vec3 in_c;
layout(location = 3) in vec2 in_uv;
layout(location = 4) in vec4 in_inst;
layout(location = 5) in uint in_mat;
layout(push_constant) uniform P { mat4 vp; } pc;
layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out uint v_mat;
layout(location = 2) out float v_light;
void main() {
    gl_Position = pc.vp * vec4(in_pos * in_inst.w + in_inst.xyz, 1.0);
    v_uv = in_uv;
    v_mat = in_mat;
    v_light = 0.45 + 0.55 * max(dot(in_n, normalize(vec3(0.4, 0.8, 0.5))), 0.0);
}
