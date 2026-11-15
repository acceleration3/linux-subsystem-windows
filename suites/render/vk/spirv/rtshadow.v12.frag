#version 460
#extension GL_EXT_ray_query : require
layout(set = 0, binding = 0) uniform accelerationStructureEXT u_tlas;
layout(push_constant) uniform P { mat4 vp; vec4 offset; vec4 light; } pc;
layout(location = 0) in vec3 v_n;
layout(location = 1) in vec3 v_c;
layout(location = 2) in vec3 v_world;
layout(location = 0) out vec4 out_color;
void main() {
    vec3 n = normalize(v_n);
    vec3 to_light = pc.light.xyz - v_world;
    float dist = length(to_light);
    vec3 l = to_light / dist;
    rayQueryEXT shadow;
    rayQueryInitializeEXT(shadow, u_tlas, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, v_world + n * 0.03, 0.001, l, dist);
    while (rayQueryProceedEXT(shadow)) {}
    float lit = rayQueryGetIntersectionTypeEXT(shadow, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 1.0 : 0.0;
    float open = 0.0;
    for (int i = 0; i < 4; ++i) {
        float a = 1.5707963 * float(i) + 0.4;
        vec3 d = normalize(n + vec3(cos(a), 0.0, sin(a)) * 0.9 + vec3(0.0, 0.5, 0.0));
        rayQueryEXT occ;
        rayQueryInitializeEXT(occ, u_tlas, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, v_world + n * 0.03, 0.001, d, 2.5);
        while (rayQueryProceedEXT(occ)) {}
        open += rayQueryGetIntersectionTypeEXT(occ, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 0.25 : 0.0;
    }
    float ndl = max(dot(n, l), 0.0);
    vec2 cell = floor(v_world.xz * 1.2);
    vec3 base = v_c * (0.85 + 0.15 * mod(cell.x + cell.y, 2.0));
    out_color = vec4(base * (0.1 + 0.35 * open + 0.8 * ndl * lit), 1.0);
}
