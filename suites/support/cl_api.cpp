#include "support/cl_api.hpp"

namespace lsw::suites {

bool ClApi::load(std::string& error) {
    if (!library_.load("OpenCL.dll")) {
        error = "OpenCL.dll cannot be loaded (error " + std::to_string(::GetLastError()) + ")";
        return false;
    }
#define LSW_CL_BIND(name) name = library_.symbol<decltype(name)>("cl" #name);
    LSW_CL_FUNCTIONS(LSW_CL_BIND)
#undef LSW_CL_BIND
    if (GetPlatformIDs == nullptr || GetDeviceIDs == nullptr || GetDeviceInfo == nullptr || CreateContext == nullptr ||
        CreateBuffer == nullptr || EnqueueNDRangeKernel == nullptr || Finish == nullptr ||
        CreateProgramWithSource == nullptr || BuildProgram == nullptr) {
        error = "OpenCL.dll lacks core entry points";
        return false;
    }
    return true;
}

}  // namespace lsw::suites
