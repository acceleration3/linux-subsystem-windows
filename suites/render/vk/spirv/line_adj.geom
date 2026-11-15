#version 450
layout(lines_adjacency) in;
layout(line_strip, max_vertices = 2) out;
layout(location = 0) in vec4 g_color[];
layout(location = 0) out vec4 v_color;
void main() {
    gl_Position = gl_in[1].gl_Position;
    v_color = g_color[1] * 0.7 + g_color[0] * 0.3;
    EmitVertex();
    gl_Position = gl_in[2].gl_Position;
    v_color = g_color[2] * 0.7 + g_color[3] * 0.3;
    EmitVertex();
    EndPrimitive();
}
