#version 450
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 out_color;
void main() {
    float id = float(gl_PrimitiveID);
    out_color = vec4(0.5 + 0.5 * sin(id * 1.3), 0.5 + 0.5 * sin(id * 2.1 + 1.0), 0.5 + 0.5 * sin(id * 0.7 + 2.0), 1.0) * (0.7 + 0.3 * v_color);
}
