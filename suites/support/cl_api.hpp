#pragma once

#include "support/dyn_library.hpp"

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 300
#endif
#define CL_USE_DEPRECATED_OPENCL_1_0_APIS
#define CL_USE_DEPRECATED_OPENCL_1_1_APIS
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#define CL_USE_DEPRECATED_OPENCL_2_0_APIS
#define CL_USE_DEPRECATED_OPENCL_2_1_APIS
#define CL_USE_DEPRECATED_OPENCL_2_2_APIS
#include <CL/cl.h>
#include <CL/cl_ext.h>

#include <string>

namespace lsw::suites {
#define LSW_CL_FUNCTIONS(X)                                                                                         \
    X(GetPlatformIDs) X(GetPlatformInfo) X(GetDeviceIDs) X(GetDeviceInfo) X(CreateSubDevices) X(RetainDevice)       \
    X(ReleaseDevice) X(CreateContext) X(RetainContext) X(ReleaseContext) X(GetContextInfo)                          \
    X(CreateCommandQueue) X(CreateCommandQueueWithProperties) X(RetainCommandQueue) X(ReleaseCommandQueue)         \
    X(GetCommandQueueInfo) X(CreateBuffer) X(CreateSubBuffer) X(CreateImage) X(RetainMemObject)                    \
    X(ReleaseMemObject) X(GetSupportedImageFormats) X(GetMemObjectInfo) X(GetImageInfo) X(SVMAlloc) X(SVMFree)     \
    X(CreateSampler) X(CreateSamplerWithProperties) X(RetainSampler) X(ReleaseSampler) X(GetSamplerInfo)          \
    X(CreateProgramWithSource) X(CreateProgramWithBinary) X(CreateProgramWithIL) X(RetainProgram)                  \
    X(ReleaseProgram) X(BuildProgram) X(CompileProgram) X(LinkProgram) X(GetProgramInfo) X(GetProgramBuildInfo)    \
    X(CreateKernel) X(CreateKernelsInProgram) X(RetainKernel) X(ReleaseKernel) X(SetKernelArg)                     \
    X(SetKernelArgSVMPointer) X(SetKernelExecInfo) X(GetKernelInfo) X(GetKernelWorkGroupInfo)                      \
    X(GetKernelArgInfo) X(WaitForEvents) X(GetEventInfo) X(CreateUserEvent) X(RetainEvent) X(ReleaseEvent)        \
    X(SetUserEventStatus) X(SetEventCallback) X(GetEventProfilingInfo) X(Flush) X(Finish) X(EnqueueReadBuffer)    \
    X(EnqueueWriteBuffer) X(EnqueueCopyBuffer) X(EnqueueReadBufferRect) X(EnqueueWriteBufferRect)                  \
    X(EnqueueCopyBufferRect) X(EnqueueFillBuffer) X(EnqueueReadImage) X(EnqueueWriteImage) X(EnqueueCopyImage)    \
    X(EnqueueFillImage) X(EnqueueCopyImageToBuffer) X(EnqueueCopyBufferToImage) X(EnqueueMapBuffer)               \
    X(EnqueueMapImage) X(EnqueueUnmapMemObject) X(EnqueueMigrateMemObjects) X(EnqueueNDRangeKernel)               \
    X(EnqueueMarkerWithWaitList) X(EnqueueBarrierWithWaitList) X(EnqueueSVMFree) X(EnqueueSVMMemcpy)               \
    X(EnqueueSVMMemFill) X(EnqueueSVMMap) X(EnqueueSVMUnmap) X(GetExtensionFunctionAddressForPlatform)

class ClApi {
public:
    [[nodiscard]] bool load(std::string& error);

#define LSW_CL_MEMBER(name) decltype(&cl##name) name = nullptr;
    LSW_CL_FUNCTIONS(LSW_CL_MEMBER)
#undef LSW_CL_MEMBER

    [[nodiscard]] void* export_symbol(const char* name) const { return library_.symbol<void*>(name); }

    [[nodiscard]] void* extension(cl_platform_id platform, const char* name) const {
        return GetExtensionFunctionAddressForPlatform != nullptr ? GetExtensionFunctionAddressForPlatform(platform, name)
                                                                 : nullptr;
    }

private:
    DynLibrary library_;
};
}  // namespace lsw::suites
