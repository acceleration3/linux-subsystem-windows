#include "sanity/cl/mem_checks.hpp"

#include "sanity/cl/aligned_block.hpp"
#include "sanity/cl/kit.hpp"

#include <algorithm>

namespace lsw::suites::sanity::cl {

namespace {

void* map_blocking(ClRig& rig, cl_mem mem, cl_map_flags flags, std::size_t offset, std::size_t size, cl_int& code) {
    return rig.api.EnqueueMapBuffer(rig.queue, mem, CL_TRUE, flags, offset, size, 0, nullptr, nullptr, &code);
}

cl_int unmap_and_finish(ClRig& rig, cl_mem mem, void* pointer) {
    cl_int code = rig.api.EnqueueUnmapMemObject(rig.queue, mem, pointer, 0, nullptr, nullptr);
    if (code == CL_SUCCESS) {
        code = rig.api.Finish(rig.queue);
    }
    return code;
}

std::vector<std::uint32_t> stepped(const std::vector<std::uint32_t>& in, std::uint32_t mul, std::uint32_t add) {
    std::vector<std::uint32_t> out(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        out[i] = in[i] * mul + add;
    }
    return out;
}

bool run_step(ClRig& rig, ClKernel& step, cl_mem mem, std::size_t words, std::uint32_t mul, std::uint32_t add, Problems& problems,
              const std::string& what) {
    cl_int code = step.set(mem, mul, add);
    if (code == CL_SUCCESS) {
        code = step.run(words);
    }
    if (code != CL_SUCCESS) {
        problems.add(what + ": running the kernel returned " + ClTools::error_name(code));
        return false;
    }
    return true;
}

bool host_matches(Context& context, Problems& problems, const std::string& what, const AlignedBlock& host, const std::vector<std::uint32_t>& expected,
                  std::size_t first_word = 0) {
    return Kit::same_bytes(context, problems, what, expected.data(), host.words() + first_word, expected.size() * 4);
}

}  // namespace

Outcome MemChecks::use_host_ptr_coherence(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        const std::size_t words = 64 * 1024;
        const std::size_t bytes = words * 4;
        AlignedBlock host(bytes, std::max<std::size_t>(rig.base_alignment(), 4096));
        if (!host) {
            return Outcome::fail("host allocation failed");
        }
        std::vector<std::uint32_t> data = Kit::words(11, words);
        std::memcpy(host.get(), data.data(), bytes);
        ClProgram program;
        if (!Kit::compile(rig, program, ClSources::kStep, "", problems, "step kernel")) {
            return problems.outcome();
        }
        ClKernel step = Kit::kernel(program, "lsw_step", problems);
        cl_int code = CL_SUCCESS;
        ClOwned<cl_mem> buffer = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR, bytes, host.get(), code);
        if (!step || !buffer) {
            problems.add("clCreateBuffer(USE_HOST_PTR) returned " + ClTools::error_name(code));
            return problems.outcome();
        }
        if (!run_step(rig, step, buffer.get(), words, 3, 7, problems, "first kernel")) {
            return problems.outcome();
        }
        std::vector<std::uint32_t> expected = stepped(data, 3, 7);
        void* mapped = map_blocking(rig, buffer.get(), CL_MAP_READ, 0, bytes, code);
        if (code != CL_SUCCESS || mapped == nullptr) {
            problems.add("blocking clEnqueueMapBuffer(READ) returned " + ClTools::error_name(code));
            return problems.outcome();
        }
        if (mapped != host.get()) {
            problems.add("the mapped pointer is not derived from the USE_HOST_PTR host_ptr (mapped " + Kit::hex(reinterpret_cast<std::uintptr_t>(mapped), 16) + ", host_ptr " + Kit::hex(reinterpret_cast<std::uintptr_t>(host.get()), 16) + ")");
        }
        host_matches(context, problems, "host_ptr after a blocking map following a kernel", host, expected);
        code = unmap_and_finish(rig, buffer.get(), mapped);
        Kit::expect(problems, "unmap and finish", code, {CL_SUCCESS});

