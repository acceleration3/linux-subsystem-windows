#version 460
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 v_color;
void main() {
    float d = float(gl_DrawID);
    float bi = float(gl_BaseInstance);
    float bv = float(gl_BaseVertex);
    gl_Position = vec4(in_pos * 0.25 + vec2(-0.7 + 0.46 * d + 0.12 * float(gl_InstanceIndex - gl_BaseInstance), -0.6 + 0.4 * bi), 0.0, 1.0);
    v_color = vec4(0.3 + 0.2 * d, 0.2 + 0.15 * bv, 0.4 + 0.15 * bi, 1.0) * (0.6 + 0.4 * in_color);
}
