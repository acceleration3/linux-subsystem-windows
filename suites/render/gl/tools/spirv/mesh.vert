#version 460 core
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec3 in_col;
layout(constant_id = 0) const float kScale = 1.0;
layout(constant_id = 1) const int kFlip = 0;
layout(location = 0) uniform vec2 u_offset;
layout(location = 0) out vec3 v_col;
void main() {
    vec2 p = in_pos * kScale + u_offset;
    if (kFlip != 0) {
        p.x = -p.x;
    }
    v_col = in_col;
    gl_Position = vec4(p, 0.0, 1.0);
}
