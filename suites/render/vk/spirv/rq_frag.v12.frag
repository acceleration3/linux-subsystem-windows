#version 460
#extension GL_EXT_ray_query : require
layout(set = 0, binding = 0) uniform accelerationStructureEXT u_tlas;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    vec2 uv = gl_FragCoord.xy / 128.0 * 2.0 - 1.0;
    vec3 origin = vec3(uv.x * 1.6, uv.y * 1.6, 3.0);
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, u_tlas, gl_RayFlagsNoneEXT, 0xFF, origin, 0.01, vec3(0.0, 0.0, -1.0), 20.0);
    while (rayQueryProceedEXT(rq)) {
        if (rayQueryGetIntersectionTypeEXT(rq, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
            rayQueryConfirmIntersectionEXT(rq);
        }
    }
    vec4 color = vec4(0.04, 0.05, 0.09, 1.0);
    if (rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT) {
        int id = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
        vec2 b = rayQueryGetIntersectionBarycentricsEXT(rq, true);
        vec3 base = id == 0 ? vec3(0.9, 0.4, 0.2) : (id == 1 ? vec3(0.2, 0.7, 0.9) : vec3(0.6, 0.9, 0.3));
        color = vec4(base * (0.5 + 0.5 * b.x + 0.2 * b.y), 1.0);
    }
    out_color = color;
}
