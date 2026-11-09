#version 450
layout(set = 0, binding = 0, std430) readonly buffer C { uint c[]; } cells;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    ivec2 cell = min(ivec2(v_uv * 32.0), ivec2(31));
    uint v = cells.c[cell.y * 32 + cell.x];
    float edge = step(0.08, fract(v_uv.x * 32.0)) * step(0.08, fract(v_uv.y * 32.0));
    vec3 live = mix(vec3(0.2, 0.9, 0.4), vec3(0.95, 0.5, 0.2), float(v >> 1) / 7.0);
    out_color = vec4(((v & 1u) == 1u ? live : vec3(0.06, 0.07, 0.12)) * (0.5 + 0.5 * edge), 1.0);
}