        const std::size_t sub_offset = 4096;
        const std::size_t sub_size = 8192;
        mapped = map_blocking(rig, buffer.get(), CL_MAP_READ, sub_offset, sub_size, code);
        if (code != CL_SUCCESS || mapped == nullptr) {
            problems.add("blocking sub-range map returned " + ClTools::error_name(code));
        } else {
            if (mapped != host.bytes() + sub_offset) {
                problems.add("a sub-range map at offset " + std::to_string(sub_offset) + " does not return host_ptr + offset");
            }
            Kit::same_bytes(context, problems, "sub-range of host_ptr after a map", expected.data() + sub_offset / 4, host.bytes() + sub_offset, sub_size);
            code = unmap_and_finish(rig, buffer.get(), mapped);
            Kit::expect(problems, "unmap of the sub-range", code, {CL_SUCCESS});
        }

        mapped = map_blocking(rig, buffer.get(), CL_MAP_WRITE, 0, bytes, code);
        if (code != CL_SUCCESS || mapped == nullptr) {
            problems.add("blocking clEnqueueMapBuffer(WRITE) returned " + ClTools::error_name(code));
            return problems.outcome();
        }
        data = Kit::words(12, words);
        std::memcpy(mapped, data.data(), bytes);
        code = unmap_and_finish(rig, buffer.get(), mapped);
        Kit::expect(problems, "unmap after host writes", code, {CL_SUCCESS});
        if (!run_step(rig, step, buffer.get(), words, 5, 1, problems, "second kernel")) {
            return problems.outcome();
        }
        expected = stepped(data, 5, 1);
        std::vector<std::uint32_t> actual;
        if (Kit::read_words(rig, buffer.get(), words, actual, problems, "after host writes between map and unmap")) {
            Kit::same_words(context, problems, "kernel result after host writes through a mapping", expected, actual);
        }

        cl_event map_event = nullptr;
        mapped = rig.api.EnqueueMapBuffer(rig.queue, buffer.get(), CL_FALSE, CL_MAP_READ, 0, bytes, 0, nullptr, &map_event, &code);
        if (code != CL_SUCCESS || mapped == nullptr || map_event == nullptr) {
            problems.add("non-blocking clEnqueueMapBuffer returned " + ClTools::error_name(code));
        } else {
            ClOwned<cl_event> owned(rig, map_event);
            if (Kit::wait(rig, map_event, problems, "map event")) {
                host_matches(context, problems, "host_ptr after a non-blocking map completed", host, expected);
            }
            code = unmap_and_finish(rig, buffer.get(), mapped);
            Kit::expect(problems, "unmap after the non-blocking map", code, {CL_SUCCESS});
        }
        return problems.outcome();
    });
}

