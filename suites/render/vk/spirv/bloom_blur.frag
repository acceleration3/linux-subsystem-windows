#version 450
layout(set = 0, binding = 0) uniform sampler2D u_src;
layout(set = 0, binding = 1) uniform sampler2D u_other;
layout(push_constant) uniform P { vec4 dir; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    const float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 acc = texture(u_src, v_uv).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        acc += texture(u_src, v_uv + pc.dir.xy * float(i)).rgb * w[i];
        acc += texture(u_src, v_uv - pc.dir.xy * float(i)).rgb * w[i];
    }
    out_color = vec4(acc, 1.0);
}
