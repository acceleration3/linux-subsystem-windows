#include "sanity/cl/kernel_checks.hpp"

#include "sanity/cl/kit.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstring>
#include <set>

namespace lsw::suites::sanity::cl {

namespace {

const char* kAtomics =
    "__kernel void atomics32(__global const int* in, volatile __global int* g, __global int* seen, __global uint* cas_tries) {\n"
    "    size_t i = get_global_id(0);\n"
    "    int v = in[i];\n"
    "    atomic_add(&g[0], v);\n"
    "    atomic_min(&g[1], v);\n"
    "    atomic_max(&g[2], v);\n"
    "    atomic_or(&g[3], v);\n"
    "    atomic_and(&g[4], v);\n"
    "    atomic_xor(&g[5], v);\n"
    "    atomic_inc(&g[6]);\n"
    "    atomic_dec(&g[7]);\n"
    "    int old = g[8];\n"
    "    uint tries = 1;\n"
    "    while (true) {\n"
    "        int seen_value = atomic_cmpxchg(&g[8], old, old + 1);\n"
    "        if (seen_value == old) { break; }\n"
    "        old = seen_value;\n"
    "        ++tries;\n"
    "    }\n"
    "    cas_tries[i] = tries;\n"
    "    seen[i] = atomic_xchg(&g[9], (int)i + 1);\n"
    "}\n"
    "__kernel void atomics_local(__global const int* in, __global int* out) {\n"
    "    __local int l[6];\n"
    "    size_t i = get_global_id(0);\n"
    "    uint lid = (uint)get_local_id(0);\n"
    "    if (lid == 0) { l[0] = 0; l[1] = INT_MAX; l[2] = INT_MIN; l[3] = 0; l[4] = 0; l[5] = 0; }\n"
    "    barrier(CLK_LOCAL_MEM_FENCE);\n"
    "    int v = in[i];\n"
    "    atomic_add(&l[0], v);\n"
    "    atomic_min(&l[1], v);\n"
    "    atomic_max(&l[2], v);\n"
    "    atomic_or(&l[3], v);\n"
    "    atomic_inc(&l[4]);\n"
    "    atomic_xor(&l[5], v);\n"
    "    barrier(CLK_LOCAL_MEM_FENCE);\n"
    "    if (lid == 0) { for (int k = 0; k < 6; ++k) { out[get_group_id(0) * 6 + k] = l[k]; } }\n"
    "}\n";

const char* kAtomics64 =
    "#pragma OPENCL EXTENSION cl_khr_int64_base_atomics : enable\n"
    "#pragma OPENCL EXTENSION cl_khr_int64_extended_atomics : enable\n"
    "__kernel void atomics64(__global const long* in, volatile __global long* g, __global long* seen) {\n"
    "    size_t i = get_global_id(0);\n"
    "    long v = in[i];\n"
    "    atom_add(&g[0], v);\n"
    "    atom_min(&g[1], v);\n"
    "    atom_max(&g[2], v);\n"
    "    atom_or(&g[3], v);\n"
    "    atom_and(&g[4], v);\n"
    "    atom_xor(&g[5], v);\n"
    "    atom_inc(&g[6]);\n"
    "    atom_dec(&g[7]);\n"
    "    long old = g[8];\n"
    "    while (true) {\n"
    "        long now = atom_cmpxchg(&g[8], old, old + 1);\n"
    "        if (now == old) { break; }\n"
    "        old = now;\n"
    "    }\n"
    "    seen[i] = atom_xchg(&g[9], (long)i + 1);\n"
    "}\n";

}  // namespace

Outcome KernelChecks::atomics_vs_cpu(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        const std::size_t count = 1 << 16;
        const std::size_t group = 64;
        ClProgram program;
        if (!Kit::compile(rig, program, std::string("#define INT_MAX 2147483647\n#define INT_MIN (-2147483647 - 1)\n") + kAtomics, "", problems, "atomics kernels")) {
            return problems.outcome();
        }
        ClKernel global = Kit::kernel(program, "atomics32", problems);
        ClKernel local = Kit::kernel(program, "atomics_local", problems);
        if (!global || !local) {
            return problems.outcome();
        }
        const std::vector<std::uint32_t> raw = Kit::words(1700, count);
        std::vector<std::int32_t> values(count);
        for (std::size_t i = 0; i < count; ++i) {
            values[i] = static_cast<std::int32_t>(raw[i]);
        }
        std::int32_t init[10] = {0, INT_MAX, INT_MIN, 0, -1, 0, 0, 0, 0, 0};
        cl_int code = CL_SUCCESS;
        ClOwned<cl_mem> in = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 4, values.data(), code);
        ClOwned<cl_mem> shared = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, sizeof(init), init, code);
        ClOwned<cl_mem> seen = Kit::buffer(rig, CL_MEM_READ_WRITE, count * 4, nullptr, code);
        ClOwned<cl_mem> tries = Kit::buffer(rig, CL_MEM_READ_WRITE, count * 4, nullptr, code);
        if (!in || !shared || !seen || !tries) {
            problems.add("buffers: " + ClTools::error_name(code));
            return problems.outcome();
        }
        const std::size_t global_limit = Kit::group_limit(global, problems, "global atomics kernel");
        const std::size_t local_limit = Kit::group_limit(local, problems, "local atomics kernel");
        if (global_limit == 0 || local_limit == 0) {
            return problems.outcome();
        }
        code = global.set(in, shared, seen, tries);
        if (code == CL_SUCCESS) {
            code = global.run(count, std::min(group, global_limit));
        }
        if (code != CL_SUCCESS) {
            problems.add("the global atomics kernel returned " + ClTools::error_name(code));
        } else {
            std::int32_t result[10] = {};
            Kit::expect(problems, "read of the global counters", Kit::read(rig, shared.get(), result, sizeof(result)), {CL_SUCCESS});
            std::uint32_t sum = 0;
            std::int32_t lowest = INT_MAX;
            std::int32_t highest = INT_MIN;
            std::uint32_t any = 0;
            std::uint32_t all = 0xFFFFFFFFu;
            std::uint32_t parity = 0;
            for (const std::int32_t v : values) {
                sum += static_cast<std::uint32_t>(v);
                lowest = std::min(lowest, v);
                highest = std::max(highest, v);
                any |= static_cast<std::uint32_t>(v);
                all &= static_cast<std::uint32_t>(v);
                parity ^= static_cast<std::uint32_t>(v);
            }
            const struct {
                const char* name;
                std::int32_t want;
                std::int32_t got;
            } checks[] = {{"atomic_add", static_cast<std::int32_t>(sum), result[0]},
                          {"atomic_min", lowest, result[1]},
                          {"atomic_max", highest, result[2]},
                          {"atomic_or", static_cast<std::int32_t>(any), result[3]},
                          {"atomic_and", static_cast<std::int32_t>(all), result[4]},
                          {"atomic_xor", static_cast<std::int32_t>(parity), result[5]},
                          {"atomic_inc", static_cast<std::int32_t>(count), result[6]},
                          {"atomic_dec", -static_cast<std::int32_t>(count), result[7]},
                          {"atomic_cmpxchg increment loop", static_cast<std::int32_t>(count), result[8]}};
            for (const auto& entry : checks) {
                if (entry.want != entry.got) {
                    problems.add(std::string("global ") + entry.name + " gives " + std::to_string(entry.got) + ", expected " + std::to_string(entry.want) + " from " + std::to_string(count) + " work-items");
                }
            }
            std::vector<std::int32_t> olds(count);
            Kit::expect(problems, "read of the exchanged values", Kit::read(rig, seen.get(), olds.data(), count * 4), {CL_SUCCESS});
            std::set<std::int32_t> distinct(olds.begin(), olds.end());
            distinct.insert(result[9]);
            if (distinct.size() != count + 1 || *distinct.begin() != 0 || *distinct.rbegin() != static_cast<std::int32_t>(count)) {
                problems.add("atomic_xchg did not hand every value to exactly one work-item (" + std::to_string(distinct.size()) + " distinct values of " + std::to_string(count + 1) + ")");
            }
        }
        {
            const std::size_t local_group = std::min(group, local_limit);
            const std::size_t groups = count / local_group;
            const std::size_t used = groups * local_group;
            ClOwned<cl_mem> out = Kit::buffer(rig, CL_MEM_WRITE_ONLY, groups * 6 * 4, problems, "local atomics output");
            if (out && local.set(in, out) == CL_SUCCESS) {
                code = local.run(used, local_group);
                if (code != CL_SUCCESS) {
                    problems.add("the local atomics kernel returned " + ClTools::error_name(code));
                } else {
                    std::vector<std::int32_t> got(groups * 6);
                    Kit::expect(problems, "read of the local results", Kit::read(rig, out.get(), got.data(), got.size() * 4), {CL_SUCCESS});
                    std::vector<std::int32_t> want(groups * 6);
                    for (std::size_t g = 0; g < groups; ++g) {
                        std::uint32_t sum = 0;
                        std::uint32_t any = 0;
                        std::uint32_t parity = 0;
                        std::int32_t lowest = INT_MAX;
                        std::int32_t highest = INT_MIN;
                        for (std::size_t k = 0; k < local_group; ++k) {
                            const std::int32_t v = values[g * local_group + k];
                            sum += static_cast<std::uint32_t>(v);
                            any |= static_cast<std::uint32_t>(v);
                            parity ^= static_cast<std::uint32_t>(v);
                            lowest = std::min(lowest, v);
                            highest = std::max(highest, v);
                        }
                        const std::int32_t row[6] = {static_cast<std::int32_t>(sum), lowest, highest, static_cast<std::int32_t>(any), static_cast<std::int32_t>(local_group), static_cast<std::int32_t>(parity)};
                        std::copy(row, row + 6, want.begin() + static_cast<std::ptrdiff_t>(g * 6));
                    }
                    if (got != want) {
                        std::size_t first = 0;
                        while (first < got.size() && got[first] == want[first]) {
                            ++first;
                        }
                        problems.add("local atomics differ from the CPU totals: group " + std::to_string(first / 6) + " counter " + std::to_string(first % 6) + " is " + std::to_string(got[first]) + ", expected " + std::to_string(want[first]));
                    }
                }
            }
        }
        if (rig.has_extension("cl_khr_int64_base_atomics") && rig.has_extension("cl_khr_int64_extended_atomics")) {
            ClProgram program64;
            if (Kit::compile(rig, program64, kAtomics64, "", problems, "64-bit atomics kernel")) {
                ClKernel kernel64 = Kit::kernel(program64, "atomics64", problems);
                const std::size_t n64 = 1 << 14;
                std::vector<std::int64_t> v64(n64);
                const std::vector<std::uint32_t> lo = Kit::words(1701, n64);
                const std::vector<std::uint32_t> hi = Kit::words(1702, n64);
                for (std::size_t i = 0; i < n64; ++i) {
                    v64[i] = static_cast<std::int64_t>((static_cast<std::uint64_t>(hi[i]) << 32) | lo[i]);
                }
                std::int64_t init64[10] = {0, LLONG_MAX, LLONG_MIN, 0, -1, 0, 0, 0, 0, 0};
                ClOwned<cl_mem> in64 = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, n64 * 8, v64.data(), code);
                ClOwned<cl_mem> shared64 = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, sizeof(init64), init64, code);
                ClOwned<cl_mem> seen64 = Kit::buffer(rig, CL_MEM_READ_WRITE, n64 * 8, nullptr, code);
                if (kernel64 && in64 && shared64 && seen64 && kernel64.set(in64, shared64, seen64) == CL_SUCCESS) {
                    code = kernel64.run(n64);
                    if (code != CL_SUCCESS) {
                        problems.add("the 64-bit atomics kernel returned " + ClTools::error_name(code));
                    } else {
                        std::int64_t got[10] = {};
                        Kit::expect(problems, "read of the 64-bit counters", Kit::read(rig, shared64.get(), got, sizeof(got)), {CL_SUCCESS});
                        std::uint64_t sum = 0;
                        std::int64_t lowest = LLONG_MAX;
                        std::int64_t highest = LLONG_MIN;
                        std::uint64_t any = 0;
                        std::uint64_t all = ~0ull;
                        std::uint64_t parity = 0;
                        for (const std::int64_t v : v64) {
                            sum += static_cast<std::uint64_t>(v);
                            lowest = std::min(lowest, v);
                            highest = std::max(highest, v);
                            any |= static_cast<std::uint64_t>(v);
                            all &= static_cast<std::uint64_t>(v);
                            parity ^= static_cast<std::uint64_t>(v);
                        }
                        const struct {
                            const char* name;
                            std::int64_t want;
                        } wants[] = {{"atom_add", static_cast<std::int64_t>(sum)}, {"atom_min", lowest}, {"atom_max", highest}, {"atom_or", static_cast<std::int64_t>(any)},
                                     {"atom_and", static_cast<std::int64_t>(all)}, {"atom_xor", static_cast<std::int64_t>(parity)}, {"atom_inc", static_cast<std::int64_t>(n64)},
                                     {"atom_dec", -static_cast<std::int64_t>(n64)}, {"atom_cmpxchg loop", static_cast<std::int64_t>(n64)}};
                        for (int k = 0; k < 9; ++k) {
                            if (got[k] != wants[k].want) {
                                problems.add(std::string("64-bit ") + wants[k].name + " gives " + std::to_string(got[k]) + ", expected " + std::to_string(wants[k].want));
                            }
                        }
                        std::vector<std::int64_t> olds(n64);
                        Kit::expect(problems, "read of the 64-bit exchanges", Kit::read(rig, seen64.get(), olds.data(), n64 * 8), {CL_SUCCESS});
                        std::set<std::int64_t> distinct(olds.begin(), olds.end());
                        distinct.insert(got[9]);
                        if (distinct.size() != n64 + 1) {
                            problems.add("64-bit atom_xchg did not hand every value to exactly one work-item");
                        }
                    }
                }
            }
        } else {
            context.measure("cl.int64_atomics", "not offered");
        }
        return problems.outcome();
    });
}

