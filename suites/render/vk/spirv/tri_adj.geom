#version 450
layout(triangles_adjacency) in;
layout(triangle_strip, max_vertices = 3) out;
layout(location = 0) in vec4 g_color[];
layout(location = 0) out vec4 v_color;
void main() {
    for (int i = 0; i < 6; i += 2) {
        gl_Position = gl_in[i].gl_Position;
        v_color = g_color[i] * 0.6 + g_color[(i + 1) % 6] * 0.4;
        EmitVertex();
    }
    EndPrimitive();
}
