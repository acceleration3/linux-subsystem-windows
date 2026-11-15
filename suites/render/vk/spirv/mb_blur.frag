#version 450
layout(set = 0, binding = 0) uniform sampler2D u_color;
layout(set = 0, binding = 1) uniform sampler2D u_velocity;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    vec2 vel = texture(u_velocity, v_uv).rg;
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 12; ++i) {
        float t = float(i) / 11.0 - 0.5;
        sum += texture(u_color, v_uv + vel * t).rgb;
    }
    out_color = vec4(sum / 12.0, 1.0);
}
