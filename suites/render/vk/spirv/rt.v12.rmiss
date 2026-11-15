#version 460
#extension GL_EXT_ray_tracing : require
layout(location = 0) rayPayloadInEXT vec4 payload;
void main() {
    payload = vec4(0.04, 0.05, 0.09 + 0.1 * float(gl_LaunchIDEXT.y) / 64.0, 1.0);
}
