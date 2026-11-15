#version 450
layout(triangles, invocations = 4) in;
layout(triangle_strip, max_vertices = 3) out;
layout(location = 0) in vec4 g_color[];
layout(location = 0) out vec4 v_color;
void main() {
    for (int i = 0; i < 3; ++i) {
        gl_Position = gl_in[i].gl_Position;
        gl_ViewportIndex = gl_InvocationID;
        v_color = g_color[i] * (0.5 + 0.17 * float(gl_InvocationID));
        v_color.a = 1.0;
        EmitVertex();
    }
    EndPrimitive();
}
