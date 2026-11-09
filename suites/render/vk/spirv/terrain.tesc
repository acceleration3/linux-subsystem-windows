#version 450
layout(vertices = 4) out;
layout(push_constant) uniform P { mat4 vp; vec4 params; } pc;
layout(location = 0) in vec4 c_color[];
layout(location = 0) out vec4 t_color[];
void main() {
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    t_color[gl_InvocationID] = c_color[gl_InvocationID];
    if (gl_InvocationID == 0) {
        gl_TessLevelOuter[0] = 8.0; gl_TessLevelOuter[1] = 8.0; gl_TessLevelOuter[2] = 8.0; gl_TessLevelOuter[3] = 8.0;
        gl_TessLevelInner[0] = 8.0; gl_TessLevelInner[1] = 8.0;
    }
}
