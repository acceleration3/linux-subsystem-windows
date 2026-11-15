#version 450
layout(vertices = 3) out;
layout(location = 0) in vec4 c_color[];
layout(location = 0) out vec4 t_color[];
void main() {
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    t_color[gl_InvocationID] = c_color[gl_InvocationID];
    if (gl_InvocationID == 0) {
        gl_TessLevelOuter[0] = 6.0;
        gl_TessLevelOuter[1] = 12.0;
        gl_TessLevelOuter[2] = 6.0;
        gl_TessLevelOuter[3] = 6.0;
        gl_TessLevelInner[0] = 6.0;
        gl_TessLevelInner[1] = 6.0;
    }
}
