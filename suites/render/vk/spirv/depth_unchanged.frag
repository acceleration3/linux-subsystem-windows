#version 450
layout(depth_unchanged) out float gl_FragDepth;
layout(location = 0) in vec2 v_uv;
layout(push_constant) uniform P { vec4 rect; vec4 color; vec4 depth; } pc;
layout(location = 0) out vec4 out_color;
void main() {
    gl_FragDepth = gl_FragCoord.z;
    out_color = pc.color * (0.6 + 0.4 * v_uv.x);
}
