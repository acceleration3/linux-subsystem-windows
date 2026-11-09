#version 450
layout(set = 0, binding = 0) uniform usampler2D u_tex;
layout(push_constant) uniform P { vec4 scale; vec4 bias; ivec4 info; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    ivec2 texel = min(ivec2(v_uv * vec2(pc.info.xy)), pc.info.xy - 1);
    vec4 v = vec4(texelFetch(u_tex, texel, pc.info.z));
    out_color = clamp(v * pc.scale + pc.bias, 0.0, 1.0);
}
