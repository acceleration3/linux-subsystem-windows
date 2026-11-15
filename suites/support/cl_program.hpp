#pragma once

#include "support/cl_kernel.hpp"

namespace lsw::suites {
class ClProgram {
public:
    [[nodiscard]] bool build(ClRig& rig, const std::string& source, const std::string& options, std::string& error);

    [[nodiscard]] ClKernel kernel(const char* name, std::string& error, cl_int* code = nullptr);

    [[nodiscard]] static std::string build_log(ClRig& rig, cl_program program);

    [[nodiscard]] cl_program get() const { return program_.get(); }

    void adopt(ClRig& rig, cl_program program) {
        rig_ = &rig;
        program_.adopt(rig, program);
    }

private:
    ClRig* rig_ = nullptr;
    ClOwned<cl_program> program_;
};
}  // namespace lsw::suites
