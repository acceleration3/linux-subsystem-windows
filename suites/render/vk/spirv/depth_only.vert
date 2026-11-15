#version 450
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_color;
layout(push_constant) uniform P { mat4 view_proj; mat4 light_vp; } pc;
void main() {
    gl_Position = pc.light_vp * vec4(in_pos, 1.0);
}
