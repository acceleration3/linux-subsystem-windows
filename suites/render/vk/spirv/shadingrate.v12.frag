#version 450
#extension GL_EXT_fragment_shading_rate : require
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 out_color;
void main() {
    float rate = float(gl_ShadingRateEXT);
    out_color = vec4(v_color.rgb * (0.4 + 0.1 * rate), 1.0) + vec4(float((gl_ShadingRateEXT & 4) != 0) * 0.3, float((gl_ShadingRateEXT & 8) != 0) * 0.3, 0.0, 0.0);
}
