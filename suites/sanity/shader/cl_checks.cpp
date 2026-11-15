#include "sanity/shader/cl_checks.hpp"

#include "sanity/cl/kit.hpp"
#include "support/cl_spirv.hpp"

#include <cstring>

namespace lsw::suites::sanity::shader {

namespace {

constexpr cl_device_info kOpenClCAllVersions = 0x1066;

struct Standard {
    const char* option;
    int macro;
    int major;
    int minor;
};

const Standard kStandards[] = {{"CL1.1", 110, 1, 1}, {"CL1.2", 120, 1, 2}, {"CL2.0", 200, 2, 0}, {"CL3.0", 300, 3, 0}};

const char* kSource =
    "__kernel void lsw_std(__global uint* out, __global const uint* a, __global const uint* b) {\n"
    "  size_t i = get_global_id(0);\n"
    "  if (i == 0) { out[0] = (uint)__OPENCL_C_VERSION__; }\n"
    "  out[i + 1] = a[i] * 3u + b[i];\n"
    "}\n";

struct NameVersion {
    cl_uint version;
    char name[64];
};

std::vector<Standard> supported_standards(ClRig& rig) {
    std::vector<Standard> out;
    ClVersion c_version;
    if (!ClTools::parse_version(rig.text_info(CL_DEVICE_OPENCL_C_VERSION), "OpenCL C ", c_version)) {
        c_version = rig.version;
    }
    std::vector<int> listed;
    if (rig.version.at_least(3, 0)) {
        std::size_t bytes = 0;
        if (rig.api.GetDeviceInfo(rig.device, kOpenClCAllVersions, 0, nullptr, &bytes) == CL_SUCCESS && bytes >= sizeof(NameVersion)) {
            std::vector<NameVersion> versions(bytes / sizeof(NameVersion));
            rig.api.GetDeviceInfo(rig.device, kOpenClCAllVersions, bytes, versions.data(), nullptr);
            for (const NameVersion& item : versions) {
                listed.push_back(static_cast<int>(item.version >> 22) * 100 + static_cast<int>((item.version >> 12) & 0x3ff) * 10);
            }
        }
    }
    for (const Standard& standard : kStandards) {
        bool usable = false;
        if (!listed.empty()) {
            for (int value : listed) {
                usable = usable || value == standard.macro;
            }
        } else {
            usable = standard.major < c_version.major || (standard.major == c_version.major && standard.minor <= c_version.minor);
        }
        if (usable) {
            out.push_back(standard);
        }
    }
    return out;
}

}  // namespace

Outcome ClChecks::c_and_il(Context& context) {
    return cl::Kit::each_device(context, [&](ClRig& rig, Problems& problems) {
        const std::vector<Standard> standards = supported_standards(rig);
        const std::size_t count = 100;
        const std::vector<std::uint32_t> a = cl::Kit::words(0xC1, count);
        const std::vector<std::uint32_t> b = cl::Kit::words(0xC2, count);
        for (const Standard& standard : standards) {
            const std::string name = std::string("-cl-std=") + standard.option;
            ClProgram program;
            std::string error;
            if (!program.build(rig, kSource, name, error)) {
                problems.add(name + ": " + error);
                continue;
            }
            ClKernel kernel = program.kernel("lsw_std", error);
            cl_int code = CL_SUCCESS;
            ClOwned<cl_mem> output = cl::Kit::buffer(rig, CL_MEM_READ_WRITE, (count + 1) * 4, nullptr, code);
            ClOwned<cl_mem> input_a = cl::Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 4, const_cast<std::uint32_t*>(a.data()), code);
            ClOwned<cl_mem> input_b = cl::Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 4, const_cast<std::uint32_t*>(b.data()), code);
            if (!kernel || !output || !input_a || !input_b) {
                problems.add(name + ": the kernel or its buffers cannot be created: " + error);
                continue;
            }
            code = kernel.set(output, input_a, input_b);
            if (code == CL_SUCCESS) {
                code = kernel.run(count);
            }
            std::vector<std::uint32_t> words(count + 1, 0xDEADBEEFu);
            if (code == CL_SUCCESS) {
                code = rig.api.EnqueueReadBuffer(rig.queue, output.get(), CL_TRUE, 0, (count + 1) * 4, words.data(), 0, nullptr, nullptr);
            }
            rig.api.Finish(rig.queue);
            if (code != CL_SUCCESS) {
                problems.add(name + ": running the kernel failed with " + ClTools::error_name(code));
                continue;
            }
            if (words[0] != static_cast<std::uint32_t>(standard.macro)) {
                problems.add(name + ": __OPENCL_C_VERSION__ is " + std::to_string(words[0]) + ", expected " + std::to_string(standard.macro));
            }
            for (std::size_t i = 0; i < count; ++i) {
                if (words[i + 1] != a[i] * 3u + b[i]) {
                    problems.add(name + ": result " + std::to_string(i) + " is 0x" + cl::Kit::hex(words[i + 1]) + ", expected 0x" + cl::Kit::hex(a[i] * 3u + b[i]));
                    break;
                }
            }
        }
        context.measure("cl.standards", static_cast<long long>(standards.size()));
        {
            const Standard* highest = standards.empty() ? nullptr : &standards.back();
            const char* beyond = highest == nullptr || highest->macro == 110 ? "-cl-std=CL1.2" : highest->macro == 120 ? "-cl-std=CL2.0" : highest->macro == 200 ? "-cl-std=CL3.0" : "-cl-std=CL4.0";
            cl_int code = CL_SUCCESS;
            const char* text = kSource;
            const std::size_t length = std::strlen(kSource);
            cl_program created = rig.api.CreateProgramWithSource(rig.context, 1, &text, &length, &code);
            ClOwned<cl_program> owned(rig, created);
            const cl_int built = rig.api.BuildProgram(created, 1, &rig.device, "-cl-std=CL9.9", nullptr, nullptr);
            cl::Kit::expect(problems, "clBuildProgram with the unknown option -cl-std=CL9.9", built, {CL_INVALID_BUILD_OPTIONS});
            if (highest != nullptr && highest->macro != 300) {
                cl_program above = rig.api.CreateProgramWithSource(rig.context, 1, &text, &length, &code);
                ClOwned<cl_program> above_owned(rig, above);
                const cl_int too_new = rig.api.BuildProgram(above, 1, &rig.device, beyond, nullptr, nullptr);
                if (too_new == CL_SUCCESS) {
                    problems.add(std::string("clBuildProgram accepted ") + beyond + " on a device whose highest OpenCL C version is below it");
                } else {
                    cl::Kit::expect(problems, std::string("clBuildProgram with ") + beyond + " beyond the device", too_new, {CL_INVALID_BUILD_OPTIONS, CL_BUILD_PROGRAM_FAILURE});
                }
            }
        }
        {
            const char* broken = "__kernel void broken(__global uint* out) { out[0] = undeclared_name +; }\n";
            const std::size_t length = std::strlen(broken);
            cl_int code = CL_SUCCESS;
            cl_program created = rig.api.CreateProgramWithSource(rig.context, 1, &broken, &length, &code);
            ClOwned<cl_program> owned(rig, created);
            const cl_int built = rig.api.BuildProgram(created, 1, &rig.device, "", nullptr, nullptr);
            cl::Kit::expect(problems, "clBuildProgram of a program with a syntax error", built, {CL_BUILD_PROGRAM_FAILURE});
            const std::string log = ClProgram::build_log(rig, created);
            if (log.empty()) {
                problems.add("the build log of a failed build is empty");
            }
            cl_build_status status = CL_BUILD_NONE;
            rig.api.GetProgramBuildInfo(created, rig.device, CL_PROGRAM_BUILD_STATUS, sizeof(status), &status, nullptr);
            if (status != CL_BUILD_ERROR) {
                problems.add("CL_PROGRAM_BUILD_STATUS of a failed build is " + std::to_string(status) + ", expected CL_BUILD_ERROR");
            }
            cl_kernel kernel = rig.api.CreateKernel(created, "broken", &code);
            if (kernel != nullptr) {
                rig.api.ReleaseKernel(kernel);
                problems.add("clCreateKernel succeeded on a program whose build failed");
            } else {
                cl::Kit::expect(problems, "clCreateKernel on a program whose build failed", code, {CL_INVALID_PROGRAM_EXECUTABLE});
            }
        }
        using CreateIl = cl_program(CL_API_CALL*)(cl_context, const void*, std::size_t, cl_int*);
        CreateIl create = nullptr;
        if (rig.version.at_least(2, 1) && rig.api.CreateProgramWithIL != nullptr) {
            create = rig.api.CreateProgramWithIL;
        } else {
            create = reinterpret_cast<CreateIl>(rig.api.extension(rig.platform, "clCreateProgramWithILKHR"));
        }
        if (create == nullptr) {
            context.note("clCreateProgramWithIL does not exist on this OpenCL " + rig.version.text() + " device");
            return;
        }
        const std::string il_version = rig.version.at_least(2, 1) || rig.has_extension("cl_khr_il_program") ? rig.text_info(CL_DEVICE_IL_VERSION) : std::string();
        const unsigned bits = rig.uint_info(CL_DEVICE_ADDRESS_BITS);
        const std::vector<std::uint32_t> module = ClSpirv::add_module(bits == 32 ? 32 : 64);
        cl_int code = CL_SUCCESS;
        cl_program program = create(rig.context, module.data(), module.size() * 4, &code);
        if (il_version.find("SPIR-V") == std::string::npos) {
            if (program != nullptr) {
                rig.api.ReleaseProgram(program);
                problems.add("CL_DEVICE_IL_VERSION is '" + il_version + "' yet clCreateProgramWithIL created a program");
            } else {
                cl::Kit::expect(problems, "clCreateProgramWithIL on a device without an IL", code, {CL_INVALID_OPERATION});
            }
            return;
        }
        if (program == nullptr || code != CL_SUCCESS) {
            problems.add("clCreateProgramWithIL with a valid SPIR-V module returned " + ClTools::error_name(code));
            return;
        }
        ClProgram from_il;
        from_il.adopt(rig, program);
        code = rig.api.BuildProgram(program, 1, &rig.device, "", nullptr, nullptr);
        if (code != CL_SUCCESS) {
            problems.add("clBuildProgram of the SPIR-V program returned " + ClTools::error_name(code) + "; log: " + ClProgram::build_log(rig, program));
            return;
        }
        std::string error;
        ClKernel kernel = from_il.kernel(ClSpirv::kAddKernel, error);
        const std::size_t items = 64;
        const std::vector<std::uint32_t> x = cl::Kit::words(0xD1, items);
        const std::vector<std::uint32_t> y = cl::Kit::words(0xD2, items);
        ClOwned<cl_mem> in_x = cl::Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, items * 4, const_cast<std::uint32_t*>(x.data()), code);
        ClOwned<cl_mem> in_y = cl::Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, items * 4, const_cast<std::uint32_t*>(y.data()), code);
        ClOwned<cl_mem> out = cl::Kit::buffer(rig, CL_MEM_READ_WRITE, items * 4, nullptr, code);
        if (!kernel || !in_x || !in_y || !out) {
            problems.add("the SPIR-V kernel or its buffers cannot be created: " + error);
            return;
        }
        code = kernel.set(in_x, in_y, out);
        if (code == CL_SUCCESS) {
            code = kernel.run(items);
        }
        std::vector<std::uint32_t> words(items, 0);
        if (code == CL_SUCCESS) {
            code = rig.api.EnqueueReadBuffer(rig.queue, out.get(), CL_TRUE, 0, items * 4, words.data(), 0, nullptr, nullptr);
        }
        rig.api.Finish(rig.queue);
        if (code != CL_SUCCESS) {
            problems.add("running the SPIR-V kernel failed with " + ClTools::error_name(code));
            return;
        }
        for (std::size_t i = 0; i < items; ++i) {
            if (words[i] != x[i] + y[i]) {
                problems.add("the SPIR-V kernel result " + std::to_string(i) + " is wrong");
                break;
            }
        }
        std::vector<std::uint32_t> garbage(32, 0xBADC0DEu);
        cl_program rejected = create(rig.context, garbage.data(), garbage.size() * 4, &code);
        if (rejected != nullptr) {
            rig.api.ReleaseProgram(rejected);
            problems.add("clCreateProgramWithIL accepted words that are not a SPIR-V module");
        } else {
            cl::Kit::expect(problems, "clCreateProgramWithIL with words that are not a SPIR-V module", code, {CL_INVALID_VALUE});
        }
    });
}

}  // namespace lsw::suites::sanity::shader
