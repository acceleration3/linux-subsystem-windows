#version 450
layout(location = 0) in vec3 v_color;
layout(location = 1) in vec3 v_normal;
layout(location = 0) out vec4 out_color;
void main() {
    vec3 n = normalize(v_normal);
    float ndl = max(dot(n, normalize(vec3(0.4, 0.8, 0.6))), 0.0);
    out_color = vec4(v_color * (0.25 + 0.75 * ndl), 1.0);
}
