#version 460
#extension GL_EXT_ray_tracing : require
layout(location = 0) rayPayloadInEXT vec4 payload;
hitAttributeEXT vec2 attribs;
void main() {
    vec3 base = gl_InstanceCustomIndexEXT == 0 ? vec3(0.95, 0.55, 0.2) : vec3(0.2, 0.5, 0.95);
    payload = vec4(base * (0.6 + 0.3 * attribs.x + 0.1 * attribs.y), 1.0);
}