Outcome MemChecks::use_host_ptr_blocking_ops(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        const std::size_t words = 32 * 1024;
        const std::size_t bytes = words * 4;
        const std::size_t alignment = std::max<std::size_t>(rig.base_alignment(), 4096);
        AlignedBlock host(bytes, alignment);
        AlignedBlock other(bytes, alignment);
        AlignedBlock readback(bytes, alignment);
        if (!host || !other || !readback) {
            return Outcome::fail("host allocation failed");
        }
        const std::vector<std::uint32_t> initial = Kit::words(21, words);
        std::memcpy(host.get(), initial.data(), bytes);
        const std::vector<std::uint32_t> replacement = Kit::words(22, words);
        std::memcpy(other.get(), replacement.data(), bytes);
        cl_int code = CL_SUCCESS;
        ClOwned<cl_mem> buffer = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR, bytes, host.get(), code);
        if (!buffer) {
            return Outcome::fail("clCreateBuffer(USE_HOST_PTR) returned " + ClTools::error_name(code));
        }
        code = Kit::write(rig, buffer.get(), other.get(), bytes);
        Kit::expect(problems, "blocking clEnqueueWriteBuffer from other memory", code, {CL_SUCCESS});
        code = Kit::read(rig, buffer.get(), readback.get(), bytes);
        Kit::expect(problems, "blocking clEnqueueReadBuffer into other memory", code, {CL_SUCCESS});
        host_matches(context, problems, "data read back after a blocking write from other memory", readback, replacement);
        void* mapped = map_blocking(rig, buffer.get(), CL_MAP_READ, 0, bytes, code);
        if (code != CL_SUCCESS || mapped == nullptr) {
            problems.add("map after the blocking write returned " + ClTools::error_name(code));
            return problems.outcome();
        }
        host_matches(context, problems, "host_ptr at the map following a blocking write from other memory", host, replacement);
        Kit::expect(problems, "unmap", unmap_and_finish(rig, buffer.get(), mapped), {CL_SUCCESS});

        ClProgram program;
        if (!Kit::compile(rig, program, ClSources::kStep, "", problems, "step kernel")) {
            return problems.outcome();
        }
        ClKernel step = Kit::kernel(program, "lsw_step", problems);
        if (!step || !run_step(rig, step, buffer.get(), words, 3, 7, problems, "kernel")) {
            return problems.outcome();
        }
        std::vector<std::uint32_t> expected = stepped(replacement, 3, 7);
        std::memset(readback.get(), 0, bytes);
        code = Kit::read(rig, buffer.get(), readback.get(), bytes);
        Kit::expect(problems, "blocking read after a kernel", code, {CL_SUCCESS});
        host_matches(context, problems, "blocking read of kernel results", readback, expected);
        mapped = map_blocking(rig, buffer.get(), CL_MAP_READ, 0, bytes, code);
        if (code == CL_SUCCESS && mapped != nullptr) {
            host_matches(context, problems, "host_ptr at the map following a kernel", host, expected);
            Kit::expect(problems, "unmap", unmap_and_finish(rig, buffer.get(), mapped), {CL_SUCCESS});
        } else {
            problems.add("map after the kernel returned " + ClTools::error_name(code));
        }

        const std::size_t part_offset = 1024;
        const std::size_t part_size = 2048;
        const std::vector<std::uint32_t> patch = Kit::words(23, part_size / 4);
        code = Kit::write(rig, buffer.get(), patch.data(), part_size, part_offset);
        Kit::expect(problems, "partial blocking write", code, {CL_SUCCESS});
        std::copy(patch.begin(), patch.end(), expected.begin() + static_cast<std::ptrdiff_t>(part_offset / 4));
        mapped = map_blocking(rig, buffer.get(), CL_MAP_READ, 0, bytes, code);
        if (code == CL_SUCCESS && mapped != nullptr) {
            host_matches(context, problems, "host_ptr at the map following a partial blocking write", host, expected);
            Kit::expect(problems, "unmap", unmap_and_finish(rig, buffer.get(), mapped), {CL_SUCCESS});
        } else {
            problems.add("map after the partial write returned " + ClTools::error_name(code));
        }
        return problems.outcome();
    });
}

Outcome MemChecks::alloc_host_ptr(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        ClProgram program;
        if (!Kit::compile(rig, program, ClSources::kStep, "", problems, "step kernel")) {
            return problems.outcome();
        }
        ClKernel step = Kit::kernel(program, "lsw_step", problems);
        if (!step) {
            return problems.outcome();
        }
        for (const std::size_t words : {std::size_t(1), std::size_t(3), std::size_t(1025), std::size_t(262144)}) {
            const std::size_t bytes = words * 4;
            const std::string tag = ClTools::bytes_text(bytes) + " ALLOC_HOST_PTR";
            cl_int code = CL_SUCCESS;
            ClOwned<cl_mem> buffer = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, bytes, nullptr, code);
            if (!buffer) {
                problems.add(tag + ": clCreateBuffer returned " + ClTools::error_name(code));
                continue;
            }
            void* mapped = map_blocking(rig, buffer.get(), CL_MAP_WRITE, 0, bytes, code);
            if (code != CL_SUCCESS || mapped == nullptr) {
                problems.add(tag + ": blocking map(WRITE) returned " + ClTools::error_name(code));
                continue;
            }
            const std::vector<std::uint32_t> data = Kit::words(31 + words, words);
            std::memcpy(mapped, data.data(), bytes);
            code = unmap_and_finish(rig, buffer.get(), mapped);
            Kit::expect(problems, tag + ": unmap", code, {CL_SUCCESS});
            if (!run_step(rig, step, buffer.get(), words, 9, 5, problems, tag)) {
                continue;
            }
            const std::vector<std::uint32_t> expected = stepped(data, 9, 5);
            mapped = map_blocking(rig, buffer.get(), CL_MAP_READ, 0, bytes, code);
            if (code != CL_SUCCESS || mapped == nullptr) {
                problems.add(tag + ": blocking map(READ) after the kernel returned " + ClTools::error_name(code));
                continue;
            }
            Kit::same_bytes(context, problems, tag + " kernel results seen through a map", expected.data(), mapped, bytes);
            Kit::expect(problems, tag + ": unmap", unmap_and_finish(rig, buffer.get(), mapped), {CL_SUCCESS});
            std::vector<std::uint32_t> actual;
            if (Kit::read_words(rig, buffer.get(), words, actual, problems, tag)) {
                Kit::same_words(context, problems, tag + " kernel results seen through a read", expected, actual);
            }
        }
        return problems.outcome();
    });
}