Outcome KernelChecks::local_memory_barrier(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        const char* source =
            "__kernel void lsw_scan(__global const uint* in, __global uint* out, __local uint* a, __local uint* b, uint n) {\n"
            "    uint lid = (uint)get_local_id(0);\n"
            "    uint size = (uint)get_local_size(0);\n"
            "    size_t gid = get_global_id(0);\n"
            "    a[lid] = gid < n ? in[gid] : 0u;\n"
            "    barrier(CLK_LOCAL_MEM_FENCE);\n"
            "    __local uint* src = a;\n"
            "    __local uint* dst = b;\n"
            "    for (uint off = 1; off < size; off <<= 1) {\n"
            "        dst[lid] = lid >= off ? src[lid] + src[lid - off] : src[lid];\n"
            "        barrier(CLK_LOCAL_MEM_FENCE);\n"
            "        __local uint* t = src; src = dst; dst = t;\n"
            "    }\n"
            "    if (gid < n) { out[gid] = src[lid]; }\n"
            "}\n"
            "__kernel void lsw_transpose(__global const uint* in, __global uint* out, __local uint* tile, uint width, uint height) {\n"
            "    uint T = (uint)get_local_size(0);\n"
            "    uint lx = (uint)get_local_id(0), ly = (uint)get_local_id(1);\n"
            "    uint x = (uint)get_global_id(0), y = (uint)get_global_id(1);\n"
            "    tile[ly * T + lx] = in[y * width + x];\n"
            "    barrier(CLK_LOCAL_MEM_FENCE);\n"
            "    uint ty = (uint)get_group_id(0) * T + ly;\n"
            "    uint tx = (uint)get_group_id(1) * T + lx;\n"
            "    out[ty * height + tx] = tile[lx * T + ly];\n"
            "}\n"
            "__kernel void lsw_rotate(__global uint* data, __global uint* out) {\n"
            "    size_t gid = get_global_id(0);\n"
            "    uint lid = (uint)get_local_id(0);\n"
            "    uint size = (uint)get_local_size(0);\n"
            "    size_t base = get_group_id(0) * size;\n"
            "    data[gid] = (uint)gid * 3u + 1u;\n"
            "    barrier(CLK_GLOBAL_MEM_FENCE);\n"
            "    out[gid] = data[base + (lid + 1) % size];\n"
            "}\n";
        ClProgram program;
        if (!Kit::compile(rig, program, source, "", problems, "barrier kernels")) {
            return problems.outcome();
        }
        ClKernel scan = Kit::kernel(program, "lsw_scan", problems);
        ClKernel transpose = Kit::kernel(program, "lsw_transpose", problems);
        ClKernel rotate = Kit::kernel(program, "lsw_rotate", problems);
        if (!scan || !transpose || !rotate) {
            return problems.outcome();
        }
        std::size_t sizes[3] = {1, 1, 1};
        rig.api.GetDeviceInfo(rig.device, CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(sizes), sizes, nullptr);
        const std::size_t limit = std::min({scan.work_group_size(), transpose.work_group_size(), rotate.work_group_size(), sizes[0]});
        std::size_t exercised = 0;
        for (const std::size_t group : {std::size_t(1), std::size_t(2), std::size_t(3), std::size_t(5), std::size_t(7), std::size_t(12), std::size_t(17), std::size_t(31), std::size_t(32), std::size_t(33), std::size_t(48), std::size_t(64), std::size_t(100), std::size_t(127), std::size_t(128), std::size_t(255), std::size_t(256)}) {
            if (group > limit) {
                continue;
            }
            ++exercised;
            const std::string tag = "work-group size " + std::to_string(group);
            Kit::Pair reduced;
            if (Kit::reduction(rig, group * 9 + 5, group, 1800 + group, reduced, problems, tag + " reduction")) {
                Kit::same_words(context, problems, tag + " reduction", reduced.expected, reduced.actual);
            }
            const std::size_t count = group * 6 + 1;
            const std::size_t padded = (count + group - 1) / group * group;
            const std::vector<std::uint32_t> input = Kit::words(1900 + group, count);
            cl_int code = CL_SUCCESS;
            ClOwned<cl_mem> in = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 4, const_cast<std::uint32_t*>(input.data()), code);
            ClOwned<cl_mem> out = Kit::buffer(rig, CL_MEM_WRITE_ONLY, count * 4, nullptr, code);
            const cl_uint n = static_cast<cl_uint>(count);
            if (in && out && scan.set(in, out, ClLocal{group * 4}, ClLocal{group * 4}, n) == CL_SUCCESS) {
                code = scan.run(padded, group);
                if (code != CL_SUCCESS) {
                    problems.add(tag + " scan returned " + ClTools::error_name(code));
                } else {
                    std::vector<std::uint32_t> expected(count);
                    for (std::size_t i = 0; i < count; ++i) {
                        expected[i] = input[i] + (i % group != 0 ? expected[i - 1] : 0u);
                    }
                    std::vector<std::uint32_t> actual;
                    if (Kit::read_words(rig, out.get(), count, actual, problems, tag + " scan")) {
                        Kit::same_words(context, problems, tag + " inclusive scan", expected, actual);
                    }
                }
            }
            const std::size_t items = group * 4;
            ClOwned<cl_mem> data = Kit::buffer(rig, CL_MEM_READ_WRITE, items * 4, nullptr, code);
            ClOwned<cl_mem> rotated = Kit::buffer(rig, CL_MEM_WRITE_ONLY, items * 4, nullptr, code);
            if (data && rotated && rotate.set(data, rotated) == CL_SUCCESS) {
                code = rotate.run(items, group);
                if (code != CL_SUCCESS) {
                    problems.add(tag + " global-fence rotate returned " + ClTools::error_name(code));
                } else {
                    std::vector<std::uint32_t> expected(items);
                    for (std::size_t i = 0; i < items; ++i) {
                        const std::size_t base = i / group * group;
                        expected[i] = static_cast<std::uint32_t>((base + (i % group + 1) % group) * 3 + 1);
                    }
                    std::vector<std::uint32_t> actual;
                    if (Kit::read_words(rig, rotated.get(), items, actual, problems, tag + " rotate")) {
                        Kit::same_words(context, problems, tag + " CLK_GLOBAL_MEM_FENCE exchange", expected, actual);
                    }
                }
            }
        }
        for (const std::size_t tile : {std::size_t(2), std::size_t(3), std::size_t(5), std::size_t(8), std::size_t(16)}) {
            if (tile * tile > transpose.work_group_size() || tile > sizes[0] || tile > sizes[1]) {
                continue;
            }
            const std::size_t width = tile * 7;
            const std::size_t height = tile * 4;
            const std::vector<std::uint32_t> input = Kit::words(2000 + tile, width * height);
            cl_int code = CL_SUCCESS;
            ClOwned<cl_mem> in = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, input.size() * 4, const_cast<std::uint32_t*>(input.data()), code);
            ClOwned<cl_mem> out = Kit::buffer(rig, CL_MEM_WRITE_ONLY, input.size() * 4, nullptr, code);
            const cl_uint w = static_cast<cl_uint>(width);
            const cl_uint h = static_cast<cl_uint>(height);
            if (!in || !out || transpose.set(in, out, ClLocal{tile * tile * 4}, w, h) != CL_SUCCESS) {
                problems.add("transpose setup for tile " + std::to_string(tile) + " failed");
                continue;
            }
            const std::size_t global[2] = {width, height};
            const std::size_t group[2] = {tile, tile};
            code = transpose.enqueue(rig.queue, 2, nullptr, global, group);
            if (code != CL_SUCCESS) {
                problems.add("transpose with " + std::to_string(tile) + "x" + std::to_string(tile) + " tiles returned " + ClTools::error_name(code));
                continue;
            }
            std::vector<std::uint32_t> expected(input.size());
            for (std::size_t y = 0; y < height; ++y) {
                for (std::size_t x = 0; x < width; ++x) {
                    expected[x * height + y] = input[y * width + x];
                }
            }
            std::vector<std::uint32_t> actual;
            if (Kit::read_words(rig, out.get(), input.size(), actual, problems, "transpose")) {
                Kit::same_words(context, problems, "transpose with " + std::to_string(tile) + "x" + std::to_string(tile) + " tiles", expected, actual);
            }
        }
        context.measure("cl.group_sizes_exercised", exercised);
        return problems.outcome();
    });
}

