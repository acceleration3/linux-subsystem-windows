#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require
layout(set = 0, binding = 0) uniform accelerationStructureEXT u_tlas;
layout(location = 0) rayPayloadInEXT vec4 payload;
layout(location = 1) rayPayloadEXT float shadowed;
layout(shaderRecordEXT) buffer Record { vec4 color; } rec;
hitAttributeEXT vec2 attribs;
layout(location = 0) callableDataEXT vec4 shade;
void main() {
    vec3 hit = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    shadowed = 1.0;
    traceRayEXT(u_tlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, hit + vec3(0.0, 0.0, 0.02), 0.01,
                normalize(vec3(0.5, 0.4, 1.0)), 10.0, 1);
    shade = vec4(attribs, float(gl_InstanceCustomIndexEXT), shadowed);
    executeCallableEXT(0, 0);
    payload = vec4(rec.color.rgb * shade.rgb * (0.4 + 0.6 * shadowed), 1.0);
}
