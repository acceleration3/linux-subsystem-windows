#version 450
layout(set = 0, binding = 0) readonly buffer Lights { vec4 d[]; } lights;
layout(set = 0, binding = 1) readonly buffer Tiles { uint d[]; } tiles;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    uvec2 tile = uvec2(gl_FragCoord.xy) / 16u;
    uint t = tile.y * 8u + tile.x;
    vec3 base = mix(vec3(0.10, 0.12, 0.16), vec3(0.28, 0.26, 0.22), float(((uint(gl_FragCoord.x) / 8u) + (uint(gl_FragCoord.y) / 8u)) & 1u));
    vec3 acc = base;
    for (uint word = 0u; word < 2u; ++word) {
        uint m = tiles.d[t * 2u + word];
        for (uint bit = 0u; bit < 32u; ++bit) {
            if ((m & (1u << bit)) != 0u) {
                uint i = word * 32u + bit;
                vec4 l = lights.d[i * 2u];
                vec4 col = lights.d[i * 2u + 1u];
                float d = length(gl_FragCoord.xy - l.xy);
                float a = clamp(1.0 - d / l.z, 0.0, 1.0);
                acc += col.rgb * (a * a * col.a);
            }
        }
    }
    out_color = vec4(acc, 1.0);
}
