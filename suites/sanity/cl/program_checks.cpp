#include "sanity/cl/program_checks.hpp"

#include "sanity/cl/kit.hpp"
#include "support/cl_spirv.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

namespace lsw::suites::sanity::cl {

namespace {

struct BuildNotice {
    std::atomic<int> calls{0};
    std::atomic<cl_program> program{nullptr};
};

void CL_CALLBACK build_done(cl_program program, void* user) {
    auto* notice = static_cast<BuildNotice*>(user);
    notice->program = program;
    ++notice->calls;
}

std::string short_path(const std::filesystem::path& path) {
    char buffer[MAX_PATH] = {};
    const DWORD length = ::GetShortPathNameA(path.string().c_str(), buffer, MAX_PATH);
    return length != 0 && length < MAX_PATH ? std::string(buffer) : path.string();
}

std::string build_option_echo(ClRig& rig, cl_program program) {
    std::size_t size = 0;
    if (rig.api.GetProgramBuildInfo(program, rig.device, CL_PROGRAM_BUILD_OPTIONS, 0, nullptr, &size) != CL_SUCCESS || size == 0) {
        return "<unavailable>";
    }
    std::vector<char> blob(size);
    rig.api.GetProgramBuildInfo(program, rig.device, CL_PROGRAM_BUILD_OPTIONS, size, blob.data(), nullptr);
    return ClTools::text_of(blob);
}

cl_build_status build_status(ClRig& rig, cl_program program) {
    cl_build_status status = CL_BUILD_NONE;
    rig.api.GetProgramBuildInfo(program, rig.device, CL_PROGRAM_BUILD_STATUS, sizeof(status), &status, nullptr);
    return status;
}

ClVersion max_c_version(const ClRig& rig) {
    ClVersion best;
    if (rig.version.at_least(3, 0)) {
        std::size_t size = 0;
        rig.api.GetDeviceInfo(rig.device, CL_DEVICE_OPENCL_C_ALL_VERSIONS, 0, nullptr, &size);
        std::vector<cl_name_version> list(size / sizeof(cl_name_version));
        if (!list.empty() && rig.api.GetDeviceInfo(rig.device, CL_DEVICE_OPENCL_C_ALL_VERSIONS, size, list.data(), nullptr) == CL_SUCCESS) {
            for (const cl_name_version& entry : list) {
                const ClVersion candidate{static_cast<int>(entry.version >> 22), static_cast<int>((entry.version >> 12) & 0x3ff)};
                if (best < candidate) {
                    best = candidate;
                }
            }
            return best;
        }
    }
    std::string rest;
    if (!ClTools::parse_version(rig.text_info(CL_DEVICE_OPENCL_C_VERSION), "OpenCL C ", best, &rest)) {
        best = ClVersion{1, 2};
    }
    return best;
}

bool log_names_line(const std::string& log, int line) {
    const std::string wanted = std::to_string(line);
    for (std::size_t at = log.find(wanted); at != std::string::npos; at = log.find(wanted, at + 1)) {
        const bool before = at == 0 || !std::isdigit(static_cast<unsigned char>(log[at - 1]));
        const bool after = at + wanted.size() >= log.size() || !std::isdigit(static_cast<unsigned char>(log[at + wanted.size()]));
        if (before && after) {
            return true;
        }
    }
    return false;
}

bool run_add(ClRig& rig, ClProgram& program, const char* name, Context& context, Problems& problems, const std::string& tag) {
    std::string error;
    ClKernel kernel = program.kernel(name, error);
    if (!kernel) {
        problems.add(tag + ": " + error);
        return false;
    }
    const std::size_t count = 4096;
    const std::vector<std::uint32_t> a = Kit::words(1500, count);
    const std::vector<std::uint32_t> b = Kit::words(1501, count);
    std::vector<std::uint32_t> expected(count);
    for (std::size_t i = 0; i < count; ++i) {
        expected[i] = a[i] + b[i];
    }
    cl_int code = CL_SUCCESS;
    ClOwned<cl_mem> ma = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 4, const_cast<std::uint32_t*>(a.data()), code);
    ClOwned<cl_mem> mb = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, count * 4, const_cast<std::uint32_t*>(b.data()), code);
    ClOwned<cl_mem> mc = Kit::buffer(rig, CL_MEM_WRITE_ONLY, count * 4, nullptr, code);
    if (!ma || !mb || !mc) {
        problems.add(tag + ": buffers: " + ClTools::error_name(code));
        return false;
    }
    code = kernel.set(ma, mb, mc);
    if (code == CL_SUCCESS) {
        code = kernel.run(count);
    }
    if (code != CL_SUCCESS) {
        problems.add(tag + ": running returned " + ClTools::error_name(code));
        return false;
    }
    std::vector<std::uint32_t> actual;
    if (!Kit::read_words(rig, mc.get(), count, actual, problems, tag)) {
        return false;
    }
    return Kit::same_words(context, problems, tag + " results", expected, actual);
}

}  // namespace

