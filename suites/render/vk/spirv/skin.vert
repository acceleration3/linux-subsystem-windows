#version 450
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec4 in_weights;
layout(set = 0, binding = 0, std430) readonly buffer Bones { mat4 m[]; } bones;
layout(location = 0) out vec4 v_color;
void main() {
    mat4 skin = bones.m[0] * in_weights.x + bones.m[1] * in_weights.y + bones.m[2] * in_weights.z;
    gl_Position = skin * vec4(in_pos, 0.0, 1.0);
    v_color = vec4(in_weights.xyz, 1.0) * 0.8 + 0.2;
}
