#include "support/opencl_api.hpp"

#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>
#include <CL/cl_ext.h>

#include <cstring>

namespace lsw::suites {

namespace {

template <typename Function>
std::string text_info(Function function, void* object, unsigned name) {
    std::size_t size = 0;
    if (function(object, name, 0, nullptr, &size) != CL_SUCCESS || size == 0) {
        return {};
    }
    std::string value(size, '\0');
    if (function(object, name, size, value.data(), nullptr) != CL_SUCCESS) {
        return {};
    }
    while (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return value;
}

}  // namespace

bool OpenClApi::load(std::string& error) {
    if (!library_.load("OpenCL.dll")) {
        error = "OpenCL.dll cannot be loaded";
        return false;
    }
    get_platform_ids_ = library_.symbol<int(__stdcall*)(unsigned, void**, unsigned*)>("clGetPlatformIDs");
    get_platform_info_ =
        library_.symbol<int(__stdcall*)(void*, unsigned, std::size_t, void*, std::size_t*)>(
            "clGetPlatformInfo");
    get_device_ids_ = library_.symbol<int(__stdcall*)(void*, unsigned long long, unsigned, void**, unsigned*)>(
        "clGetDeviceIDs");
    get_device_info_ = library_.symbol<int(__stdcall*)(void*, unsigned, std::size_t, void*, std::size_t*)>(
        "clGetDeviceInfo");
    if (get_platform_ids_ == nullptr || get_platform_info_ == nullptr || get_device_ids_ == nullptr ||
        get_device_info_ == nullptr) {
        error = "OpenCL.dll lacks the platform and device entry points";
        return false;
    }
    return true;
}

bool OpenClApi::platforms(std::vector<OpenClPlatform>& out, std::string& error) const {
    unsigned count = 0;
    int result = get_platform_ids_(0, nullptr, &count);
    if (result == kPlatformNotFound || (result == CL_SUCCESS && count == 0)) {
        return true;
    }
    if (result != CL_SUCCESS) {
        error = "clGetPlatformIDs failed: " + std::to_string(result);
        return false;
    }
    std::vector<void*> ids(count);
    result = get_platform_ids_(count, ids.data(), nullptr);
    if (result != CL_SUCCESS) {
        error = "clGetPlatformIDs (fill) failed: " + std::to_string(result);
        return false;
    }
    for (void* platform_id : ids) {
        OpenClPlatform platform;
        platform.name = text_info(get_platform_info_, platform_id, CL_PLATFORM_NAME);
        platform.vendor = text_info(get_platform_info_, platform_id, CL_PLATFORM_VENDOR);
        platform.version = text_info(get_platform_info_, platform_id, CL_PLATFORM_VERSION);
        unsigned devices = 0;
        result = get_device_ids_(platform_id, CL_DEVICE_TYPE_ALL, 0, nullptr, &devices);
        if (result != CL_SUCCESS && result != CL_DEVICE_NOT_FOUND) {
            error = "clGetDeviceIDs failed: " + std::to_string(result);
            return false;
        }
        std::vector<void*> device_ids(result == CL_SUCCESS ? devices : 0);
        if (!device_ids.empty() && get_device_ids_(platform_id, CL_DEVICE_TYPE_ALL, devices,
                                                  device_ids.data(), nullptr) != CL_SUCCESS) {
            error = "clGetDeviceIDs (fill) failed";
            return false;
        }
        for (void* device_id : device_ids) {
            OpenClDevice device;
            device.name = text_info(get_device_info_, device_id, CL_DEVICE_NAME);
            device.vendor = text_info(get_device_info_, device_id, CL_DEVICE_VENDOR);
            device.version = text_info(get_device_info_, device_id, CL_DEVICE_VERSION);
            device.driver_version = text_info(get_device_info_, device_id, CL_DRIVER_VERSION);
            device.extensions = text_info(get_device_info_, device_id, CL_DEVICE_EXTENSIONS);
            cl_uint vendor_id = 0;
            get_device_info_(device_id, CL_DEVICE_VENDOR_ID, sizeof(vendor_id), &vendor_id, nullptr);
            device.vendor_id = vendor_id;
            cl_ulong value = 0;
            get_device_info_(device_id, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(value), &value, nullptr);
            device.global_memory = value;
            value = 0;
            get_device_info_(device_id, CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(value), &value, nullptr);
            device.max_allocation = value;
            device.luid_extension =
                (" " + device.extensions + " ").find(" cl_khr_device_uuid ") != std::string::npos;
            cl_bool valid = CL_FALSE;
            device.luid_valid_query_error = get_device_info_(
                device_id, CL_DEVICE_LUID_VALID_KHR, sizeof(valid), &valid, nullptr);
            device.luid_valid = device.luid_valid_query_error == CL_SUCCESS && valid != CL_FALSE;
            cl_uchar luid[CL_LUID_SIZE_KHR] = {};
            device.luid_query_error =
                get_device_info_(device_id, CL_DEVICE_LUID_KHR, sizeof(luid), luid, nullptr);
            if (device.luid_query_error == CL_SUCCESS) {
                std::memcpy(&device.luid, luid, sizeof(device.luid));
            }
            platform.devices.push_back(std::move(device));
        }
        out.push_back(std::move(platform));
    }
    return true;
}

}  // namespace lsw::suites