Outcome ProgramChecks::build_source_options(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("lsw-cl-inc-" + std::to_string(::GetCurrentProcessId()));
        std::error_code ignored;
        std::filesystem::create_directories(dir, ignored);
        {
            std::ofstream header(dir / "lsw_header.h");
            header << "#define LSW_HEADER_VALUE 4000u\n";
        }
        const std::string include_dir = short_path(dir);
        const std::string source =
            "#include \"lsw_header.h\"\n"
            "#ifndef LSW_VALUE\n#error LSW_VALUE is missing\n#endif\n"
            "__kernel void lsw_value(__global uint* o) { o[get_global_id(0)] = LSW_VALUE + LSW_HEADER_VALUE + (uint)get_global_id(0); }\n";
        auto run_value = [&](ClProgram& program, const std::string& tag, std::uint32_t base) {
            std::string error;
            ClKernel kernel = program.kernel("lsw_value", error);
            ClOwned<cl_mem> out = Kit::buffer(rig, CL_MEM_WRITE_ONLY, 256, problems, tag);
            if (!kernel || !out) {
                problems.add(tag + ": " + error);
                return;
            }
            if (kernel.set(out) != CL_SUCCESS || kernel.run(64) != CL_SUCCESS) {
                problems.add(tag + ": running failed");
                return;
            }
            std::vector<std::uint32_t> got;
            if (Kit::read_words(rig, out.get(), 64, got, problems, tag)) {
                std::vector<std::uint32_t> want(64);
                for (std::uint32_t i = 0; i < 64; ++i) {
                    want[i] = base + 4000u + i;
                }
                Kit::same_words(context, problems, tag + " results", want, got);
            }
        };
        auto build_with = [&](const std::string& options, std::uint32_t base) {
            ClProgram program;
            std::string error;
            if (!program.build(rig, source, options, error)) {
                problems.add("options '" + options + "': " + error);
                return;
            }
            if (build_status(rig, program.get()) != CL_BUILD_SUCCESS) {
                problems.add("options '" + options + "': CL_PROGRAM_BUILD_STATUS is not CL_BUILD_SUCCESS");
            }
            const std::string echoed = build_option_echo(rig, program.get());
            if (echoed != options) {
                problems.add("options '" + options + "': CL_PROGRAM_BUILD_OPTIONS reads '" + echoed + "'");
            }
            run_value(program, "options '" + options + "'", base);
        };
        const std::string base_options = "-D LSW_VALUE=123 -I " + include_dir;
        build_with(base_options, 123);
        build_with("-DLSW_VALUE=77 -I" + include_dir, 77);
        const ClVersion c_version = max_c_version(rig);
        context.measure("cl.max_c_version", c_version.text());
        std::vector<std::string> standards;
        if (rig.version.at_least(1, 2)) {
            standards.push_back("CL1.2");
        }
        for (const char* std_name : {"CL2.0", "CL3.0"}) {
            ClVersion wanted;
            wanted.major = std_name[2] - '0';
            wanted.minor = std_name[4] - '0';
            if (c_version.at_least(wanted.major, wanted.minor)) {
                standards.push_back(std_name);
            }
        }
        for (const std::string& standard : standards) {
            build_with(base_options + " -cl-std=" + standard, 123);
        }
        for (const char* flag : {"-cl-fast-relaxed-math", "-cl-mad-enable", "-cl-opt-disable", "-cl-no-signed-zeros", "-cl-unsafe-math-optimizations",
                                 "-cl-finite-math-only", "-cl-single-precision-constant", "-w", "-cl-denorms-are-zero"}) {
            build_with(base_options + " " + flag, 123);
        }
        {
            ClProgram program;
            std::string error;
            if (program.build(rig, source, base_options + " -cl-kernel-arg-info", error) && rig.version.at_least(1, 2)) {
                std::string kernel_error;
                ClKernel kernel = program.kernel("lsw_value", kernel_error);
                char name[64] = {};
                const cl_int code = kernel ? rig.api.GetKernelArgInfo(kernel.get(), 0, CL_KERNEL_ARG_NAME, sizeof(name), name, nullptr) : CL_INVALID_KERNEL;
                if (code != CL_SUCCESS || std::string(name) != "o") {
                    problems.add("-cl-kernel-arg-info: CL_KERNEL_ARG_NAME returned " + ClTools::error_name(code) + " '" + name + "', expected 'o'");
                }
            } else if (!error.empty()) {
                problems.add("-cl-kernel-arg-info: " + error);
            }
        }
        const struct {
            std::string options;
            const char* what;
        } rejected[] = {{base_options + " -cl-std=CL9.9", "an unknown -cl-std value"},
                        {base_options + " -no-such-option", "an unknown option"},
                        {base_options + " -cl-std=CL0.1", "a -cl-std value below every version"}};
        for (const auto& entry : rejected) {
            ClProgram program;
            std::string error;
            if (program.build(rig, source, entry.options, error)) {
                problems.add(std::string("clBuildProgram accepted ") + entry.what);
                continue;
            }
            const cl_int code = rig.api.BuildProgram(program.get(), 1, &rig.device, entry.options.c_str(), nullptr, nullptr);
            Kit::expect(problems, std::string("clBuildProgram with ") + entry.what, code, {CL_INVALID_BUILD_OPTIONS});
        }
        if (c_version.major < 3) {
            ClProgram program;
            std::string error;
            if (program.build(rig, source, base_options + " -cl-std=CL3.0", error)) {
                problems.add("-cl-std=CL3.0 was accepted although the highest OpenCL C version is " + c_version.text());
            } else {
                Kit::expect(problems, "-cl-std=CL3.0 on an OpenCL C " + c_version.text() + " compiler", rig.api.BuildProgram(program.get(), 1, &rig.device, (base_options + " -cl-std=CL3.0").c_str(), nullptr, nullptr), {CL_INVALID_BUILD_OPTIONS});
            }
        }
        {
            ClProgram program;
            std::string error;
            cl_int code = CL_SUCCESS;
            const char* text = ClSources::kStep;
            cl_program created = rig.api.CreateProgramWithSource(rig.context, 1, &text, nullptr, &code);
            program.adopt(rig, created);
            BuildNotice notice;
            code = rig.api.BuildProgram(created, 1, &rig.device, "", build_done, &notice);
            Kit::expect(problems, "clBuildProgram with a notification callback", code, {CL_SUCCESS});
            for (int wait = 0; wait < 500 && notice.calls.load() == 0; ++wait) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (notice.calls.load() != 1) {
                problems.add("the build notification callback ran " + std::to_string(notice.calls.load()) + " times, expected once");
            } else if (notice.program.load() != created) {
                problems.add("the build notification callback received a different program");
            }
            if (build_status(rig, created) != CL_BUILD_SUCCESS) {
                problems.add("CL_PROGRAM_BUILD_STATUS is not CL_BUILD_SUCCESS after the callback ran");
            }
        }
        std::filesystem::remove_all(dir, ignored);
        return problems.outcome();
    });
}