Outcome KernelChecks::args_and_ndrange(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        const char* source =
            "__kernel void lsw_cover(volatile __global uint* hits, uint w, uint h) {\n"
            "    size_t x = get_global_id(0) - get_global_offset(0);\n"
            "    size_t y = get_global_id(1) - get_global_offset(1);\n"
            "    size_t z = get_global_id(2) - get_global_offset(2);\n"
            "    atomic_inc(&hits[x + w * (y + h * z)]);\n"
            "}\n"
            "__kernel void lsw_args(__global uint* out, uint a, float b, __local uint* tmp) {\n"
            "    out[get_global_id(0)] = a + (uint)b;\n"
            "}\n"
            "__attribute__((reqd_work_group_size(4, 1, 1)))\n"
            "__kernel void lsw_reqd(__global uint* out) { out[get_global_id(0)] = (uint)get_local_size(0); }\n";
        const std::string options = rig.version.at_least(1, 2) ? "-cl-std=CL1.2 -cl-kernel-arg-info" : "";
        ClProgram program;
        if (!Kit::compile(rig, program, source, options, problems, "ND-range kernels")) {
            return problems.outcome();
        }
        ClKernel cover = Kit::kernel(program, "lsw_cover", problems);
        ClKernel args = Kit::kernel(program, "lsw_args", problems);
        ClKernel reqd = Kit::kernel(program, "lsw_reqd", problems);
        if (!cover || !args || !reqd) {
            return problems.outcome();
        }
        cl_int code = CL_SUCCESS;
        auto exactly_once = [&](const std::string& tag, cl_uint dims, const std::size_t* offset, const std::size_t* global, const std::size_t* local, ClKernel& kernel) {
            std::size_t total = 1;
            for (cl_uint d = 0; d < dims; ++d) {
                total *= global[d];
            }
            std::vector<std::uint32_t> zeros(total, 0);
            ClOwned<cl_mem> hits = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, total * 4, zeros.data(), code);
            const cl_uint w = static_cast<cl_uint>(global[0]);
            const cl_uint h = dims > 1 ? static_cast<cl_uint>(global[1]) : 1u;
            if (!hits || kernel.set(hits, w, h) != CL_SUCCESS) {
                problems.add(tag + ": setup failed");
                return;
            }
            const cl_int result = kernel.enqueue(rig.queue, dims, offset, global, local);
            if (result != CL_SUCCESS) {
                problems.add(tag + ": enqueue returned " + ClTools::error_name(result));
                return;
            }
            std::vector<std::uint32_t> got;
            if (Kit::read_words(rig, hits.get(), total, got, problems, tag)) {
                std::size_t wrong = 0;
                std::size_t first = 0;
                for (std::size_t i = 0; i < total; ++i) {
                    if (got[i] != 1) {
                        if (wrong++ == 0) {
                            first = i;
                        }
                    }
                }
                if (wrong != 0) {
                    problems.add(tag + ": " + std::to_string(wrong) + " of " + std::to_string(total) + " work-items did not run exactly once, first at " + std::to_string(first) + " (ran " + std::to_string(got[first]) + " times)");
                }
            }
        };
        {
            const std::size_t g1[1] = {1000};
            exactly_once("1D range", 1, nullptr, g1, nullptr, cover);
            const std::size_t offset[1] = {37};
            const std::size_t g2[1] = {100};
            exactly_once("1D range with a global offset", 1, offset, g2, nullptr, cover);
            const std::size_t g3[2] = {33, 7};
            exactly_once("2D range", 2, nullptr, g3, nullptr, cover);
            const std::size_t g4[3] = {5, 6, 7};
            const std::size_t offset3[3] = {11, 0, 3};
            exactly_once("3D range with an offset", 3, offset3, g4, nullptr, cover);
            const std::size_t g5[3] = {4, 6, 7};
            const std::size_t l5[3] = {2, 3, 1};
            if (cover.work_group_size() >= 6) {
                exactly_once("3D range with a 2x3x1 work-group", 3, nullptr, g5, l5, cover);
            }
        }
        bool non_uniform = false;
        if (rig.version.at_least(3, 0)) {
            cl_bool supported = CL_FALSE;
            rig.api.GetDeviceInfo(rig.device, CL_DEVICE_NON_UNIFORM_WORK_GROUP_SUPPORT, sizeof(supported), &supported, nullptr);
            non_uniform = supported != CL_FALSE;
        } else {
            non_uniform = rig.version.at_least(2, 0);
        }
        if (non_uniform) {
            ClProgram modern;
            std::string error;
            if (!modern.build(rig, source, rig.version.at_least(3, 0) ? "-cl-std=CL3.0" : "-cl-std=CL2.0", error)) {
                problems.add("non-uniform work-groups are supported but the OpenCL C 2.0+ program does not build: " + error);
            } else {
                ClKernel modern_cover = Kit::kernel(modern, "lsw_cover", problems);
                const std::size_t group = std::min<std::size_t>(64, modern_cover.work_group_size());
                const std::size_t g[1] = {group * 5 + group / 2 + 1};
                const std::size_t l[1] = {group};
                if (modern_cover && group > 1) {
                    exactly_once("non-uniform 1D range", 1, nullptr, g, l, modern_cover);
                }
            }
        } else {
            context.measure("cl.non_uniform_work_groups", "not supported");
        }
        {
            const std::size_t uneven[1] = {cover.work_group_size() * 2 + 1};
            const std::size_t local[1] = {cover.work_group_size()};
            if (local[0] >= 2) {
                ClOwned<cl_mem> hits = Kit::buffer(rig, CL_MEM_READ_WRITE, uneven[0] * 4, problems, "uneven hits");
                if (hits && cover.set(hits, cl_uint(1), cl_uint(1)) == CL_SUCCESS) {
                    Kit::expect(problems, "a global size that is not a multiple of the local size in OpenCL C 1.2", cover.enqueue(rig.queue, 1, nullptr, uneven, local), {CL_INVALID_WORK_GROUP_SIZE});
                }
            }
        }
        {
            cl_uint num_args = 0;
            rig.api.GetKernelInfo(args.get(), CL_KERNEL_NUM_ARGS, sizeof(num_args), &num_args, nullptr);
            if (num_args != 4) {
                problems.add("CL_KERNEL_NUM_ARGS is " + std::to_string(num_args) + ", expected 4");
            }
            char name[64] = {};
            rig.api.GetKernelInfo(args.get(), CL_KERNEL_FUNCTION_NAME, sizeof(name), name, nullptr);
            if (std::string(name) != "lsw_args") {
                problems.add(std::string("CL_KERNEL_FUNCTION_NAME is '") + name + "'");
            }
            cl_context owner = nullptr;
            rig.api.GetKernelInfo(args.get(), CL_KERNEL_CONTEXT, sizeof(owner), &owner, nullptr);
            if (owner != rig.context) {
                problems.add("CL_KERNEL_CONTEXT is not the context the kernel was created in");
            }
            ClOwned<cl_mem> out = Kit::buffer(rig, CL_MEM_READ_WRITE, 256, problems, "args output");
            const cl_mem handle = out.get();
            cl_uint scalar = 7;
            float real = 3.0f;
            Kit::expect(problems, "clSetKernelArg with an index past the last argument", rig.api.SetKernelArg(args.get(), 99, sizeof(handle), &handle), {CL_INVALID_ARG_INDEX});
            Kit::expect(problems, "clSetKernelArg with a 2-byte value for a uint", rig.api.SetKernelArg(args.get(), 1, 2, &scalar), {CL_INVALID_ARG_SIZE});
            Kit::expect(problems, "clSetKernelArg with an 8-byte value for a uint", rig.api.SetKernelArg(args.get(), 1, 8, &scalar), {CL_INVALID_ARG_SIZE});
            Kit::expect(problems, "clSetKernelArg with a 1-byte value for a buffer", rig.api.SetKernelArg(args.get(), 0, 1, &handle), {CL_INVALID_ARG_SIZE});
            Kit::expect(problems, "clSetKernelArg with a NULL value for a uint", rig.api.SetKernelArg(args.get(), 1, sizeof(scalar), nullptr), {CL_INVALID_ARG_VALUE});
            Kit::expect(problems, "clEnqueueNDRangeKernel with no argument set", args.run(16), {CL_INVALID_KERNEL_ARGS});
            Kit::expect(problems, "buffer argument", rig.api.SetKernelArg(args.get(), 0, sizeof(handle), &handle), {CL_SUCCESS});
            Kit::expect(problems, "clEnqueueNDRangeKernel with only the first argument set", args.run(16), {CL_INVALID_KERNEL_ARGS});
            Kit::expect(problems, "scalar argument", rig.api.SetKernelArg(args.get(), 1, sizeof(scalar), &scalar), {CL_SUCCESS});
            Kit::expect(problems, "float argument", rig.api.SetKernelArg(args.get(), 2, sizeof(real), &real), {CL_SUCCESS});
            Kit::expect(problems, "clEnqueueNDRangeKernel with the __local argument unset", args.run(16), {CL_INVALID_KERNEL_ARGS});
            Kit::expect(problems, "local argument", rig.api.SetKernelArg(args.get(), 3, 64, nullptr), {CL_SUCCESS});
            Kit::expect(problems, "clEnqueueNDRangeKernel with every argument set", args.run(16), {CL_SUCCESS});
            std::vector<std::uint32_t> got;
            if (Kit::read_words(rig, out.get(), 16, got, problems, "args kernel")) {
                for (std::size_t i = 0; i < 16; ++i) {
                    if (got[i] != 10) {
                        problems.add("the kernel with every argument set wrote " + std::to_string(got[i]) + " at " + std::to_string(i) + ", expected 10");
                        break;
                    }
                }
            }
            const cl_mem none = nullptr;
            Kit::expect(problems, "clSetKernelArg with a NULL buffer", rig.api.SetKernelArg(args.get(), 0, sizeof(none), &none), {CL_SUCCESS});
            if (rig.version.at_least(1, 2)) {
                char arg_name[64] = {};
                Kit::expect(problems, "CL_KERNEL_ARG_NAME", rig.api.GetKernelArgInfo(args.get(), 1, CL_KERNEL_ARG_NAME, sizeof(arg_name), arg_name, nullptr), {CL_SUCCESS});
                if (std::string(arg_name) != "a") {
                    problems.add(std::string("CL_KERNEL_ARG_NAME of argument 1 is '") + arg_name + "', expected 'a'");
                }
                cl_kernel_arg_address_qualifier qualifier = 0;
                Kit::expect(problems, "CL_KERNEL_ARG_ADDRESS_QUALIFIER", rig.api.GetKernelArgInfo(args.get(), 3, CL_KERNEL_ARG_ADDRESS_QUALIFIER, sizeof(qualifier), &qualifier, nullptr), {CL_SUCCESS});
                if (qualifier != CL_KERNEL_ARG_ADDRESS_LOCAL) {
                    problems.add("argument 3 is not reported as __local");
                }
                Kit::expect(problems, "CL_KERNEL_ARG_NAME with an index past the end", rig.api.GetKernelArgInfo(args.get(), 9, CL_KERNEL_ARG_NAME, sizeof(arg_name), arg_name, nullptr), {CL_INVALID_ARG_INDEX});
                ClProgram plain;
                if (Kit::compile(rig, plain, source, "-cl-std=CL1.2", problems, "program without arg info")) {
                    ClKernel without = Kit::kernel(plain, "lsw_args", problems);
                    if (without) {
                        Kit::expect(problems, "clGetKernelArgInfo without -cl-kernel-arg-info", rig.api.GetKernelArgInfo(without.get(), 0, CL_KERNEL_ARG_NAME, sizeof(arg_name), arg_name, nullptr), {CL_KERNEL_ARG_INFO_NOT_AVAILABLE});
                    }
                }
            }
        }
        {
            ClOwned<cl_mem> out = Kit::buffer(rig, CL_MEM_READ_WRITE, 256, problems, "reqd output");
            if (out && reqd.set(out) == CL_SUCCESS) {
                const std::size_t global[1] = {16};
                const std::size_t wrong[1] = {2};
                const std::size_t right[1] = {4};
                Kit::expect(problems, "a local size that differs from reqd_work_group_size", reqd.enqueue(rig.queue, 1, nullptr, global, wrong), {CL_INVALID_WORK_GROUP_SIZE});
                Kit::expect(problems, "the local size named by reqd_work_group_size", reqd.enqueue(rig.queue, 1, nullptr, global, right), {CL_SUCCESS});
                std::size_t compile_size[3] = {};
                rig.api.GetKernelWorkGroupInfo(reqd.get(), rig.device, CL_KERNEL_COMPILE_WORK_GROUP_SIZE, sizeof(compile_size), compile_size, nullptr);
                if (compile_size[0] != 4 || compile_size[1] != 1 || compile_size[2] != 1) {
                    problems.add("CL_KERNEL_COMPILE_WORK_GROUP_SIZE is (" + std::to_string(compile_size[0]) + ", " + std::to_string(compile_size[1]) + ", " + std::to_string(compile_size[2]) + "), expected (4, 1, 1)");
                }
            }
        }
        return problems.outcome();
    });
}

