#pragma once

namespace lsw::suites {
class ClSources {
public:
    static constexpr const char* kVectorAdd =
        "__kernel void lsw_add(__global const uint* a, __global const uint* b, __global uint* c) {\n"
        "    size_t i = get_global_id(0);\n"
        "    c[i] = a[i] + b[i];\n"
        "}\n"
        "__kernel void lsw_addf(__global const float* a, __global const float* b, __global float* c) {\n"
        "    size_t i = get_global_id(0);\n"
        "    c[i] = a[i] + b[i];\n"
        "}\n";

    static constexpr const char* kReduce =
        "__kernel void lsw_reduce(__global const uint* in, __global uint* out, __local uint* scratch, uint n) {\n"
        "    uint lid = (uint)get_local_id(0);\n"
        "    uint size = (uint)get_local_size(0);\n"
        "    size_t gid = get_global_id(0);\n"
        "    scratch[lid] = gid < n ? in[gid] : 0u;\n"
        "    barrier(CLK_LOCAL_MEM_FENCE);\n"
        "    for (uint stride = 1; stride < size; stride *= 2) {\n"
        "        if (lid % (2 * stride) == 0 && lid + stride < size) {\n"
        "            scratch[lid] += scratch[lid + stride];\n"
        "        }\n"
        "        barrier(CLK_LOCAL_MEM_FENCE);\n"
        "    }\n"
        "    if (lid == 0) {\n"
        "        out[get_group_id(0)] = scratch[0];\n"
        "    }\n"
        "}\n";

    static constexpr const char* kStep =
        "__kernel void lsw_step(__global uint* d, uint mul, uint add) {\n"
        "    size_t i = get_global_id(0);\n"
        "    d[i] = d[i] * mul + add;\n"
        "}\n";
};
}  // namespace lsw::suites