Outcome ProgramChecks::build_errors_logs(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        const std::string bad =
            "// lsw broken source\n"
            "__kernel void broken(__global uint* o) {\n"
            "    o[0] = lsw_undeclared_symbol_xyz;\n"
            "}\n";
        cl_int code = CL_SUCCESS;
        const char* text = bad.c_str();
        cl_program created = rig.api.CreateProgramWithSource(rig.context, 1, &text, nullptr, &code);
        if (created == nullptr) {
            return Outcome::fail("clCreateProgramWithSource returned " + ClTools::error_name(code));
        }
        ClOwned<cl_program> program(rig, created);
        code = rig.api.BuildProgram(created, 1, &rig.device, "", nullptr, nullptr);
        Kit::expect(problems, "clBuildProgram on invalid source", code, {CL_BUILD_PROGRAM_FAILURE});
        const std::string log = ClProgram::build_log(rig, created);
        if (log.empty()) {
            problems.add("the build log of a failed build is empty");
        } else if (!log_names_line(log, 3)) {
            problems.add("the build log does not name line 3: '" + log.substr(0, 300) + "'");
        }
        if (build_status(rig, created) != CL_BUILD_ERROR) {
            problems.add("CL_PROGRAM_BUILD_STATUS after a failed build is not CL_BUILD_ERROR");
        }
        cl_kernel kernel = rig.api.CreateKernel(created, "broken", &code);
        if (kernel != nullptr) {
            rig.api.ReleaseKernel(kernel);
            problems.add("a kernel was created from a program that failed to build");
        } else {
            Kit::expect(problems, "clCreateKernel on a failed program", code, {CL_INVALID_PROGRAM_EXECUTABLE});
        }
        cl_uint count = 0;
        Kit::expect(problems, "clCreateKernelsInProgram on a failed program", rig.api.CreateKernelsInProgram(created, 0, nullptr, &count), {CL_INVALID_PROGRAM_EXECUTABLE});
        ClProgram good;
        if (Kit::compile(rig, good, ClSources::kVectorAdd, "", problems, "valid program")) {
            cl_kernel missing = rig.api.CreateKernel(good.get(), "lsw_no_such_kernel", &code);
            if (missing != nullptr) {
                rig.api.ReleaseKernel(missing);
                problems.add("a kernel was created for a name the program does not define");
            } else {
                Kit::expect(problems, "clCreateKernel with an unknown name", code, {CL_INVALID_KERNEL_NAME});
            }
            cl_kernel none = rig.api.CreateKernel(good.get(), nullptr, &code);
            if (none != nullptr) {
                rig.api.ReleaseKernel(none);
                problems.add("clCreateKernel(NULL name) created a kernel");
            } else {
                Kit::expect(problems, "clCreateKernel with a NULL name", code, {CL_INVALID_VALUE});
            }
            cl_uint kernels = 0;
            Kit::expect(problems, "clCreateKernelsInProgram count", rig.api.CreateKernelsInProgram(good.get(), 0, nullptr, &kernels), {CL_SUCCESS});
            if (kernels != 2) {
                problems.add("clCreateKernelsInProgram counts " + std::to_string(kernels) + " kernels in a program that defines two");
            }
            std::size_t names_size = 0;
            if (rig.version.at_least(1, 2)) {
                code = rig.api.GetProgramInfo(good.get(), CL_PROGRAM_KERNEL_NAMES, 0, nullptr, &names_size);
                std::vector<char> names(names_size);
                if (code == CL_SUCCESS && names_size != 0) {
                    rig.api.GetProgramInfo(good.get(), CL_PROGRAM_KERNEL_NAMES, names_size, names.data(), nullptr);
                    const std::string listed = ClTools::text_of(names);
                    if (listed.find("lsw_add") == std::string::npos || listed.find("lsw_addf") == std::string::npos) {
                        problems.add("CL_PROGRAM_KERNEL_NAMES reads '" + listed + "'");
                    }
                } else {
                    problems.add("CL_PROGRAM_KERNEL_NAMES returned " + ClTools::error_name(code));
                }
            }
        }
        const char* empty = "";
        cl_program none_program = rig.api.CreateProgramWithSource(rig.context, 1, &empty, nullptr, &code);
        if (none_program != nullptr) {
            rig.api.ReleaseProgram(none_program);
        }
        Kit::expect(problems, "clBuildProgram(NULL program)", rig.api.BuildProgram(nullptr, 0, nullptr, nullptr, nullptr, nullptr), {CL_INVALID_PROGRAM});
        return problems.outcome();
    });
}