Outcome KernelChecks::large_workloads(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        const char* source =
            "__kernel void lsw_big(__global uint* d) { size_t i = get_global_id(0); d[i] = (uint)i * 3u + 1u; }\n"
            "__kernel void lsw_lcg(__global uint* d, uint rounds) {\n"
            "    size_t i = get_global_id(0);\n"
            "    uint x = d[i];\n"
            "    for (uint r = 0; r < rounds; ++r) { x = x * 1664525u + 1013904223u; }\n"
            "    d[i] = x;\n"
            "}\n";
        ClProgram program;
        if (!Kit::compile(rig, program, source, "", problems, "large workload kernels")) {
            return problems.outcome();
        }
        ClKernel big = Kit::kernel(program, "lsw_big", problems);
        ClKernel lcg = Kit::kernel(program, "lsw_lcg", problems);
        if (!big || !lcg) {
            return problems.outcome();
        }
        {
            const std::uint64_t budget = std::min<std::uint64_t>({rig.max_allocation(), rig.global_memory() / 8, sizeof(void*) == 4 ? (64ull << 20) : (128ull << 20)});
            std::size_t items = static_cast<std::size_t>(budget / 4);
            if (sizeof(void*) == 8) {
                items = std::min<std::size_t>(items, std::size_t(1) << 25);
            }
            ClOwned<cl_mem> buffer = Kit::buffer(rig, CL_MEM_READ_WRITE, items * 4, problems, "large buffer");
            if (buffer && big.set(buffer) == CL_SUCCESS) {
                const auto begin = std::chrono::steady_clock::now();
                const cl_int code = big.run(items);
                if (code != CL_SUCCESS) {
                    problems.add("a kernel over " + std::to_string(items) + " work-items returned " + ClTools::error_name(code));
                } else {
                    std::vector<std::uint32_t> got(items);
                    const cl_int read_code = Kit::read(rig, buffer.get(), got.data(), items * 4);
                    if (read_code != CL_SUCCESS) {
                        problems.add("reading " + std::to_string(items) + " results returned " + ClTools::error_name(read_code));
                    } else {
                        std::size_t wrong = 0;
                        std::size_t first = 0;
                        for (std::size_t i = 0; i < items; ++i) {
                            if (got[i] != static_cast<std::uint32_t>(i) * 3u + 1u) {
                                if (wrong++ == 0) {
                                    first = i;
                                }
                            }
                        }
                        if (wrong != 0) {
                            problems.add(std::to_string(wrong) + " of " + std::to_string(items) + " results are wrong, first at " + std::to_string(first) + " (read " + std::to_string(got[first]) + ")");
                        }
                    }
                }
                context.measure("cl.large_items", items);
                context.measure("cl.large_ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count());
            }
        }
        {
            const std::size_t items = 1 << 18;
            const std::vector<std::uint32_t> seed = Kit::words(2100, items);
            cl_int code = CL_SUCCESS;
            ClOwned<cl_mem> state = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, items * 4, const_cast<std::uint32_t*>(seed.data()), code);
            if (!state || lcg.set(state, cl_uint(1)) != CL_SUCCESS) {
                problems.add("long-running buffer: " + ClTools::error_name(code));
                return problems.outcome();
            }
            using Clock = std::chrono::steady_clock;
            cl_uint rounds = 256;
            std::uint64_t total_rounds = 0;
            auto timed = [&](cl_uint r) {
                (void)lcg.set(state, r);
                const auto begin = Clock::now();
                const cl_int result = lcg.run(items);
                const cl_int finished = result == CL_SUCCESS ? rig.api.Finish(rig.queue) : result;
                total_rounds += r;
                return std::pair<cl_int, double>{finished, std::chrono::duration<double, std::milli>(Clock::now() - begin).count()};
            };
            auto probe = timed(rounds);
            if (probe.first != CL_SUCCESS) {
                problems.add("the calibration dispatch returned " + ClTools::error_name(probe.first));
                return problems.outcome();
            }
            const double per_round = std::max(probe.second, 0.01) / rounds;
            rounds = static_cast<cl_uint>(std::clamp(20.0 / per_round, 64.0, 1048576.0));
            const auto start = Clock::now();
            std::size_t dispatches = 0;
            double slowest = 0;
            while (Clock::now() - start < std::chrono::seconds(3)) {
                const auto step = timed(rounds);
                if (step.first != CL_SUCCESS) {
                    problems.add("dispatch " + std::to_string(dispatches) + " of the sustained run returned " + ClTools::error_name(step.first));
                    break;
                }
                slowest = std::max(slowest, step.second);
                ++dispatches;
            }
            std::uint32_t mul = 1;
            std::uint32_t add = 0;
            std::uint32_t base_mul = 1664525u;
            std::uint32_t base_add = 1013904223u;
            for (std::uint64_t n = total_rounds; n != 0; n >>= 1) {
                if ((n & 1) != 0) {
                    add = add * base_mul + base_add;
                    mul *= base_mul;
                }
                base_add = base_add * base_mul + base_add;
                base_mul *= base_mul;
            }
            std::vector<std::uint32_t> expected(items);
            for (std::size_t i = 0; i < items; ++i) {
                expected[i] = seed[i] * mul + add;
            }
            std::vector<std::uint32_t> actual;
            if (Kit::read_words(rig, state.get(), items, actual, problems, "sustained run")) {
                Kit::same_words(context, problems, "results after " + std::to_string(dispatches) + " dispatches (" + std::to_string(total_rounds) + " rounds)", expected, actual);
            }
            context.measure("cl.sustained_dispatches", dispatches);
            context.measure("cl.sustained_slowest_dispatch_ms", slowest);
        }
        return problems.outcome();
    });
}

}  // namespace lsw::suites::sanity::cl
