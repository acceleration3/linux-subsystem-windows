#version 450
#extension GL_ARB_gpu_shader_int64 : require
layout(location = 0) in vec2 in_pos;
layout(location = 1) in int64_t in_attr;
layout(location = 0) out vec4 v_color;
void main() {
    gl_Position = vec4(in_pos, 0.0, 1.0);
    gl_PointSize = 12.0;
    int hi = int(in_attr >> 33);
    uint lo = uint(in_attr & int64_t(15));
    uint bit31 = uint((in_attr >> 31) & int64_t(1));
    uint bit32 = uint((in_attr >> 32) & int64_t(1));
    v_color = vec4(float(-hi - 1) / 15.0, float(lo) / 15.0, float(bit31) * 0.5 + float(bit32) * 0.25, 1.0);
}