Outcome ProgramChecks::binary_roundtrip(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        ClProgram source_program;
        if (!Kit::compile(rig, source_program, ClSources::kVectorAdd, "", problems, "source build")) {
            return problems.outcome();
        }
        cl_uint devices = 0;
        rig.api.GetProgramInfo(source_program.get(), CL_PROGRAM_NUM_DEVICES, sizeof(devices), &devices, nullptr);
        if (devices != 1) {
            return Outcome::fail("CL_PROGRAM_NUM_DEVICES is " + std::to_string(devices) + " for a context with one device");
        }
        std::size_t binary_size = 0;
        cl_int code = rig.api.GetProgramInfo(source_program.get(), CL_PROGRAM_BINARY_SIZES, sizeof(binary_size), &binary_size, nullptr);
        if (code != CL_SUCCESS || binary_size == 0) {
            return Outcome::fail("CL_PROGRAM_BINARY_SIZES returned " + ClTools::error_name(code) + " with size " + std::to_string(binary_size));
        }
        std::vector<std::uint8_t> binary(binary_size);
        unsigned char* target = binary.data();
        code = rig.api.GetProgramInfo(source_program.get(), CL_PROGRAM_BINARIES, sizeof(target), &target, nullptr);
        if (code != CL_SUCCESS) {
            return Outcome::fail("CL_PROGRAM_BINARIES returned " + ClTools::error_name(code));
        }
        if (rig.version.at_least(1, 2)) {
            cl_program_binary_type type = 0;
            rig.api.GetProgramBuildInfo(source_program.get(), rig.device, CL_PROGRAM_BINARY_TYPE, sizeof(type), &type, nullptr);
            if (type != CL_PROGRAM_BINARY_TYPE_EXECUTABLE) {
                problems.add("CL_PROGRAM_BINARY_TYPE of a built source program is " + std::to_string(type));
            }
        }
        const unsigned char* pointer = binary.data();
        cl_int binary_status = CL_INVALID_VALUE;
        cl_program loaded = rig.api.CreateProgramWithBinary(rig.context, 1, &rig.device, &binary_size, &pointer, &binary_status, &code);
        if (loaded == nullptr || code != CL_SUCCESS || binary_status != CL_SUCCESS) {
            problems.add("clCreateProgramWithBinary of the program's own binary returned " + ClTools::error_name(code) + ", binary status " + ClTools::error_name(binary_status));
            if (loaded != nullptr) {
                rig.api.ReleaseProgram(loaded);
            }
            return problems.outcome();
        }
        ClProgram from_binary;
        from_binary.adopt(rig, loaded);
        code = rig.api.BuildProgram(loaded, 1, &rig.device, "", nullptr, nullptr);
        if (code != CL_SUCCESS) {
            problems.add("clBuildProgram on a program created from a binary returned " + ClTools::error_name(code) + "; " + ClProgram::build_log(rig, loaded));
            return problems.outcome();
        }
        run_add(rig, from_binary, "lsw_add", context, problems, "binary-loaded vector add");
        run_add(rig, source_program, "lsw_add", context, problems, "source-built vector add");

        auto expect_invalid = [&](std::vector<std::uint8_t> bytes, const std::string& what) {
            std::size_t length = bytes.size();
            const unsigned char* data = bytes.data();
            cl_int status = CL_SUCCESS;
            cl_int created_code = CL_SUCCESS;
            cl_program program = rig.api.CreateProgramWithBinary(rig.context, 1, &rig.device, &length, &data, &status, &created_code);
            ClOwned<cl_program> owned(rig, program);
            if (program == nullptr || created_code != CL_SUCCESS) {
                Kit::expect(problems, what + ": clCreateProgramWithBinary", created_code, {CL_INVALID_BINARY});
                if (status != CL_SUCCESS) {
                    Kit::expect(problems, what + ": binary status", status, {CL_INVALID_BINARY});
                }
                return;
            }
            if (status != CL_SUCCESS) {
                Kit::expect(problems, what + ": binary status", status, {CL_INVALID_BINARY});
                return;
            }
            const cl_int built = rig.api.BuildProgram(program, 1, &rig.device, "", nullptr, nullptr);
            if (built == CL_SUCCESS) {
                problems.add(what + ": was accepted and built");
            } else {
                Kit::expect(problems, what + ": clBuildProgram", built, {CL_INVALID_BINARY});
            }
        };
        expect_invalid(std::vector<std::uint8_t>(64, 0xAA), "a binary of 64 garbage bytes");
        expect_invalid(std::vector<std::uint8_t>(binary.begin(), binary.begin() + static_cast<std::ptrdiff_t>(binary.size() / 2)), "a binary cut to half its size");
        {
            std::vector<std::uint8_t> flipped = binary;
            for (std::size_t i = 0; i < std::min<std::size_t>(16, flipped.size()); ++i) {
                flipped[i] = static_cast<std::uint8_t>(~flipped[i]);
            }
            expect_invalid(flipped, "a binary with its first 16 bytes inverted");
        }
        std::size_t zero_length = 0;
        cl_int zero_status = 0;
        cl_program empty = rig.api.CreateProgramWithBinary(rig.context, 1, &rig.device, &zero_length, &pointer, &zero_status, &code);
        if (empty != nullptr) {
            rig.api.ReleaseProgram(empty);
            problems.add("clCreateProgramWithBinary accepted a zero-length binary");
        } else {
            Kit::expect(problems, "clCreateProgramWithBinary with a zero-length binary", code, {CL_INVALID_VALUE});
        }
        const unsigned char* null_binary = nullptr;
        cl_program missing = rig.api.CreateProgramWithBinary(rig.context, 1, &rig.device, &binary_size, &null_binary, &zero_status, &code);
        if (missing != nullptr) {
            rig.api.ReleaseProgram(missing);
            problems.add("clCreateProgramWithBinary accepted a NULL binary pointer");
        } else {
            Kit::expect(problems, "clCreateProgramWithBinary with a NULL binary", code, {CL_INVALID_VALUE});
        }
        context.measure("cl.binary_bytes", binary_size);
        return problems.outcome();
    });
}

