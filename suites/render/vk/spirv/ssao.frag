#version 450
layout(set = 0, binding = 0) uniform sampler2D u_g;
layout(push_constant) uniform P { vec4 params; vec4 size; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
const vec3 kKernel[16] = vec3[16](
    vec3(-0.0936, 0.0079, 0.0435),
    vec3(-0.1135, 0.0019, 0.0110),
    vec3(-0.0198, -0.1285, 0.0203),
    vec3(-0.0341, 0.1477, 0.0379),
    vec3(0.0763, -0.0701, 0.1567),
    vec3(0.1566, -0.0628, 0.1512),
    vec3(0.1202, -0.0658, 0.2352),
    vec3(0.2503, 0.1692, 0.1198),
    vec3(0.0640, 0.0217, 0.3788),
    vec3(-0.4478, -0.0142, 0.0561),
    vec3(0.2046, 0.3218, 0.3614),
    vec3(0.2107, 0.1784, 0.5397),
    vec3(-0.3556, -0.1885, 0.5656),
    vec3(-0.7684, -0.0616, 0.1687),
    vec3(-0.6666, -0.4539, 0.3789),
    vec3(0.7573, -0.4610, 0.4625)
);
vec3 view_pos(vec2 uv, float d) {
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(ndc.x * pc.params.x * pc.params.y * d, -ndc.y * pc.params.x * d, -d);
}
void main() {
    vec4 g = texture(u_g, v_uv);
    if (g.w <= 0.0) {
        out_color = vec4(1.0);
        return;
    }
    vec3 n = normalize(g.xyz);
    vec3 pos = view_pos(v_uv, g.w);
    ivec2 px = ivec2(gl_FragCoord.xy);
    float a = float((px.x & 3) + 4 * (px.y & 3)) * 0.3926991;
    vec3 r = vec3(cos(a), sin(a), 0.0);
    vec3 t = normalize(r - n * dot(r, n));
    vec3 b = cross(n, t);
    mat3 tbn = mat3(t, b, n);
    float occ = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec3 s = pos + tbn * kKernel[i] * pc.params.z;
        vec2 ndc = vec2(s.x / (pc.params.x * pc.params.y * -s.z), -s.y / (pc.params.x * -s.z));
        vec2 suv = ndc * 0.5 + 0.5;
        float ds = texture(u_g, suv).w;
        if (ds <= 0.0) {
            ds = 1000.0;
        }
        float range = smoothstep(0.0, 1.0, pc.params.z / max(abs(g.w - ds), 1e-4));
        occ += (ds < -s.z - pc.params.w ? 1.0 : 0.0) * range;
    }
    out_color = vec4(vec3(1.0 - occ / 16.0), 1.0);
}
