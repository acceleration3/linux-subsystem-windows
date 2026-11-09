#version 450
layout(set = 0, binding = 0) uniform samplerCube u_env;
layout(push_constant) uniform P { vec4 f; vec4 r; vec4 u; vec4 rough; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
float radical(uint bits) {
    bits = bitfieldReverse(bits);
    return float(bits) * 2.3283064365386963e-10;
}
void main() {
    vec2 ndc = v_uv * 2.0 - 1.0;
    vec3 n = normalize(pc.f.xyz + ndc.x * pc.r.xyz - ndc.y * pc.u.xyz);
    vec3 v = n;
    float a = pc.rough.x * pc.rough.x;
    vec3 up = abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(up, n));
    vec3 b = cross(n, t);
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    for (uint i = 0u; i < 96u; ++i) {
        float u1 = (float(i) + 0.5) / 96.0;
        float u2 = radical(i);
        float phi = 6.2831853 * u1;
        float cos_theta = sqrt((1.0 - u2) / (1.0 + (a * a - 1.0) * u2));
        float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
        vec3 h = t * (sin_theta * cos(phi)) + b * (sin_theta * sin(phi)) + n * cos_theta;
        vec3 l = normalize(2.0 * dot(v, h) * h - v);
        float ndl = dot(n, l);
        if (ndl > 0.0) {
            sum += textureLod(u_env, l, 0.0).rgb * ndl;
            weight += ndl;
        }
    }
    out_color = vec4(sum / max(weight, 1e-4), 1.0);
}
