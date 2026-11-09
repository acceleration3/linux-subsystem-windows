#version 460 core
layout(location = 0) in vec3 v_col;
layout(constant_id = 2) const float kTint = 1.0;
layout(location = 1) uniform vec4 u_color;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = vec4(v_col * kTint, 1.0) * u_color;
}
