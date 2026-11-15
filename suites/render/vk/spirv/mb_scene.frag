#version 450
layout(location = 0) in vec3 v_c;
layout(location = 1) in vec4 v_cur;
layout(location = 2) in vec4 v_prev;
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_velocity;
void main() {
    vec2 a = v_cur.xy / v_cur.w;
    vec2 b = v_prev.xy / v_prev.w;
    out_color = vec4(v_c, 1.0);
    out_velocity = vec4((a - b) * 0.5, 0.0, 1.0);
}
