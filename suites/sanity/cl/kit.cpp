#include "sanity/cl/kit.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::sanity::cl {

Outcome Kit::each_device(Context& context, const DeviceBody& body, bool profiling) {
    ClApi api;
    ClDeviceList list;
    std::string error;
    if (!ClRig::list_devices(api, list, error)) {
        return open_failed(error);
    }
    Problems problems;
    for (const ClDeviceRef& ref : list.refs) {
        ClRig rig;
        ClRigOptions options;
        options.profiling = profiling;
        if (!rig.open_ref(ref, options, error)) {
            problems.add(ref.label() + ": " + error);
            continue;
        }
        Problems device_problems;
        body(rig, device_problems);
        rig.drain();
        if (!device_problems.empty()) {
            problems.add(ref.label() + ": " + device_problems.text());
        }
    }
    context.measure("cl.devices", list.refs.size());
    return problems.outcome();
}

Outcome Kit::on_default(Context& context, const std::function<Outcome(ClRig&)>& body, bool profiling) {
    ClRig rig;
    ClRigOptions options;
    options.profiling = profiling;
    std::string error;
    if (!rig.open(options, error)) {
        return open_failed(error);
    }
    context.measure("cl.device", rig.ref.label() + ", OpenCL " + rig.version.text());
    Outcome outcome = body(rig);
    rig.drain();
    return outcome;
}

bool Kit::expect(Problems& problems, const std::string& what, cl_int got, std::initializer_list<cl_int> accepted) {
    if (ClTools::code_in(got, accepted)) {
        return true;
    }
    problems.add(what + " returned " + ClTools::error_name(got) + ", expected " + ClTools::accepted_text(accepted));
    return false;
}

ClOwned<cl_mem> Kit::buffer(ClRig& rig, cl_mem_flags flags, std::size_t size, void* host, cl_int& code) {
    code = CL_SUCCESS;
    cl_mem mem = rig.api.CreateBuffer(rig.context, flags, size, host, &code);
    if (code != CL_SUCCESS) {
        if (mem != nullptr) {
            rig.api.ReleaseMemObject(mem);
        }
        return {};
    }
    return ClOwned<cl_mem>(rig, mem);
}

ClOwned<cl_mem> Kit::buffer(ClRig& rig, cl_mem_flags flags, std::size_t size, Problems& problems, const std::string& what,
                            void* host) {
    cl_int code = CL_SUCCESS;
    ClOwned<cl_mem> mem = buffer(rig, flags, size, host, code);
    if (!mem) {
        problems.add(what + ": clCreateBuffer(" + ClTools::bytes_text(size) + ") returned " + ClTools::error_name(code));
    }
    return mem;
}

std::vector<std::uint32_t> Kit::words(std::uint64_t seed, std::size_t count) {
    std::vector<std::uint32_t> data(count);
    Pattern::fill(seed, 0, data.data(), count * sizeof(std::uint32_t));
    return data;
}

std::vector<std::uint8_t> Kit::bytes(std::uint64_t seed, std::size_t count) {
    std::vector<std::uint8_t> data(count);
    Pattern::fill(seed, 0, data.data(), count);
    return data;
}

cl_int Kit::write(ClRig& rig, cl_mem mem, const void* data, std::size_t size, std::size_t offset) {
    return rig.api.EnqueueWriteBuffer(rig.queue, mem, CL_TRUE, offset, size, data, 0, nullptr, nullptr);
}

cl_int Kit::read(ClRig& rig, cl_mem mem, void* data, std::size_t size, std::size_t offset) {
    return rig.api.EnqueueReadBuffer(rig.queue, mem, CL_TRUE, offset, size, data, 0, nullptr, nullptr);
}

bool Kit::read_words(ClRig& rig, cl_mem mem, std::size_t count, std::vector<std::uint32_t>& out, Problems& problems,
                     const std::string& what) {
    out.assign(count, 0);
    const cl_int code = read(rig, mem, out.data(), count * sizeof(std::uint32_t));
    if (code != CL_SUCCESS) {
        problems.add(what + ": clEnqueueReadBuffer returned " + ClTools::error_name(code));
        return false;
    }
    return true;
}

std::string Kit::sanitize(std::string name) {
    for (char& c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            c = '_';
        }
    }
    return name;
}

bool Kit::same_bytes(Context& context, Problems& problems, const std::string& what, const void* expected,
                     const void* actual, std::size_t size) {
    if (size == 0 || std::memcmp(expected, actual, size) == 0) {
        return true;
    }
    const auto* a = static_cast<const std::uint8_t*>(expected);
    const auto* b = static_cast<const std::uint8_t*>(actual);
    std::size_t first = 0;
    std::size_t bad = 0;
    for (std::size_t i = 0; i < size; ++i) {
        if (a[i] != b[i]) {
            if (bad == 0) {
                first = i;
            }
            ++bad;
        }
    }
    ArtifactWriter writer(context);
    const std::string stem = sanitize(what);
    writer.bytes(stem + ".expected.bin", expected, size);
    writer.bytes(stem + ".actual.bin", actual, size);
    problems.add(what + ": " + std::to_string(bad) + " of " + std::to_string(size) + " bytes differ, first at offset " +
                 std::to_string(first) + " expected 0x" + Text::hex(a[first], 2) + " actual 0x" + Text::hex(b[first], 2));
    return false;
}

