#version 450
layout(set = 0, binding = 0) uniform samplerCube u_cube;
layout(push_constant) uniform P { mat4 vp; vec4 light; vec4 eye; } pc;
layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_n;
layout(location = 2) in vec3 v_c;
layout(location = 0) out vec4 out_color;
void main() {
    vec3 to_frag = v_world - pc.light.xyz;
    float dist = length(to_frag);
    vec3 l = -to_frag / dist;
    vec3 n = normalize(v_n);
    if (dot(n, pc.eye.xyz - v_world) < 0.0) {
        n = -n;
    }
    vec3 t = normalize(cross(to_frag, vec3(0.0, 1.0, 0.01)));
    vec3 b = normalize(cross(to_frag, t));
    float lit = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec3 off = (i == 0 ? t : (i == 1 ? -t : (i == 2 ? b : -b))) * dist * 0.012;
        float stored = texture(u_cube, to_frag + off).r;
        lit += dist / pc.light.w - 0.012 > stored ? 0.0 : 1.0;
    }
    lit *= 0.25;
    vec2 cell = floor(v_world.xz * 0.75) + floor(v_world.y * 0.75);
    vec3 base = v_c * (0.85 + 0.15 * mod(cell.x + cell.y, 2.0));
    float att = 1.0 / (1.0 + 0.045 * dist * dist);
    vec3 color = base * (0.12 + 1.5 * max(dot(n, l), 0.0) * att * lit);
    out_color = vec4(color, 1.0);
}