Outcome MemChecks::copy_host_ptr(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        const std::size_t alignment = std::max<std::size_t>(rig.base_alignment(), 64);
        for (const std::size_t bytes : {std::size_t(1), std::size_t(7), std::size_t(4097), std::size_t(1u << 20)}) {
            const std::string tag = ClTools::bytes_text(bytes) + " COPY_HOST_PTR";
            AlignedBlock host(bytes, alignment);
            if (!host) {
                return Outcome::fail("host allocation failed");
            }
            const std::vector<std::uint8_t> original = Kit::bytes(41 + bytes, bytes);
            std::memcpy(host.get(), original.data(), bytes);
            for (const cl_mem_flags extra : {cl_mem_flags(0), cl_mem_flags(CL_MEM_ALLOC_HOST_PTR)}) {
                cl_int code = CL_SUCCESS;
                ClOwned<cl_mem> buffer = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR | extra, bytes, host.get(), code);
                if (!buffer) {
                    problems.add(tag + ": clCreateBuffer returned " + ClTools::error_name(code));
                    continue;
                }
                for (std::size_t i = 0; i < bytes; ++i) {
                    host.bytes()[i] = static_cast<std::uint8_t>(~original[i]);
                }
                std::vector<std::uint8_t> actual(bytes);
                code = Kit::read(rig, buffer.get(), actual.data(), bytes);
                Kit::expect(problems, tag + ": read back", code, {CL_SUCCESS});
                Kit::same_bytes(context, problems, tag + " contents after the source changed", original.data(), actual.data(), bytes);
                std::memcpy(host.get(), original.data(), bytes);
            }
        }
        cl_int code = CL_SUCCESS;
        ClOwned<cl_mem> none = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, 64, nullptr, code);
        if (none) {
            problems.add("COPY_HOST_PTR with a NULL host_ptr created a buffer");
        } else {
            Kit::expect(problems, "COPY_HOST_PTR with a NULL host_ptr", code, {CL_INVALID_HOST_PTR});
        }
        std::uint32_t scratch[16] = {};
        ClOwned<cl_mem> stray = Kit::buffer(rig, CL_MEM_READ_WRITE, sizeof(scratch), scratch, code);
        if (stray) {
            problems.add("a host_ptr without USE_HOST_PTR or COPY_HOST_PTR created a buffer");
        } else {
            Kit::expect(problems, "a host_ptr without USE_HOST_PTR or COPY_HOST_PTR", code, {CL_INVALID_HOST_PTR});
        }
        ClOwned<cl_mem> use_null = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR, 64, nullptr, code);
        if (use_null) {
            problems.add("USE_HOST_PTR with a NULL host_ptr created a buffer");
        } else {
            Kit::expect(problems, "USE_HOST_PTR with a NULL host_ptr", code, {CL_INVALID_HOST_PTR});
        }
        return problems.outcome();
    });
}

