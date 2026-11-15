#version 450
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_n;
layout(location = 2) in vec3 in_c;
layout(push_constant) uniform P { mat4 cur; mat4 prev; } pc;
layout(location = 0) out vec3 v_c;
layout(location = 1) out vec4 v_cur;
layout(location = 2) out vec4 v_prev;
void main() {
    v_cur = pc.cur * vec4(in_pos, 1.0);
    v_prev = pc.prev * vec4(in_pos, 1.0);
    gl_Position = v_cur;
    v_c = in_c * (0.55 + 0.45 * max(dot(in_n, normalize(vec3(0.3, 0.9, 0.4))), 0.0));
}
