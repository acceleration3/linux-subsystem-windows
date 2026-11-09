#version 450
#extension GL_ARB_gpu_shader_int64 : require
layout(location = 0) in vec2 in_pos;
layout(location = 1) in uint64_t in_attr;
layout(location = 0) out vec4 v_color;
void main() {
    gl_Position = vec4(in_pos, 0.0, 1.0);
    gl_PointSize = 12.0;
    uint hi = uint(in_attr >> 33);
    uint lo = uint(in_attr & uint64_t(15));
    uint bit31 = uint((in_attr >> 31) & uint64_t(1));
    uint bit32 = uint((in_attr >> 32) & uint64_t(1));
    uint mid = uint((in_attr >> 8) & uint64_t(15));
    v_color = vec4(float(hi) / 15.0, float(lo) / 15.0, float(bit31) * 0.5 + float(bit32) * 0.25 + float(mid) / 60.0, 1.0);
}
