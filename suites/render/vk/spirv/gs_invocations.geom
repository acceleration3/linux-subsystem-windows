#version 450
layout(triangles, invocations = 4) in;
layout(triangle_strip, max_vertices = 3) out;
layout(location = 0) in vec4 g_color[];
layout(location = 0) out vec4 v_color;
void main() {
    float id = float(gl_InvocationID);
    for (int i = 0; i < 3; ++i) {
        gl_Position = vec4(gl_in[i].gl_Position.xy * 0.4 + vec2(-0.55 + 0.37 * id, -0.5 + 0.3 * mod(id, 2.0)), 0.0, 1.0);
        v_color = g_color[i].gbra * (0.5 + 0.17 * id);
        EmitVertex();
    }
    EndPrimitive();
}
