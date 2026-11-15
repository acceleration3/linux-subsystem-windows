#version 460
#extension GL_EXT_ray_tracing : require
layout(location = 0) rayPayloadInEXT vec4 payload;
layout(shaderRecordEXT) buffer Record { vec4 color; } rec;
void main() {
    payload = vec4(rec.color.rgb * (0.7 + 0.3 * float(gl_LaunchIDEXT.y) / 64.0), 1.0);
}
