#version 450
#extension GL_EXT_fragment_shading_rate : require
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec4 in_color;
layout(push_constant) uniform P { uint rate; } pc;
layout(location = 0) out vec4 v_color;
void main() {
    gl_Position = vec4(in_pos, 0.0, 1.0);
    v_color = in_color;
    gl_PrimitiveShadingRateEXT = int(pc.rate);
}