bool Kit::same_words(Context& context, Problems& problems, const std::string& what,
                     const std::vector<std::uint32_t>& expected, const std::vector<std::uint32_t>& actual) {
    if (expected.size() != actual.size()) {
        problems.add(what + ": expected " + std::to_string(expected.size()) + " words, got " + std::to_string(actual.size()));
        return false;
    }
    if (expected == actual) {
        return true;
    }
    std::size_t first = 0;
    std::size_t bad = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (expected[i] != actual[i]) {
            if (bad == 0) {
                first = i;
            }
            ++bad;
        }
    }
    ArtifactWriter writer(context);
    const std::string stem = sanitize(what);
    writer.bytes(stem + ".expected.bin", expected.data(), expected.size() * sizeof(std::uint32_t));
    writer.bytes(stem + ".actual.bin", actual.data(), actual.size() * sizeof(std::uint32_t));
    problems.add(what + ": " + std::to_string(bad) + " of " + std::to_string(expected.size()) + " words differ, first word " +
                 std::to_string(first) + " expected 0x" + Text::hex(expected[first], 8) + " actual 0x" +
                 Text::hex(actual[first], 8));
    return false;
}

bool Kit::compile(ClRig& rig, ClProgram& program, const std::string& source, const std::string& options, Problems& problems,
                  const std::string& what) {
    std::string error;
    if (!program.build(rig, source, options, error)) {
        problems.add(what + ": " + error);
        return false;
    }
    return true;
}

ClKernel Kit::kernel(ClProgram& program, const char* name, Problems& problems) {
    std::string error;
    ClKernel made = program.kernel(name, error);
    if (!made) {
        problems.add(error);
    }
    return made;
}

bool Kit::wait(ClRig& rig, cl_event event, Problems& problems, const std::string& what) {
    const cl_int code = rig.api.WaitForEvents(1, &event);
    if (code != CL_SUCCESS) {
        problems.add(what + ": clWaitForEvents returned " + ClTools::error_name(code));
        return false;
    }
    return true;
}

cl_int Kit::event_status(ClRig& rig, cl_event event) {
    cl_int status = 0x7fff;
    rig.api.GetEventInfo(event, CL_EVENT_COMMAND_EXECUTION_STATUS, sizeof(status), &status, nullptr);
    return status;
}

bool Kit::vector_add(ClRig& rig, std::size_t count, std::uint64_t seed, Pair& out, Problems& problems,
                     const std::string& what) {
    ClProgram program;
    if (!compile(rig, program, ClSources::kVectorAdd, "", problems, what)) {
        return false;
    }
    ClKernel add = kernel(program, "lsw_add", problems);
    if (!add) {
        return false;
    }
    const std::vector<std::uint32_t> a = words(seed, count);
    const std::vector<std::uint32_t> b = words(seed + 1, count);
    out.expected.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.expected[i] = a[i] + b[i];
    }
    cl_int code = CL_SUCCESS;
    ClOwned<cl_mem> ma = buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 4, const_cast<std::uint32_t*>(a.data()), code);
    ClOwned<cl_mem> mb = buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 4, const_cast<std::uint32_t*>(b.data()), code);
    ClOwned<cl_mem> mc = buffer(rig, CL_MEM_WRITE_ONLY, count * 4, nullptr, code);
    if (!ma || !mb || !mc) {
        problems.add(what + ": creating the vector-add buffers failed: " + ClTools::error_name(code));
        return false;
    }
    code = add.set(ma, mb, mc);
    if (code == CL_SUCCESS) {
        code = add.run(count);
    }
    if (code != CL_SUCCESS) {
        problems.add(what + ": running the vector add returned " + ClTools::error_name(code));
        return false;
    }
    return read_words(rig, mc.get(), count, out.actual, problems, what);
}

bool Kit::reduction(ClRig& rig, std::size_t count, std::size_t group, std::uint64_t seed, Pair& out, Problems& problems,
                    const std::string& what) {
    ClProgram program;
    if (!compile(rig, program, ClSources::kReduce, "", problems, what)) {
        return false;
    }
    ClKernel reduce = kernel(program, "lsw_reduce", problems);
    if (!reduce) {
        return false;
    }
    group = std::min(group, reduce.work_group_size());
    const std::size_t groups = (count + group - 1) / group;
    const std::size_t padded = groups * group;
    std::vector<std::uint32_t> input = words(seed, count);
    out.expected.assign(groups, 0);
    for (std::size_t i = 0; i < count; ++i) {
        out.expected[i / group] += input[i];
    }
    cl_int code = CL_SUCCESS;
    ClOwned<cl_mem> in = buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 4, input.data(), code);
    ClOwned<cl_mem> partial = buffer(rig, CL_MEM_WRITE_ONLY, groups * 4, nullptr, code);
    if (!in || !partial) {
        problems.add(what + ": creating the reduction buffers failed: " + ClTools::error_name(code));
        return false;
    }
    const cl_uint n = static_cast<cl_uint>(count);
    code = reduce.set(in, partial, ClLocal{group * 4}, n);
    if (code == CL_SUCCESS) {
        code = reduce.run(padded, group);
    }
    if (code != CL_SUCCESS) {
        problems.add(what + ": running the reduction returned " + ClTools::error_name(code));
        return false;
    }
    return read_words(rig, partial.get(), groups, out.actual, problems, what);
}

}  // namespace lsw::suites::sanity::cl