Outcome MemChecks::access_flags_honoured(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        const std::size_t bytes = 4096;
        const std::vector<std::uint8_t> data = Kit::bytes(51, bytes);
        std::vector<std::uint8_t> scratch(bytes);
        struct Case {
            const char* name;
            cl_mem_flags flags;
            bool host_reads;
            bool host_writes;
        };
        std::vector<Case> cases = {{"READ_ONLY", CL_MEM_READ_ONLY, true, true},
                                   {"WRITE_ONLY", CL_MEM_WRITE_ONLY, true, true},
                                   {"READ_WRITE", CL_MEM_READ_WRITE, true, true}};
        const bool host_flags = rig.version.at_least(1, 2);
        if (host_flags) {
            cases.push_back({"HOST_READ_ONLY", CL_MEM_HOST_READ_ONLY, true, false});
            cases.push_back({"HOST_WRITE_ONLY", CL_MEM_HOST_WRITE_ONLY, false, true});
            cases.push_back({"HOST_NO_ACCESS", CL_MEM_HOST_NO_ACCESS, false, false});
            cases.push_back({"READ_ONLY|HOST_WRITE_ONLY", CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY, false, true});
            cases.push_back({"WRITE_ONLY|HOST_READ_ONLY", CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY, true, false});
        }
        for (const Case& c : cases) {
            const std::string tag = std::string("CL_MEM_") + c.name;
            cl_int code = CL_SUCCESS;
            ClOwned<cl_mem> buffer = Kit::buffer(rig, c.flags | CL_MEM_COPY_HOST_PTR, bytes, const_cast<std::uint8_t*>(data.data()), code);
            if (!buffer) {
                problems.add(tag + ": clCreateBuffer returned " + ClTools::error_name(code));
                continue;
            }
            void* mapped = map_blocking(rig, buffer.get(), CL_MAP_READ, 0, bytes, code);
            if (c.host_reads) {
                if (code != CL_SUCCESS || mapped == nullptr) {
                    problems.add(tag + ": map(READ) returned " + ClTools::error_name(code));
                } else {
                    Kit::expect(problems, tag + ": unmap", unmap_and_finish(rig, buffer.get(), mapped), {CL_SUCCESS});
                }
            } else {
                Kit::expect(problems, tag + ": map(READ)", code, {CL_INVALID_OPERATION});
            }
            mapped = map_blocking(rig, buffer.get(), CL_MAP_WRITE, 0, bytes, code);
            if (c.host_writes) {
                if (code != CL_SUCCESS || mapped == nullptr) {
                    problems.add(tag + ": map(WRITE) returned " + ClTools::error_name(code));
                } else {
                    Kit::expect(problems, tag + ": unmap", unmap_and_finish(rig, buffer.get(), mapped), {CL_SUCCESS});
                }
            } else {
                Kit::expect(problems, tag + ": map(WRITE)", code, {CL_INVALID_OPERATION});
            }
            if (rig.version.at_least(1, 2)) {
                mapped = map_blocking(rig, buffer.get(), CL_MAP_WRITE_INVALIDATE_REGION, 0, bytes, code);
                if (c.host_writes) {
                    if (code != CL_SUCCESS || mapped == nullptr) {
                        problems.add(tag + ": map(WRITE_INVALIDATE_REGION) returned " + ClTools::error_name(code));
                    } else {
                        Kit::expect(problems, tag + ": unmap", unmap_and_finish(rig, buffer.get(), mapped), {CL_SUCCESS});
                    }
                } else {
                    Kit::expect(problems, tag + ": map(WRITE_INVALIDATE_REGION)", code, {CL_INVALID_OPERATION});
                }
            }
            code = Kit::read(rig, buffer.get(), scratch.data(), bytes);
            Kit::expect(problems, tag + ": clEnqueueReadBuffer", code, c.host_reads ? std::initializer_list<cl_int>{CL_SUCCESS} : std::initializer_list<cl_int>{CL_INVALID_OPERATION});
            code = Kit::write(rig, buffer.get(), data.data(), bytes);
            Kit::expect(problems, tag + ": clEnqueueWriteBuffer", code, c.host_writes ? std::initializer_list<cl_int>{CL_SUCCESS} : std::initializer_list<cl_int>{CL_INVALID_OPERATION});
        }
        if (host_flags) {
            struct Bad {
                const char* name;
                cl_mem_flags flags;
            };
            const Bad bad[] = {
                {"READ_WRITE|READ_ONLY", CL_MEM_READ_WRITE | CL_MEM_READ_ONLY},
                {"READ_WRITE|WRITE_ONLY", CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY},
                {"READ_ONLY|WRITE_ONLY", CL_MEM_READ_ONLY | CL_MEM_WRITE_ONLY},
                {"USE_HOST_PTR|ALLOC_HOST_PTR", CL_MEM_USE_HOST_PTR | CL_MEM_ALLOC_HOST_PTR},
                {"USE_HOST_PTR|COPY_HOST_PTR", CL_MEM_USE_HOST_PTR | CL_MEM_COPY_HOST_PTR},
                {"HOST_WRITE_ONLY|HOST_READ_ONLY", CL_MEM_HOST_WRITE_ONLY | CL_MEM_HOST_READ_ONLY},
                {"HOST_WRITE_ONLY|HOST_NO_ACCESS", CL_MEM_HOST_WRITE_ONLY | CL_MEM_HOST_NO_ACCESS},
                {"HOST_READ_ONLY|HOST_NO_ACCESS", CL_MEM_HOST_READ_ONLY | CL_MEM_HOST_NO_ACCESS},
                {"an undefined flag bit", cl_mem_flags(1) << 62},
            };
            std::vector<std::uint8_t> host_copy = data;
            for (const Bad& entry : bad) {
                cl_int code = CL_SUCCESS;
                const bool wants_host = (entry.flags & (CL_MEM_USE_HOST_PTR | CL_MEM_COPY_HOST_PTR)) != 0;
                ClOwned<cl_mem> buffer = Kit::buffer(rig, entry.flags, bytes, wants_host ? host_copy.data() : nullptr, code);
                if (buffer) {
                    problems.add(std::string("clCreateBuffer(") + entry.name + ") created a buffer");
                } else {
                    Kit::expect(problems, std::string("clCreateBuffer(") + entry.name + ")", code, {CL_INVALID_VALUE});
                }
            }
        }
        return problems.outcome();
    });
}

