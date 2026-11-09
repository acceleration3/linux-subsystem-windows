#version 450
layout(location = 0) in vec3 v_n;
layout(location = 1) in vec3 v_c;
layout(location = 2) in vec3 v_world;
layout(location = 0) out vec4 out_color;
void main() {
    vec3 n = normalize(v_n);
    float ndl = max(dot(n, normalize(vec3(0.4, 0.8, 0.5))), 0.0);
    vec2 cell = floor(v_world.xz * 1.5);
    out_color = vec4(v_c * (0.85 + 0.15 * mod(cell.x + cell.y, 2.0)) * (0.3 + 0.7 * ndl), 1.0);
}
