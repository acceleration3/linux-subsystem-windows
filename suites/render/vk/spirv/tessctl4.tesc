#version 450
layout(vertices = 4) out;
layout(push_constant) uniform P { vec4 outer; vec4 inner; } pc;
layout(location = 0) in vec4 c_color[];
layout(location = 0) out vec4 t_color[];
void main() {
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    t_color[gl_InvocationID] = c_color[gl_InvocationID];
    if (gl_InvocationID == 0) {
        gl_TessLevelOuter[0] = pc.outer.x;
        gl_TessLevelOuter[1] = pc.outer.y;
        gl_TessLevelOuter[2] = pc.outer.z;
        gl_TessLevelOuter[3] = pc.outer.w;
        gl_TessLevelInner[0] = pc.inner.x;
        gl_TessLevelInner[1] = pc.inner.y;
    }
}
