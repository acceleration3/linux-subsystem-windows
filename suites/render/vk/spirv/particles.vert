#version 450
layout(std430, set = 0, binding = 0) readonly buffer P { vec4 p[]; } parts;
layout(location = 0) out vec4 v_color;
void main() {
    vec4 s = parts.p[gl_VertexIndex];
    gl_Position = vec4(s.xy, 0.0, 1.0);
    gl_PointSize = 4.0;
    float h = float(gl_VertexIndex) * 0.07;
    v_color = vec4(0.5 + 0.5 * sin(h), 0.5 + 0.5 * sin(h + 2.0), 0.5 + 0.5 * sin(h + 4.0), 1.0);
}
