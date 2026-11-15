#include "support/cl_interop.hpp"

namespace lsw::suites {

void* ClInterop::find(const ClRig& rig, const char* name, bool* from_platform) {
    void* address = rig.api.extension(rig.platform, name);
    if (from_platform != nullptr) {
        *from_platform = address != nullptr;
    }
    return address != nullptr ? address : rig.api.export_symbol(name);
}

void ClInterop::load(const ClRig& rig) {
#define LSW_CL_FIND(member, name) member = reinterpret_cast<decltype(member)>(find(rig, name))
    LSW_CL_FIND(GetGLContextInfo, "clGetGLContextInfoKHR");
    LSW_CL_FIND(CreateFromGLBuffer, "clCreateFromGLBuffer");
    LSW_CL_FIND(CreateFromGLTexture, "clCreateFromGLTexture");
    LSW_CL_FIND(CreateFromGLRenderbuffer, "clCreateFromGLRenderbuffer");
    LSW_CL_FIND(GetGLObjectInfo, "clGetGLObjectInfo");
    LSW_CL_FIND(AcquireGLObjects, "clEnqueueAcquireGLObjects");
    LSW_CL_FIND(ReleaseGLObjects, "clEnqueueReleaseGLObjects");
    LSW_CL_FIND(CreateEventFromGLsync, "clCreateEventFromGLsyncKHR");
    LSW_CL_FIND(GetDeviceIDsFromD3D10, "clGetDeviceIDsFromD3D10KHR");
    LSW_CL_FIND(CreateFromD3D10Buffer, "clCreateFromD3D10BufferKHR");
    LSW_CL_FIND(CreateFromD3D10Texture2D, "clCreateFromD3D10Texture2DKHR");
    LSW_CL_FIND(CreateFromD3D10Texture3D, "clCreateFromD3D10Texture3DKHR");
    LSW_CL_FIND(AcquireD3D10Objects, "clEnqueueAcquireD3D10ObjectsKHR");
    LSW_CL_FIND(ReleaseD3D10Objects, "clEnqueueReleaseD3D10ObjectsKHR");
    LSW_CL_FIND(GetDeviceIDsFromD3D11, "clGetDeviceIDsFromD3D11KHR");
    LSW_CL_FIND(CreateFromD3D11Buffer, "clCreateFromD3D11BufferKHR");
    LSW_CL_FIND(CreateFromD3D11Texture2D, "clCreateFromD3D11Texture2DKHR");
    LSW_CL_FIND(CreateFromD3D11Texture3D, "clCreateFromD3D11Texture3DKHR");
    LSW_CL_FIND(AcquireD3D11Objects, "clEnqueueAcquireD3D11ObjectsKHR");
    LSW_CL_FIND(ReleaseD3D11Objects, "clEnqueueReleaseD3D11ObjectsKHR");
    LSW_CL_FIND(GetDeviceIDsFromDX9, "clGetDeviceIDsFromDX9MediaAdapterKHR");
    LSW_CL_FIND(CreateFromDX9Surface, "clCreateFromDX9MediaSurfaceKHR");
    LSW_CL_FIND(AcquireDX9Surfaces, "clEnqueueAcquireDX9MediaSurfacesKHR");
    LSW_CL_FIND(ReleaseDX9Surfaces, "clEnqueueReleaseDX9MediaSurfacesKHR");
#undef LSW_CL_FIND
}

}  // namespace lsw::suites
