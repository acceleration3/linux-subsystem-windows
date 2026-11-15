#version 450
layout(set = 0, binding = 0) uniform sampler2D u_ao;
layout(push_constant) uniform P { vec4 texel; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    float sum = 0.0;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            sum += texture(u_ao, v_uv + (vec2(float(x), float(y)) - 1.5) * pc.texel.xy).r;
        }
    }
    out_color = vec4(vec3(sum / 16.0), 1.0);
}