Outcome MemChecks::sub_buffer_alignment(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        if (!rig.version.at_least(1, 1) || rig.api.CreateSubBuffer == nullptr) {
            return Context::skip("clCreateSubBuffer needs OpenCL 1.1; the device reports " + rig.version.text());
        }
        const std::size_t align = rig.base_alignment();
        const std::size_t bytes = align * 8;
        const std::vector<std::uint8_t> data = Kit::bytes(61, bytes);
        cl_int code = CL_SUCCESS;
        ClOwned<cl_mem> parent = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bytes, const_cast<std::uint8_t*>(data.data()), code);
        if (!parent) {
            return Outcome::fail("parent buffer: " + ClTools::error_name(code));
        }
        auto make = [&](cl_mem_flags flags, std::size_t origin, std::size_t size, cl_buffer_create_type type, cl_mem from, cl_int& result) {
            cl_buffer_region region{origin, size};
            cl_mem sub = rig.api.CreateSubBuffer(from, flags, type, &region, &result);
            return ClOwned<cl_mem>(rig, result == CL_SUCCESS ? sub : nullptr);
        };
        cl_int result = CL_SUCCESS;
        for (const std::size_t origin : {std::size_t(1), align / 2, align + 1}) {
            if (origin % align == 0) {
                continue;
            }
            ClOwned<cl_mem> sub = make(0, origin, align, CL_BUFFER_CREATE_TYPE_REGION, parent.get(), result);
            if (sub) {
                problems.add("a sub-buffer at the misaligned origin " + std::to_string(origin) + " (CL_DEVICE_MEM_BASE_ADDR_ALIGN " + std::to_string(align * 8) + " bits) was created");
            } else {
                Kit::expect(problems, "clCreateSubBuffer at origin " + std::to_string(origin), result, {CL_MISALIGNED_SUB_BUFFER_OFFSET});
            }
        }
        ClOwned<cl_mem> first = make(0, align, align * 2, CL_BUFFER_CREATE_TYPE_REGION, parent.get(), result);
        if (!first) {
            problems.add("an aligned sub-buffer at origin " + std::to_string(align) + " failed: " + ClTools::error_name(result));
            return problems.outcome();
        }
        ClOwned<cl_mem> second = make(0, align * 4, align, CL_BUFFER_CREATE_TYPE_REGION, parent.get(), result);
        if (!second) {
            problems.add("an aligned sub-buffer at origin " + std::to_string(align * 4) + " failed: " + ClTools::error_name(result));
            return problems.outcome();
        }
        cl_mem associated = nullptr;
        std::size_t reported_offset = 0;
        rig.api.GetMemObjectInfo(first.get(), CL_MEM_ASSOCIATED_MEMOBJECT, sizeof(associated), &associated, nullptr);
        rig.api.GetMemObjectInfo(first.get(), CL_MEM_OFFSET, sizeof(reported_offset), &reported_offset, nullptr);
        if (associated != parent.get() || reported_offset != align) {
            problems.add("CL_MEM_ASSOCIATED_MEMOBJECT / CL_MEM_OFFSET of the sub-buffer do not name the parent and origin " + std::to_string(align));
        }
        std::vector<std::uint8_t> model = data;
        const std::vector<std::uint8_t> patch = Kit::bytes(62, align * 2);
        code = Kit::write(rig, first.get(), patch.data(), patch.size());
        Kit::expect(problems, "write through the sub-buffer", code, {CL_SUCCESS});
        std::copy(patch.begin(), patch.end(), model.begin() + static_cast<std::ptrdiff_t>(align));
        std::vector<std::uint8_t> actual(bytes);
        code = Kit::read(rig, parent.get(), actual.data(), bytes);
        Kit::expect(problems, "read of the parent", code, {CL_SUCCESS});
        Kit::same_bytes(context, problems, "parent after a write through a sub-buffer", model.data(), actual.data(), bytes);
        const std::vector<std::uint8_t> parent_patch = Kit::bytes(63, align);
        code = Kit::write(rig, parent.get(), parent_patch.data(), parent_patch.size(), align * 4);
        Kit::expect(problems, "write to the parent", code, {CL_SUCCESS});
        std::vector<std::uint8_t> through_sub(align);
        code = Kit::read(rig, second.get(), through_sub.data(), through_sub.size());
        Kit::expect(problems, "read through the second sub-buffer", code, {CL_SUCCESS});
        Kit::same_bytes(context, problems, "sub-buffer after a write to the parent", parent_patch.data(), through_sub.data(), align);

        ClProgram program;
        if (Kit::compile(rig, program, ClSources::kStep, "", problems, "step kernel")) {
            ClKernel step = Kit::kernel(program, "lsw_step", problems);
            if (step) {
                const std::size_t words = align * 2 / 4;
                if (run_step(rig, step, first.get(), words, 3, 1, problems, "kernel on the sub-buffer")) {
                    std::vector<std::uint32_t> before(words);
                    std::memcpy(before.data(), patch.data(), words * 4);
                    const std::vector<std::uint32_t> expected = stepped(before, 3, 1);
                    std::vector<std::uint8_t> parent_after(bytes);
                    code = Kit::read(rig, parent.get(), parent_after.data(), bytes);
                    Kit::expect(problems, "read of the parent after the kernel", code, {CL_SUCCESS});
                    Kit::same_bytes(context, problems, "parent after a kernel wrote through a sub-buffer", expected.data(), parent_after.data() + align, words * 4);
                }
            }
        }
        make(0, align, bytes, CL_BUFFER_CREATE_TYPE_REGION, parent.get(), result);
        Kit::expect(problems, "a sub-buffer region beyond the parent", result, {CL_INVALID_VALUE});
        make(0, align, 0, CL_BUFFER_CREATE_TYPE_REGION, parent.get(), result);
        Kit::expect(problems, "a zero-sized sub-buffer", result, {CL_INVALID_BUFFER_SIZE});
        make(0, align, align, 0, parent.get(), result);
        Kit::expect(problems, "an invalid buffer_create_type", result, {CL_INVALID_VALUE});
        make(0, 0, align, CL_BUFFER_CREATE_TYPE_REGION, first.get(), result);
        Kit::expect(problems, "a sub-buffer of a sub-buffer", result, {CL_INVALID_MEM_OBJECT});
        ClOwned<cl_mem> read_only = Kit::buffer(rig, CL_MEM_READ_ONLY, bytes, nullptr, code);
        if (read_only) {
            make(CL_MEM_WRITE_ONLY, 0, align, CL_BUFFER_CREATE_TYPE_REGION, read_only.get(), result);
            Kit::expect(problems, "a WRITE_ONLY sub-buffer of a READ_ONLY buffer", result, {CL_INVALID_VALUE});
            ClOwned<cl_mem> inherited = make(0, 0, align, CL_BUFFER_CREATE_TYPE_REGION, read_only.get(), result);
            Kit::expect(problems, "a sub-buffer inheriting READ_ONLY", result, {CL_SUCCESS});
        }
        context.measure("cl.mem_base_addr_align_bytes", align);
        return problems.outcome();
    });
}

}  // namespace lsw::suites::sanity::cl
