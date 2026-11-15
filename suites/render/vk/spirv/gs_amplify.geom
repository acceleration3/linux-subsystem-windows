#version 450
layout(triangles) in;
layout(triangle_strip, max_vertices = 9) out;
layout(location = 0) in vec4 g_color[];
layout(location = 0) out vec4 v_color;
void main() {
    for (int copy = 0; copy < 3; ++copy) {
        vec4 shift = vec4(0.0);
        shift.xy = vec2(-0.6 + 0.6 * float(copy), -0.55 + 0.6 * float(copy % 2));
        for (int i = 0; i < 3; ++i) {
            gl_Position = vec4(gl_in[i].gl_Position.xy * 0.35, 0.0, 1.0) + shift;
            v_color = g_color[i] * (0.6 + 0.2 * float(copy));
            EmitVertex();
        }
        EndPrimitive();
    }
}
