#version 460
#extension GL_EXT_ray_tracing : require
layout(location = 0) rayPayloadInEXT vec4 payload;
layout(shaderRecordEXT) buffer Record { vec4 color; vec4 params; } rec;
hitAttributeEXT vec2 attribs;
void main() {
    float shade = rec.params.x + rec.params.y * attribs.x + rec.params.z * attribs.y;
    payload = vec4(rec.color.rgb * shade, 1.0);
}
