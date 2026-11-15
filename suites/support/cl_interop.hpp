#pragma once

#include "support/cl_rig.hpp"

#include <windows.h>
#include <d3d10.h>
#include <d3d11.h>
#include <d3d9.h>

#include <CL/cl_d3d10.h>
#include <CL/cl_d3d11.h>
#include <CL/cl_dx9_media_sharing.h>
#include <CL/cl_gl.h>

namespace lsw::suites {
class ClInterop {
public:
    void load(const ClRig& rig);

    [[nodiscard]] static void* find(const ClRig& rig, const char* name, bool* from_platform = nullptr);

    clGetGLContextInfoKHR_fn GetGLContextInfo = nullptr;
    clCreateFromGLBuffer_fn CreateFromGLBuffer = nullptr;
    clCreateFromGLTexture_fn CreateFromGLTexture = nullptr;
    clCreateFromGLRenderbuffer_fn CreateFromGLRenderbuffer = nullptr;
    clGetGLObjectInfo_fn GetGLObjectInfo = nullptr;
    clEnqueueAcquireGLObjects_fn AcquireGLObjects = nullptr;
    clEnqueueReleaseGLObjects_fn ReleaseGLObjects = nullptr;
    clCreateEventFromGLsyncKHR_fn CreateEventFromGLsync = nullptr;

    clGetDeviceIDsFromD3D10KHR_fn GetDeviceIDsFromD3D10 = nullptr;
    clCreateFromD3D10BufferKHR_fn CreateFromD3D10Buffer = nullptr;
    clCreateFromD3D10Texture2DKHR_fn CreateFromD3D10Texture2D = nullptr;
    clCreateFromD3D10Texture3DKHR_fn CreateFromD3D10Texture3D = nullptr;
    clEnqueueAcquireD3D10ObjectsKHR_fn AcquireD3D10Objects = nullptr;
    clEnqueueReleaseD3D10ObjectsKHR_fn ReleaseD3D10Objects = nullptr;

    clGetDeviceIDsFromD3D11KHR_fn GetDeviceIDsFromD3D11 = nullptr;
    clCreateFromD3D11BufferKHR_fn CreateFromD3D11Buffer = nullptr;
    clCreateFromD3D11Texture2DKHR_fn CreateFromD3D11Texture2D = nullptr;
    clCreateFromD3D11Texture3DKHR_fn CreateFromD3D11Texture3D = nullptr;
    clEnqueueAcquireD3D11ObjectsKHR_fn AcquireD3D11Objects = nullptr;
    clEnqueueReleaseD3D11ObjectsKHR_fn ReleaseD3D11Objects = nullptr;

    clGetDeviceIDsFromDX9MediaAdapterKHR_fn GetDeviceIDsFromDX9 = nullptr;
    clCreateFromDX9MediaSurfaceKHR_fn CreateFromDX9Surface = nullptr;
    clEnqueueAcquireDX9MediaSurfacesKHR_fn AcquireDX9Surfaces = nullptr;
    clEnqueueReleaseDX9MediaSurfacesKHR_fn ReleaseDX9Surfaces = nullptr;
};
}  // namespace lsw::suites
