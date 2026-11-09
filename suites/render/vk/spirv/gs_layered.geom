#version 450
layout(triangles, invocations = 6) in;
layout(triangle_strip, max_vertices = 3) out;
layout(push_constant) uniform P { vec4 layers; } pc;
layout(location = 0) in vec4 g_color[];
layout(location = 0) out vec4 v_color;
void main() {
    if (float(gl_InvocationID) >= pc.layers.x) {
        return;
    }
    float id = float(gl_InvocationID);
    for (int i = 0; i < 3; ++i) {
        gl_Position = vec4(gl_in[i].gl_Position.xy * (0.5 + 0.1 * id) + vec2(0.05 * id - 0.1, 0.0), 0.0, 1.0);
        gl_Layer = gl_InvocationID;
        v_color = g_color[i] * (0.5 + 0.1 * id) + vec4(0.08 * id, 0.0, 0.0, 0.0);
        v_color.a = 1.0;
        EmitVertex();
    }
    EndPrimitive();
}