Outcome ProgramChecks::il_spirv_caps(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        using CreateIl = cl_program(CL_API_CALL*)(cl_context, const void*, std::size_t, cl_int*);
        CreateIl create = nullptr;
        if (rig.version.at_least(2, 1) && rig.api.CreateProgramWithIL != nullptr) {
            create = rig.api.CreateProgramWithIL;
        } else {
            create = reinterpret_cast<CreateIl>(rig.api.extension(rig.platform, "clCreateProgramWithILKHR"));
        }
        if (create == nullptr) {
            return Context::skip("the device reports OpenCL " + rig.version.text() + " without cl_khr_il_program, so clCreateProgramWithIL does not exist");
        }
        const std::string il_version = rig.version.at_least(2, 1) || rig.has_extension("cl_khr_il_program") ? rig.text_info(CL_DEVICE_IL_VERSION) : std::string();
        context.measure("cl.il_version", il_version);
        const unsigned bits = rig.uint_info(CL_DEVICE_ADDRESS_BITS);
        const std::vector<std::uint32_t> module = ClSpirv::add_module(bits == 32 ? 32 : 64);
        cl_int code = CL_SUCCESS;
        cl_program program = create(rig.context, module.data(), module.size() * 4, &code);
        if (il_version.find("SPIR-V") == std::string::npos) {
            if (program != nullptr) {
                rig.api.ReleaseProgram(program);
                problems.add("CL_DEVICE_IL_VERSION is '" + il_version + "' yet clCreateProgramWithIL created a program");
            } else {
                Kit::expect(problems, "clCreateProgramWithIL on a device without an IL", code, {CL_INVALID_OPERATION});
            }
            return problems.outcome();
        }
        if (program == nullptr || code != CL_SUCCESS) {
            problems.add("clCreateProgramWithIL with a valid SPIR-V module (" + std::to_string(bits) + "-bit addressing, IL '" + il_version + "') returned " + ClTools::error_name(code));
            return problems.outcome();
        }
        ClProgram from_il;
        from_il.adopt(rig, program);
        std::size_t il_size = 0;
        code = rig.api.GetProgramInfo(program, CL_PROGRAM_IL, 0, nullptr, &il_size);
        if (code == CL_SUCCESS) {
            std::vector<std::uint8_t> returned(il_size);
            rig.api.GetProgramInfo(program, CL_PROGRAM_IL, il_size, returned.data(), nullptr);
            Kit::same_bytes(context, problems, "CL_PROGRAM_IL", module.data(), returned.data(), std::min(il_size, module.size() * 4));
            if (il_size != module.size() * 4) {
                problems.add("CL_PROGRAM_IL has " + std::to_string(il_size) + " bytes, the module has " + std::to_string(module.size() * 4));
            }
        } else {
            problems.add("CL_PROGRAM_IL returned " + ClTools::error_name(code));
        }
        code = rig.api.BuildProgram(program, 1, &rig.device, "", nullptr, nullptr);
        if (code != CL_SUCCESS) {
            problems.add("clBuildProgram on a SPIR-V program returned " + ClTools::error_name(code) + "; " + ClProgram::build_log(rig, program));
            return problems.outcome();
        }
        run_add(rig, from_il, ClSpirv::kAddKernel, context, problems, "SPIR-V vector add");
        const std::vector<std::uint32_t> garbage(64, 0x41414141u);
        cl_program bad = create(rig.context, garbage.data(), garbage.size() * 4, &code);
        if (bad != nullptr) {
            rig.api.ReleaseProgram(bad);
            problems.add("clCreateProgramWithIL accepted garbage as a module");
        } else {
            Kit::expect(problems, "clCreateProgramWithIL with garbage", code, {CL_INVALID_VALUE});
        }
        bad = create(rig.context, nullptr, 0, &code);
        if (bad != nullptr) {
            rig.api.ReleaseProgram(bad);
            problems.add("clCreateProgramWithIL accepted a NULL module");
        } else {
            Kit::expect(problems, "clCreateProgramWithIL with a NULL module", code, {CL_INVALID_VALUE});
        }
        return problems.outcome();
    });
}

