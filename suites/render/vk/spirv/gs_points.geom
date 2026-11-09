#version 450
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
layout(location = 0) in vec4 g_color[];
layout(location = 0) out vec4 v_color;
void main() {
    vec4 p = gl_in[0].gl_Position;
    float s = 0.07 + 0.03 * g_color[0].r;
    gl_Position = p + vec4(-s, -s, 0.0, 0.0); v_color = g_color[0]; EmitVertex();
    gl_Position = p + vec4(s, -s, 0.0, 0.0); v_color = g_color[0] * 0.7; EmitVertex();
    gl_Position = p + vec4(-s, s, 0.0, 0.0); v_color = g_color[0] * 0.5; EmitVertex();
    gl_Position = p + vec4(s, s, 0.0, 0.0); v_color = g_color[0].bgra; EmitVertex();
    EndPrimitive();
}
