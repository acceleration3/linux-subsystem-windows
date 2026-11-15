#version 450
layout(set = 0, binding = 0) uniform samplerCube u_cube;
layout(push_constant) uniform P { mat4 vp; vec4 eye; } pc;
layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_n;
layout(location = 2) in vec3 v_c;
layout(location = 0) out vec4 out_color;
void main() {
    vec3 n = normalize(v_n);
    vec3 v = normalize(pc.eye.xyz - v_world);
    vec3 r = reflect(-v, n);
    float fres = 0.3 + 0.7 * pow(1.0 - max(dot(n, v), 0.0), 3.0);
    vec3 env = texture(u_cube, r).rgb;
    out_color = vec4(mix(v_c * 0.25, env, fres * 0.9 + 0.1), 1.0);
}
