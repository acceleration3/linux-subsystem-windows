#version 450
layout(set = 0, binding = 0) uniform samplerCube u_env;
layout(push_constant) uniform P { vec4 f; vec4 r; vec4 u; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    vec2 ndc = v_uv * 2.0 - 1.0;
    vec3 n = normalize(pc.f.xyz + ndc.x * pc.r.xyz - ndc.y * pc.u.xyz);
    vec3 up = abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(up, n));
    vec3 b = cross(n, t);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 96; ++i) {
        float u1 = (float(i) + 0.5) / 96.0;
        float u2 = fract(float(i) * 0.6180339887);
        float rr = sqrt(u1);
        float phi = 6.2831853 * u2;
        vec3 l = vec3(rr * cos(phi), rr * sin(phi), sqrt(1.0 - u1));
        sum += textureLod(u_env, t * l.x + b * l.y + n * l.z, 0.0).rgb;
    }
    out_color = vec4(sum / 96.0, 1.0);
}
