#version 450
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec3 in_color;
layout(location = 2) in float in_shade;
layout(location = 0) out vec4 v_color;
void main() {
    gl_Position = vec4(in_pos, 0.0, 1.0);
    v_color = vec4(in_color * in_shade, 1.0);
}
