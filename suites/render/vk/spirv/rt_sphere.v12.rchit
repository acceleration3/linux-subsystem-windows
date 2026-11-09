#version 460
#extension GL_EXT_ray_tracing : require
layout(location = 0) rayPayloadInEXT vec4 payload;
hitAttributeEXT vec2 sphere_attr;
void main() {
    vec3 hit = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3 n = normalize(hit - vec3(0.7, 0.5, 0.0));
    payload = vec4(0.5 + 0.5 * n, 1.0);
}