Outcome ProgramChecks::compile_link(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        if (!rig.version.at_least(1, 2) || rig.api.CompileProgram == nullptr || rig.api.LinkProgram == nullptr) {
            return Context::skip("separate compilation and linking need OpenCL 1.2; the device reports " + rig.version.text());
        }
        const bool compiler = rig.uint_info(CL_DEVICE_COMPILER_AVAILABLE) != 0;
        const bool linker = rig.uint_info(CL_DEVICE_LINKER_AVAILABLE) != 0;
        auto create = [&](const std::string& text, cl_int& code) {
            const char* ptr = text.c_str();
            return ClOwned<cl_program>(rig, rig.api.CreateProgramWithSource(rig.context, 1, &ptr, nullptr, &code));
        };
        cl_int code = CL_SUCCESS;
        const std::string unit_a =
            "#include \"lsw_defs.h\"\n"
            "uint lsw_twice(uint);\n"
            "__kernel void use_twice(__global uint* d) { size_t i = get_global_id(0); d[i] = lsw_twice(d[i]) + LSW_BIAS; }\n";
        const std::string unit_b = "uint lsw_twice(uint x) { return x * 2u; }\n";
        const std::string header = "#define LSW_BIAS 5u\n";
        ClOwned<cl_program> a = create(unit_a, code);
        ClOwned<cl_program> b = create(unit_b, code);
        ClOwned<cl_program> h = create(header, code);
        if (!a || !b || !h) {
            return Outcome::fail("clCreateProgramWithSource returned " + ClTools::error_name(code));
        }
        const cl_program header_program = h.get();
        const char* header_name = "lsw_defs.h";
        if (!compiler) {
            Kit::expect(problems, "clCompileProgram without a compiler", rig.api.CompileProgram(a.get(), 1, &rig.device, "", 1, &header_program, &header_name, nullptr, nullptr), {CL_COMPILER_NOT_AVAILABLE});
            return problems.outcome();
        }
        code = rig.api.CompileProgram(a.get(), 1, &rig.device, "", 1, &header_program, &header_name, nullptr, nullptr);
        if (code != CL_SUCCESS) {
            problems.add("clCompileProgram with a header returned " + ClTools::error_name(code) + "; " + ClProgram::build_log(rig, a.get()));
            a = create("#define LSW_BIAS 5u\n" + unit_a.substr(unit_a.find('\n') + 1), code);
            code = a ? rig.api.CompileProgram(a.get(), 1, &rig.device, "", 0, nullptr, nullptr, nullptr, nullptr) : code;
            if (code != CL_SUCCESS) {
                problems.add("clCompileProgram without a header returned " + ClTools::error_name(code));
                return problems.outcome();
            }
        }
        code = rig.api.CompileProgram(b.get(), 1, &rig.device, "", 0, nullptr, nullptr, nullptr, nullptr);
        Kit::expect(problems, "clCompileProgram of the second unit", code, {CL_SUCCESS});
        cl_program_binary_type type = 0;
        rig.api.GetProgramBuildInfo(a.get(), rig.device, CL_PROGRAM_BINARY_TYPE, sizeof(type), &type, nullptr);
        if (type != CL_PROGRAM_BINARY_TYPE_COMPILED_OBJECT) {
            problems.add("CL_PROGRAM_BINARY_TYPE after clCompileProgram is " + std::to_string(type) + ", expected COMPILED_OBJECT");
        }
        if (!linker) {
            const cl_program both[2] = {a.get(), b.get()};
            cl_program unavailable = rig.api.LinkProgram(rig.context, 1, &rig.device, "", 2, both, nullptr, nullptr, &code);
            if (unavailable != nullptr) {
                rig.api.ReleaseProgram(unavailable);
            }
            Kit::expect(problems, "clLinkProgram without a linker", code, {CL_LINKER_NOT_AVAILABLE});
            return problems.outcome();
        }
        const cl_program units[2] = {a.get(), b.get()};
        cl_program linked_handle = rig.api.LinkProgram(rig.context, 1, &rig.device, "", 2, units, nullptr, nullptr, &code);
        ClProgram linked;
        if (linked_handle == nullptr || code != CL_SUCCESS) {
            problems.add("clLinkProgram of two units returned " + ClTools::error_name(code));
            if (linked_handle != nullptr) {
                rig.api.ReleaseProgram(linked_handle);
            }
        } else {
            linked.adopt(rig, linked_handle);
            rig.api.GetProgramBuildInfo(linked_handle, rig.device, CL_PROGRAM_BINARY_TYPE, sizeof(type), &type, nullptr);
            if (type != CL_PROGRAM_BINARY_TYPE_EXECUTABLE) {
                problems.add("CL_PROGRAM_BINARY_TYPE after clLinkProgram is " + std::to_string(type) + ", expected EXECUTABLE");
            }
            std::string error;
            ClKernel kernel = linked.kernel("use_twice", error);
            const std::size_t count = 2048;
            std::vector<std::uint32_t> data = Kit::words(1600, count);
            ClOwned<cl_mem> buffer = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, count * 4, data.data(), code);
            if (!kernel || !buffer || kernel.set(buffer) != CL_SUCCESS || kernel.run(count) != CL_SUCCESS) {
                problems.add("running the linked kernel failed: " + error + " " + ClTools::error_name(code));
            } else {
                std::vector<std::uint32_t> expected(count);
                for (std::size_t i = 0; i < count; ++i) {
                    expected[i] = data[i] * 2 + 5;
                }
                std::vector<std::uint32_t> actual;
                if (Kit::read_words(rig, buffer.get(), count, actual, problems, "linked kernel")) {
                    Kit::same_words(context, problems, "linked kernel results", expected, actual);
                }
            }
        }
        const cl_program only_a[1] = {a.get()};
        cl_program failed = rig.api.LinkProgram(rig.context, 1, &rig.device, "", 1, only_a, nullptr, nullptr, &code);
        Kit::expect(problems, "clLinkProgram with an unresolved symbol", code, {CL_LINK_PROGRAM_FAILURE});
        if (failed == nullptr) {
            problems.add("clLinkProgram with an unresolved symbol returned no program to read the build log from");
        } else {
            ClOwned<cl_program> owned(rig, failed);
            if (ClProgram::build_log(rig, failed).empty()) {
                problems.add("the link log of an unresolved symbol is empty");
            }
        }
        ClOwned<cl_program> broken = create("__kernel void k(__global uint* o) { o[0] = lsw_missing; }\n", code);
        if (broken) {
            code = rig.api.CompileProgram(broken.get(), 1, &rig.device, "", 0, nullptr, nullptr, nullptr, nullptr);
            Kit::expect(problems, "clCompileProgram on invalid source", code, {CL_COMPILE_PROGRAM_FAILURE});
            if (ClProgram::build_log(rig, broken.get()).empty()) {
                problems.add("the compile log of invalid source is empty");
            }
        }
        Kit::expect(problems, "clCompileProgram with an unknown option", rig.api.CompileProgram(b.get(), 1, &rig.device, "-no-such-option", 0, nullptr, nullptr, nullptr, nullptr), {CL_INVALID_COMPILER_OPTIONS});
        Kit::expect(problems, "clCompileProgram with headers but a zero count", rig.api.CompileProgram(b.get(), 1, &rig.device, "", 0, &header_program, &header_name, nullptr, nullptr), {CL_INVALID_VALUE});
        cl_program bad_link = rig.api.LinkProgram(rig.context, 1, &rig.device, "-no-such-link-option", 2, units, nullptr, nullptr, &code);
        if (bad_link != nullptr) {
            rig.api.ReleaseProgram(bad_link);
        }
        Kit::expect(problems, "clLinkProgram with an unknown option", code, {CL_INVALID_LINKER_OPTIONS});
        return problems.outcome();
    });
}

}  // namespace lsw::suites::sanity::cl
