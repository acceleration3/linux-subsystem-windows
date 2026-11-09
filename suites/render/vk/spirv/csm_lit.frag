#version 450
layout(set = 0, binding = 0) uniform Cascades { mat4 vp[3]; vec4 splits; vec4 light_dir; } u;
layout(set = 0, binding = 1) uniform sampler2DArrayShadow u_shadow;
layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_n;
layout(location = 2) in vec3 v_c;
layout(location = 3) in float v_depth;
layout(location = 0) out vec4 out_color;
void main() {
    int c = v_depth < u.splits.x ? 0 : (v_depth < u.splits.y ? 1 : 2);
    vec4 sp = u.vp[c] * vec4(v_world, 1.0);
    vec3 p = sp.xyz / sp.w;
    vec2 uv = p.xy * 0.5 + 0.5;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            lit += texture(u_shadow, vec4(uv + vec2(float(x), float(y)) / 256.0, float(c), p.z - 0.0015));
        }
    }
    lit /= 9.0;
    vec3 n = normalize(v_n);
    float ndl = max(dot(n, -u.light_dir.xyz), 0.0);
    vec2 cell = floor(v_world.xz * 0.5);
    float checker = mod(cell.x + cell.y, 2.0);
    vec3 base = v_c * (0.8 + 0.2 * checker);
    vec3 tint = c == 0 ? vec3(1.0, 0.93, 0.93) : (c == 1 ? vec3(0.93, 1.0, 0.93) : vec3(0.93, 0.93, 1.0));
    vec3 color = base * tint * (0.25 + 0.75 * ndl * lit);
    out_color = vec4(mix(color, vec3(0.55, 0.65, 0.8), clamp(v_depth / 70.0, 0.0, 1.0)), 1.0);
}
