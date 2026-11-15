#version 450
layout(depth_greater) out float gl_FragDepth;
layout(location = 0) in vec2 v_uv;
layout(push_constant) uniform P { vec4 rect; vec4 color; vec4 depth; } pc;
layout(location = 0) out vec4 out_color;
void main() {
    gl_FragDepth = min(gl_FragCoord.z + 0.3 * v_uv.x, 1.0);
    out_color = pc.color;
}
