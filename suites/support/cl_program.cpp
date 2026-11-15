#include "support/cl_program.hpp"

namespace lsw::suites {

std::string ClProgram::build_log(ClRig& rig, cl_program program) {
    std::size_t size = 0;
    if (rig.api.GetProgramBuildInfo(program, rig.device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &size) != CL_SUCCESS || size == 0) {
        return {};
    }
    std::vector<char> blob(size);
    if (rig.api.GetProgramBuildInfo(program, rig.device, CL_PROGRAM_BUILD_LOG, size, blob.data(), nullptr) != CL_SUCCESS) {
        return {};
    }
    return ClTools::text_of(blob);
}

bool ClProgram::build(ClRig& rig, const std::string& source, const std::string& options, std::string& error) {
    rig_ = &rig;
    cl_int code = CL_SUCCESS;
    const char* text = source.c_str();
    const std::size_t length = source.size();
    cl_program created = rig.api.CreateProgramWithSource(rig.context, 1, &text, &length, &code);
    if (created == nullptr || code != CL_SUCCESS) {
        error = "clCreateProgramWithSource failed: " + ClTools::error_name(code);
        return false;
    }
    program_.adopt(rig, created);
    code = rig.api.BuildProgram(created, 1, &rig.device, options.c_str(), nullptr, nullptr);
    if (code != CL_SUCCESS) {
        std::string log = build_log(rig, created);
        if (log.size() > 1500) {
            log.resize(1500);
        }
        error = "clBuildProgram(" + options + ") failed: " + ClTools::error_name(code) + "; build log: " + log;
        return false;
    }
    return true;
}

ClKernel ClProgram::kernel(const char* name, std::string& error, cl_int* code_out) {
    cl_int code = CL_SUCCESS;
    cl_kernel created = rig_->api.CreateKernel(program_.get(), name, &code);
    if (code_out != nullptr) {
        *code_out = code;
    }
    if (created == nullptr || code != CL_SUCCESS) {
        error = std::string("clCreateKernel(") + name + ") failed: " + ClTools::error_name(code);
        return {};
    }
    return ClKernel(*rig_, created);
}

}  // namespace lsw::suites
