#include "sanity/cl/device_checks.hpp"

#include "sanity/cl/kit.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <set>
#include <thread>

namespace lsw::suites::sanity::cl {

namespace {

enum class Shape { Uint, Ulong, Bool, Size, Text, Pointer, SizeArray, Bitfield, PropertyArray, NameVersions };

struct Param {
    cl_device_info id;
    const char* name;
    Shape shape;
    int major;
    int minor;
};

#define LSW_PARAM(id, shape, major, minor) {id, #id, Shape::shape, major, minor}

const Param kParams[] = {
    LSW_PARAM(CL_DEVICE_TYPE, Bitfield, 1, 0),
    LSW_PARAM(CL_DEVICE_VENDOR_ID, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_MAX_COMPUTE_UNITS, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_MAX_WORK_GROUP_SIZE, Size, 1, 0),
    LSW_PARAM(CL_DEVICE_MAX_WORK_ITEM_SIZES, SizeArray, 1, 0),
    LSW_PARAM(CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR, Uint, 1, 1),
    LSW_PARAM(CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT, Uint, 1, 1),
    LSW_PARAM(CL_DEVICE_NATIVE_VECTOR_WIDTH_INT, Uint, 1, 1),
    LSW_PARAM(CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG, Uint, 1, 1),
    LSW_PARAM(CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT, Uint, 1, 1),
    LSW_PARAM(CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE, Uint, 1, 1),
    LSW_PARAM(CL_DEVICE_MAX_CLOCK_FREQUENCY, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_ADDRESS_BITS, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_MAX_READ_IMAGE_ARGS, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_MAX_WRITE_IMAGE_ARGS, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_MAX_MEM_ALLOC_SIZE, Ulong, 1, 0),
    LSW_PARAM(CL_DEVICE_IMAGE2D_MAX_WIDTH, Size, 1, 0),
    LSW_PARAM(CL_DEVICE_IMAGE2D_MAX_HEIGHT, Size, 1, 0),
    LSW_PARAM(CL_DEVICE_IMAGE3D_MAX_WIDTH, Size, 1, 0),
    LSW_PARAM(CL_DEVICE_IMAGE3D_MAX_HEIGHT, Size, 1, 0),
    LSW_PARAM(CL_DEVICE_IMAGE3D_MAX_DEPTH, Size, 1, 0),
    LSW_PARAM(CL_DEVICE_IMAGE_SUPPORT, Bool, 1, 0),
    LSW_PARAM(CL_DEVICE_MAX_PARAMETER_SIZE, Size, 1, 0),
    LSW_PARAM(CL_DEVICE_MAX_SAMPLERS, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_MEM_BASE_ADDR_ALIGN, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_SINGLE_FP_CONFIG, Bitfield, 1, 0),
    LSW_PARAM(CL_DEVICE_DOUBLE_FP_CONFIG, Bitfield, 1, 2),
    LSW_PARAM(CL_DEVICE_GLOBAL_MEM_CACHE_TYPE, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_GLOBAL_MEM_CACHE_SIZE, Ulong, 1, 0),
    LSW_PARAM(CL_DEVICE_GLOBAL_MEM_SIZE, Ulong, 1, 0),
    LSW_PARAM(CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE, Ulong, 1, 0),
    LSW_PARAM(CL_DEVICE_MAX_CONSTANT_ARGS, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_LOCAL_MEM_TYPE, Uint, 1, 0),
    LSW_PARAM(CL_DEVICE_LOCAL_MEM_SIZE, Ulong, 1, 0),
    LSW_PARAM(CL_DEVICE_ERROR_CORRECTION_SUPPORT, Bool, 1, 0),
    LSW_PARAM(CL_DEVICE_PROFILING_TIMER_RESOLUTION, Size, 1, 0),
    LSW_PARAM(CL_DEVICE_ENDIAN_LITTLE, Bool, 1, 0),
    LSW_PARAM(CL_DEVICE_AVAILABLE, Bool, 1, 0),
    LSW_PARAM(CL_DEVICE_COMPILER_AVAILABLE, Bool, 1, 0),
    LSW_PARAM(CL_DEVICE_EXECUTION_CAPABILITIES, Bitfield, 1, 0),
    LSW_PARAM(CL_DEVICE_QUEUE_PROPERTIES, Bitfield, 1, 0),
    LSW_PARAM(CL_DEVICE_NAME, Text, 1, 0),
    LSW_PARAM(CL_DEVICE_VENDOR, Text, 1, 0),
    LSW_PARAM(CL_DRIVER_VERSION, Text, 1, 0),
    LSW_PARAM(CL_DEVICE_PROFILE, Text, 1, 0),
    LSW_PARAM(CL_DEVICE_VERSION, Text, 1, 0),
    LSW_PARAM(CL_DEVICE_EXTENSIONS, Text, 1, 0),
    LSW_PARAM(CL_DEVICE_PLATFORM, Pointer, 1, 0),
    LSW_PARAM(CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF, Uint, 1, 1),
    LSW_PARAM(CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF, Uint, 1, 1),
    LSW_PARAM(CL_DEVICE_OPENCL_C_VERSION, Text, 1, 1),
    LSW_PARAM(CL_DEVICE_LINKER_AVAILABLE, Bool, 1, 2),
    LSW_PARAM(CL_DEVICE_BUILT_IN_KERNELS, Text, 1, 2),
    LSW_PARAM(CL_DEVICE_IMAGE_MAX_BUFFER_SIZE, Size, 1, 2),
    LSW_PARAM(CL_DEVICE_IMAGE_MAX_ARRAY_SIZE, Size, 1, 2),
    LSW_PARAM(CL_DEVICE_PARENT_DEVICE, Pointer, 1, 2),
    LSW_PARAM(CL_DEVICE_PARTITION_MAX_SUB_DEVICES, Uint, 1, 2),
    LSW_PARAM(CL_DEVICE_PARTITION_PROPERTIES, PropertyArray, 1, 2),
    LSW_PARAM(CL_DEVICE_PARTITION_AFFINITY_DOMAIN, Bitfield, 1, 2),
    LSW_PARAM(CL_DEVICE_PARTITION_TYPE, PropertyArray, 1, 2),
    LSW_PARAM(CL_DEVICE_REFERENCE_COUNT, Uint, 1, 2),
    LSW_PARAM(CL_DEVICE_PREFERRED_INTEROP_USER_SYNC, Bool, 1, 2),
    LSW_PARAM(CL_DEVICE_PRINTF_BUFFER_SIZE, Size, 1, 2),
    LSW_PARAM(CL_DEVICE_IMAGE_PITCH_ALIGNMENT, Uint, 2, 0),
    LSW_PARAM(CL_DEVICE_IMAGE_BASE_ADDRESS_ALIGNMENT, Uint, 2, 0),
    LSW_PARAM(CL_DEVICE_MAX_READ_WRITE_IMAGE_ARGS, Uint, 2, 0),
    LSW_PARAM(CL_DEVICE_MAX_GLOBAL_VARIABLE_SIZE, Size, 2, 0),
    LSW_PARAM(CL_DEVICE_QUEUE_ON_DEVICE_PROPERTIES, Bitfield, 2, 0),
    LSW_PARAM(CL_DEVICE_QUEUE_ON_DEVICE_PREFERRED_SIZE, Uint, 2, 0),
    LSW_PARAM(CL_DEVICE_QUEUE_ON_DEVICE_MAX_SIZE, Uint, 2, 0),
    LSW_PARAM(CL_DEVICE_MAX_ON_DEVICE_QUEUES, Uint, 2, 0),
    LSW_PARAM(CL_DEVICE_MAX_ON_DEVICE_EVENTS, Uint, 2, 0),
    LSW_PARAM(CL_DEVICE_SVM_CAPABILITIES, Bitfield, 2, 0),
    LSW_PARAM(CL_DEVICE_GLOBAL_VARIABLE_PREFERRED_TOTAL_SIZE, Size, 2, 0),
    LSW_PARAM(CL_DEVICE_MAX_PIPE_ARGS, Uint, 2, 0),
    LSW_PARAM(CL_DEVICE_PIPE_MAX_ACTIVE_RESERVATIONS, Uint, 2, 0),
    LSW_PARAM(CL_DEVICE_PIPE_MAX_PACKET_SIZE, Uint, 2, 0),
    LSW_PARAM(CL_DEVICE_PREFERRED_PLATFORM_ATOMIC_ALIGNMENT, Uint, 2, 0),
    LSW_PARAM(CL_DEVICE_PREFERRED_GLOBAL_ATOMIC_ALIGNMENT, Uint, 2, 0),
    LSW_PARAM(CL_DEVICE_PREFERRED_LOCAL_ATOMIC_ALIGNMENT, Uint, 2, 0),
    LSW_PARAM(CL_DEVICE_IL_VERSION, Text, 2, 1),
    LSW_PARAM(CL_DEVICE_MAX_NUM_SUB_GROUPS, Uint, 2, 1),
    LSW_PARAM(CL_DEVICE_SUB_GROUP_INDEPENDENT_FORWARD_PROGRESS, Bool, 2, 1),
    LSW_PARAM(CL_DEVICE_NUMERIC_VERSION, Uint, 3, 0),
    LSW_PARAM(CL_DEVICE_EXTENSIONS_WITH_VERSION, NameVersions, 3, 0),
    LSW_PARAM(CL_DEVICE_ILS_WITH_VERSION, NameVersions, 3, 0),
    LSW_PARAM(CL_DEVICE_BUILT_IN_KERNELS_WITH_VERSION, NameVersions, 3, 0),
    LSW_PARAM(CL_DEVICE_ATOMIC_MEMORY_CAPABILITIES, Bitfield, 3, 0),
    LSW_PARAM(CL_DEVICE_ATOMIC_FENCE_CAPABILITIES, Bitfield, 3, 0),
    LSW_PARAM(CL_DEVICE_NON_UNIFORM_WORK_GROUP_SUPPORT, Bool, 3, 0),
    LSW_PARAM(CL_DEVICE_OPENCL_C_ALL_VERSIONS, NameVersions, 3, 0),
    LSW_PARAM(CL_DEVICE_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, Size, 3, 0),
    LSW_PARAM(CL_DEVICE_WORK_GROUP_COLLECTIVE_FUNCTIONS_SUPPORT, Bool, 3, 0),
    LSW_PARAM(CL_DEVICE_GENERIC_ADDRESS_SPACE_SUPPORT, Bool, 3, 0),
    LSW_PARAM(CL_DEVICE_OPENCL_C_FEATURES, NameVersions, 3, 0),
    LSW_PARAM(CL_DEVICE_DEVICE_ENQUEUE_CAPABILITIES, Bitfield, 3, 0),
    LSW_PARAM(CL_DEVICE_PIPE_SUPPORT, Bool, 3, 0),
    LSW_PARAM(CL_DEVICE_LATEST_CONFORMANCE_VERSION_PASSED, Text, 3, 0),
};

#undef LSW_PARAM

std::size_t fixed_size(Shape shape) {
    switch (shape) {
    case Shape::Uint:
    case Shape::Bool:
        return 4;
    case Shape::Ulong:
    case Shape::Bitfield:
        return 8;
    case Shape::Size:
        return sizeof(std::size_t);
    case Shape::Pointer:
        return sizeof(void*);
    default:
        return 0;
    }
}

void query_param(ClRig& rig, const Param& param, Problems& problems) {
    std::size_t size = 0;
    const cl_int code = rig.api.GetDeviceInfo(rig.device, param.id, 0, nullptr, &size);
    if (code != CL_SUCCESS) {
        problems.add(std::string(param.name) + " size query returned " + ClTools::error_name(code));
        return;
    }
    const std::size_t fixed = fixed_size(param.shape);
    if (fixed != 0 && size != fixed) {
        problems.add(std::string(param.name) + " reports size " + std::to_string(size) + ", documented " + std::to_string(fixed));
        return;
    }
    if (param.shape == Shape::SizeArray) {
        const std::size_t want = rig.uint_info(CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS) * sizeof(std::size_t);
        if (size != want) {
            problems.add(std::string(param.name) + " reports size " + std::to_string(size) + ", expected MAX_WORK_ITEM_DIMENSIONS x size_t = " + std::to_string(want));
            return;
        }
    }
    if (param.shape == Shape::PropertyArray && size % sizeof(cl_device_partition_property) != 0) {
        problems.add(std::string(param.name) + " reports size " + std::to_string(size) + ", not a multiple of cl_device_partition_property");
    }
    if (param.shape == Shape::NameVersions && size % sizeof(cl_name_version) != 0) {
        problems.add(std::string(param.name) + " reports size " + std::to_string(size) + ", not a multiple of cl_name_version");
        return;
    }
    std::vector<char> blob(size + 8);
    const cl_int fetched = rig.api.GetDeviceInfo(rig.device, param.id, size, blob.data(), nullptr);
    if (fetched != CL_SUCCESS) {
        problems.add(std::string(param.name) + " value query returned " + ClTools::error_name(fetched));
        return;
    }
    if (param.shape == Shape::Text) {
        if (size == 0 || blob[size - 1] != '\0' || std::strlen(blob.data()) != size - 1) {
            problems.add(std::string(param.name) + " is not a NUL-terminated string of the reported size " + std::to_string(size));
        }
    }
    if (param.shape == Shape::Bool) {
        cl_bool value = 0;
        std::memcpy(&value, blob.data(), sizeof(value));
        if (value != CL_TRUE && value != CL_FALSE) {
            problems.add(std::string(param.name) + " is " + std::to_string(value) + ", neither CL_TRUE nor CL_FALSE");
        }
    }
    if (size > 1 && param.shape != Shape::Text) {
        const cl_int too_small = rig.api.GetDeviceInfo(rig.device, param.id, size - 1, blob.data(), nullptr);
        if (too_small != CL_INVALID_VALUE) {
            problems.add(std::string(param.name) + " with a buffer one byte too small returned " + ClTools::error_name(too_small) + ", expected CL_INVALID_VALUE");
        }
    }
}

void spec_minimums(ClRig& rig, Problems& problems) {
    cl_device_type type = 0;
    rig.info(CL_DEVICE_TYPE, type);
    if ((type & CL_DEVICE_TYPE_CUSTOM) != 0 || !rig.profile_full()) {
        return;
    }
    auto at_least = [&](const char* name, std::uint64_t value, std::uint64_t minimum) {
        if (value < minimum) {
            problems.add(std::string(name) + " is " + std::to_string(value) + ", the specification minimum is " + std::to_string(minimum));
        }
    };
    const std::uint64_t global = rig.global_memory();
    const std::uint64_t alloc_min = std::max<std::uint64_t>(std::min<std::uint64_t>(1ull << 30, global / 4), 32ull << 20);
    at_least("CL_DEVICE_MAX_MEM_ALLOC_SIZE", rig.max_allocation(), alloc_min);
    at_least("CL_DEVICE_MAX_COMPUTE_UNITS", rig.uint_info(CL_DEVICE_MAX_COMPUTE_UNITS), 1);
    at_least("CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS", rig.uint_info(CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS), 3);
    at_least("CL_DEVICE_MAX_WORK_GROUP_SIZE", rig.size_info(CL_DEVICE_MAX_WORK_GROUP_SIZE), 1);
    std::size_t sizes[3] = {};
    if (rig.api.GetDeviceInfo(rig.device, CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(sizes), sizes, nullptr) == CL_SUCCESS) {
        for (const std::size_t item : sizes) {
            at_least("CL_DEVICE_MAX_WORK_ITEM_SIZES[i]", item, 1);
        }
    }
    at_least("CL_DEVICE_LOCAL_MEM_SIZE", rig.ulong_info(CL_DEVICE_LOCAL_MEM_SIZE), 32 * 1024);
    at_least("CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE", rig.ulong_info(CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE), 64 * 1024);
    at_least("CL_DEVICE_MAX_CONSTANT_ARGS", rig.uint_info(CL_DEVICE_MAX_CONSTANT_ARGS), 8);
    at_least("CL_DEVICE_MAX_PARAMETER_SIZE", rig.size_info(CL_DEVICE_MAX_PARAMETER_SIZE), 1024);
    at_least("CL_DEVICE_MEM_BASE_ADDR_ALIGN", rig.uint_info(CL_DEVICE_MEM_BASE_ADDR_ALIGN), 1024);
    const cl_uint bits = rig.uint_info(CL_DEVICE_ADDRESS_BITS);
    if (bits != 32 && bits != 64) {
        problems.add("CL_DEVICE_ADDRESS_BITS is " + std::to_string(bits) + ", expected 32 or 64");
    }
    cl_device_fp_config single = 0;
    rig.info(CL_DEVICE_SINGLE_FP_CONFIG, single);
    if ((single & (CL_FP_ROUND_TO_NEAREST | CL_FP_INF_NAN)) != (CL_FP_ROUND_TO_NEAREST | CL_FP_INF_NAN)) {
        problems.add("CL_DEVICE_SINGLE_FP_CONFIG 0x" + Text::hex(single, 4) + " lacks CL_FP_ROUND_TO_NEAREST | CL_FP_INF_NAN");
    }
    cl_device_exec_capabilities exec = 0;
    rig.info(CL_DEVICE_EXECUTION_CAPABILITIES, exec);
    if ((exec & CL_EXEC_KERNEL) == 0) {
        problems.add("CL_DEVICE_EXECUTION_CAPABILITIES lacks CL_EXEC_KERNEL");
    }
    cl_command_queue_properties queue_properties = 0;
    rig.info(CL_DEVICE_QUEUE_PROPERTIES, queue_properties);
    if ((queue_properties & CL_QUEUE_PROFILING_ENABLE) == 0) {
        problems.add("CL_DEVICE_QUEUE_PROPERTIES lacks CL_QUEUE_PROFILING_ENABLE");
    }
    if (rig.uint_info(CL_DEVICE_COMPILER_AVAILABLE) == 0) {
        problems.add("a FULL_PROFILE device reports CL_DEVICE_COMPILER_AVAILABLE = CL_FALSE");
    }
    if (rig.version.at_least(1, 2)) {
        at_least("CL_DEVICE_PRINTF_BUFFER_SIZE", rig.size_info(CL_DEVICE_PRINTF_BUFFER_SIZE), 1 << 20);
        if (rig.uint_info(CL_DEVICE_COMPILER_AVAILABLE) != 0 && rig.uint_info(CL_DEVICE_LINKER_AVAILABLE) == 0) {
            problems.add("CL_DEVICE_LINKER_AVAILABLE is CL_FALSE while the compiler is available on a FULL_PROFILE device");
        }
        if (rig.uint_info(CL_DEVICE_REFERENCE_COUNT) != 1) {
            problems.add("CL_DEVICE_REFERENCE_COUNT of a root device is " + std::to_string(rig.uint_info(CL_DEVICE_REFERENCE_COUNT)) + ", expected 1");
        }
        cl_device_id parent = reinterpret_cast<cl_device_id>(1);
        rig.api.GetDeviceInfo(rig.device, CL_DEVICE_PARENT_DEVICE, sizeof(parent), &parent, nullptr);
        if (parent != nullptr) {
            problems.add("CL_DEVICE_PARENT_DEVICE of a root device is not NULL");
        }
    }
    if (rig.images()) {
        const bool two = rig.version.at_least(2, 0);
        at_least("CL_DEVICE_IMAGE2D_MAX_WIDTH", rig.size_info(CL_DEVICE_IMAGE2D_MAX_WIDTH), two ? 16384 : 8192);
        at_least("CL_DEVICE_IMAGE2D_MAX_HEIGHT", rig.size_info(CL_DEVICE_IMAGE2D_MAX_HEIGHT), two ? 16384 : 8192);
        at_least("CL_DEVICE_IMAGE3D_MAX_WIDTH", rig.size_info(CL_DEVICE_IMAGE3D_MAX_WIDTH), 2048);
        at_least("CL_DEVICE_IMAGE3D_MAX_HEIGHT", rig.size_info(CL_DEVICE_IMAGE3D_MAX_HEIGHT), 2048);
        at_least("CL_DEVICE_IMAGE3D_MAX_DEPTH", rig.size_info(CL_DEVICE_IMAGE3D_MAX_DEPTH), 2048);
        at_least("CL_DEVICE_MAX_READ_IMAGE_ARGS", rig.uint_info(CL_DEVICE_MAX_READ_IMAGE_ARGS), 128);
        at_least("CL_DEVICE_MAX_WRITE_IMAGE_ARGS", rig.uint_info(CL_DEVICE_MAX_WRITE_IMAGE_ARGS), two ? 64 : 8);
        at_least("CL_DEVICE_MAX_SAMPLERS", rig.uint_info(CL_DEVICE_MAX_SAMPLERS), 16);
        if (rig.version.at_least(1, 2)) {
            at_least("CL_DEVICE_IMAGE_MAX_BUFFER_SIZE", rig.size_info(CL_DEVICE_IMAGE_MAX_BUFFER_SIZE), 65536);
            at_least("CL_DEVICE_IMAGE_MAX_ARRAY_SIZE", rig.size_info(CL_DEVICE_IMAGE_MAX_ARRAY_SIZE), 2048);
        }
    }
}

struct Probe {
    const char* extension;
    const char* pragma_name;
    const char* body;
};

const Probe kUseProbes[] = {
    {"cl_khr_fp64", "cl_khr_fp64", "__kernel void lsw_probe(__global double* a) { a[0] = a[0] * 2.0; }\n"},
    {"cl_khr_fp16", "cl_khr_fp16", "__kernel void lsw_probe(__global half* a) { half v = a[0]; a[0] = v + v; }\n"},
    {"cl_khr_int64_base_atomics", "cl_khr_int64_base_atomics", "__kernel void lsw_probe(volatile __global long* a) { atom_add(a, (long)1); }\n"},
    {"cl_khr_int64_extended_atomics", "cl_khr_int64_extended_atomics", "__kernel void lsw_probe(volatile __global long* a) { atom_max(a, (long)1); }\n"},
};

struct ExtensionFunctions {
    const char* extension;
    std::vector<const char*> functions;
};

const std::vector<ExtensionFunctions>& extension_functions() {
    static const std::vector<ExtensionFunctions> table = {
        {"cl_khr_d3d11_sharing", {"clGetDeviceIDsFromD3D11KHR", "clCreateFromD3D11BufferKHR", "clCreateFromD3D11Texture2DKHR", "clCreateFromD3D11Texture3DKHR", "clEnqueueAcquireD3D11ObjectsKHR", "clEnqueueReleaseD3D11ObjectsKHR"}},
        {"cl_khr_d3d10_sharing", {"clGetDeviceIDsFromD3D10KHR", "clCreateFromD3D10BufferKHR", "clCreateFromD3D10Texture2DKHR", "clCreateFromD3D10Texture3DKHR", "clEnqueueAcquireD3D10ObjectsKHR", "clEnqueueReleaseD3D10ObjectsKHR"}},
        {"cl_khr_dx9_media_sharing", {"clGetDeviceIDsFromDX9MediaAdapterKHR", "clCreateFromDX9MediaSurfaceKHR", "clEnqueueAcquireDX9MediaSurfacesKHR", "clEnqueueReleaseDX9MediaSurfacesKHR"}},
        {"cl_khr_gl_sharing", {"clGetGLContextInfoKHR"}},
        {"cl_khr_gl_event", {"clCreateEventFromGLsyncKHR"}},
        {"cl_khr_il_program", {"clCreateProgramWithILKHR"}},
        {"cl_khr_create_command_queue", {"clCreateCommandQueueWithPropertiesKHR"}},
        {"cl_khr_subgroups", {"clGetKernelSubGroupInfoKHR"}},
        {"cl_khr_terminate_context", {"clTerminateContextKHR"}},
        {"cl_khr_egl_image", {"clCreateFromEGLImageKHR", "clEnqueueAcquireEGLObjectsKHR", "clEnqueueReleaseEGLObjectsKHR"}},
        {"cl_khr_egl_event", {"clCreateEventFromEGLSyncKHR"}},
        {"cl_khr_icd", {"clIcdGetPlatformIDsKHR"}},
        {"cl_khr_external_memory", {"clEnqueueAcquireExternalMemObjectsKHR", "clEnqueueReleaseExternalMemObjectsKHR"}},
        {"cl_khr_semaphore", {"clCreateSemaphoreWithPropertiesKHR", "clEnqueueWaitSemaphoresKHR", "clEnqueueSignalSemaphoresKHR", "clGetSemaphoreInfoKHR", "clReleaseSemaphoreKHR", "clRetainSemaphoreKHR"}},
        {"cl_khr_external_semaphore", {"clGetSemaphoreHandleForTypeKHR"}},
        {"cl_khr_command_buffer", {"clCreateCommandBufferKHR", "clRetainCommandBufferKHR", "clReleaseCommandBufferKHR", "clFinalizeCommandBufferKHR", "clEnqueueCommandBufferKHR", "clCommandBarrierWithWaitListKHR", "clCommandCopyBufferKHR", "clCommandNDRangeKernelKHR", "clGetCommandBufferInfoKHR"}},
        {"cl_khr_suggested_local_work_size", {"clGetKernelSuggestedLocalWorkSizeKHR"}},
        {"cl_ext_device_fission", {"clCreateSubDevicesEXT", "clRetainDeviceEXT", "clReleaseDeviceEXT"}},
    };
    return table;
}

std::string probe_source(const Probe& probe) {
    return std::string("#pragma OPENCL EXTENSION ") + probe.pragma_name + " : enable\n" + probe.body;
}

}  // namespace

Outcome DeviceChecks::query_all_params(Context& context) {
    return Kit::each_device(context, [&](ClRig& rig, Problems& problems) {
        std::size_t queried = 0;
        for (const Param& param : kParams) {
            if (!rig.version.at_least(param.major, param.minor)) {
                continue;
            }
            ++queried;
            query_param(rig, param, problems);
        }
        if (rig.has_extension("cl_khr_fp16") && rig.version.at_least(1, 2)) {
            cl_device_fp_config half = 0;
            cl_int code = CL_SUCCESS;
            if (!rig.info(CL_DEVICE_HALF_FP_CONFIG, half, &code)) {
                problems.add("CL_DEVICE_HALF_FP_CONFIG returned " + ClTools::error_name(code) + " although cl_khr_fp16 is advertised");
            }
        }
        if (rig.has_extension("cl_khr_device_uuid")) {
            cl_uchar uuid[CL_UUID_SIZE_KHR] = {};
            cl_uchar luid[CL_LUID_SIZE_KHR] = {};
            cl_bool valid = CL_FALSE;
            cl_uint mask = 0;
            Kit::expect(problems, "CL_DEVICE_UUID_KHR", rig.api.GetDeviceInfo(rig.device, CL_DEVICE_UUID_KHR, sizeof(uuid), uuid, nullptr), {CL_SUCCESS});
            Kit::expect(problems, "CL_DRIVER_UUID_KHR", rig.api.GetDeviceInfo(rig.device, CL_DRIVER_UUID_KHR, sizeof(uuid), uuid, nullptr), {CL_SUCCESS});
            Kit::expect(problems, "CL_DEVICE_LUID_VALID_KHR", rig.api.GetDeviceInfo(rig.device, CL_DEVICE_LUID_VALID_KHR, sizeof(valid), &valid, nullptr), {CL_SUCCESS});
            Kit::expect(problems, "CL_DEVICE_LUID_KHR", rig.api.GetDeviceInfo(rig.device, CL_DEVICE_LUID_KHR, sizeof(luid), luid, nullptr), {CL_SUCCESS});
            Kit::expect(problems, "CL_DEVICE_NODE_MASK_KHR", rig.api.GetDeviceInfo(rig.device, CL_DEVICE_NODE_MASK_KHR, sizeof(mask), &mask, nullptr), {CL_SUCCESS});
        }
        Kit::expect(problems, "clGetDeviceInfo(invalid parameter)", rig.api.GetDeviceInfo(rig.device, 0x7777, 0, nullptr, nullptr), {CL_INVALID_VALUE});
        spec_minimums(rig, problems);
        context.measure("cl.params_queried", queried);
    });
}

Outcome DeviceChecks::extensions_match_entry_points(Context& context) {
    return Kit::each_device(context, [&](ClRig& rig, Problems& problems) {
        const std::vector<std::string> words = ClTools::split(rig.extensions);
        std::set<std::string> unique(words.begin(), words.end());
        if (unique.size() != words.size()) {
            problems.add("CL_DEVICE_EXTENSIONS lists an extension more than once");
        }
        for (const std::string& word : words) {
            if (word.compare(0, 3, "cl_") != 0 && word.compare(0, 5, "cles_") != 0) {
                problems.add("extension name '" + word + "' does not start with cl_ or cles_");
            }
        }
        std::size_t platform_size = 0;
        std::string platform_extensions;
        if (rig.api.GetPlatformInfo(rig.platform, CL_PLATFORM_EXTENSIONS, 0, nullptr, &platform_size) == CL_SUCCESS && platform_size != 0) {
            std::vector<char> blob(platform_size);
            rig.api.GetPlatformInfo(rig.platform, CL_PLATFORM_EXTENSIONS, platform_size, blob.data(), nullptr);
            platform_extensions = ClTools::text_of(blob);
        }
        for (const ExtensionFunctions& entry : extension_functions()) {
            const bool advertised = rig.has_extension(entry.extension) || ClTools::has_word(platform_extensions, entry.extension);
            if (!advertised) {
                continue;
            }
            for (const char* function : entry.functions) {
                if (rig.api.extension(rig.platform, function) == nullptr) {
                    problems.add(std::string(entry.extension) + " is advertised but clGetExtensionFunctionAddressForPlatform(" + function + ") returns NULL");
                }
            }
        }
        if (rig.version.at_least(3, 0)) {
            std::size_t size = 0;
            if (rig.api.GetDeviceInfo(rig.device, CL_DEVICE_EXTENSIONS_WITH_VERSION, 0, nullptr, &size) == CL_SUCCESS && size % sizeof(cl_name_version) == 0) {
                std::vector<cl_name_version> versioned(size / sizeof(cl_name_version));
                if (size != 0 && rig.api.GetDeviceInfo(rig.device, CL_DEVICE_EXTENSIONS_WITH_VERSION, size, versioned.data(), nullptr) == CL_SUCCESS) {
                    std::set<std::string> names;
                    for (const cl_name_version& item : versioned) {
                        names.insert(std::string(item.name, strnlen(item.name, CL_NAME_VERSION_MAX_NAME_SIZE)));
                    }
                    if (names != unique) {
                        problems.add("CL_DEVICE_EXTENSIONS_WITH_VERSION names " + std::to_string(names.size()) + " extension(s), CL_DEVICE_EXTENSIONS names " + std::to_string(unique.size()));
                    }
                }
            }
        }
        for (const Probe& probe : kUseProbes) {
            const bool advertised = rig.has_extension(probe.extension);
            ClProgram program;
            std::string error;
            const bool built = program.build(rig, probe_source(probe), "", error);
            if (advertised && !built) {
                problems.add(std::string(probe.extension) + " is advertised but a kernel using it does not build: " + error);
            } else if (!advertised && built) {
                problems.add(std::string(probe.extension) + " is not advertised but a kernel using it builds");
            }
        }
        context.measure("cl.extension_count", words.size());
    });
}

Outcome DeviceChecks::limits_honoured(Context& context) {
    return Kit::each_device(context, [&](ClRig& rig, Problems& problems) {
        const std::string options = rig.version.at_least(1, 2) ? "-cl-std=CL1.2" : "";
        const char* source =
            "__kernel void lsw_ids(__global uint* out) {\n"
            "    size_t g = get_global_id(0) + get_global_id(1) * get_global_size(0) +\n"
            "               get_global_id(2) * get_global_size(0) * get_global_size(1);\n"
            "    uint l = (uint)(get_local_id(0) + get_local_id(1) * get_local_size(0) +\n"
            "                    get_local_id(2) * get_local_size(0) * get_local_size(1));\n"
            "    uint grp = (uint)(get_group_id(0) + 31 * get_group_id(1) + 997 * get_group_id(2));\n"
            "    out[g] = (uint)g * 5u + l * 3u + grp;\n"
            "}\n"
            "__kernel void lsw_local(__global uint* out, __local uint* tmp, uint n) {\n"
            "    uint lid = (uint)get_local_id(0);\n"
            "    uint size = (uint)get_local_size(0);\n"
            "    for (uint i = lid; i < n; i += size) { tmp[i] = i * 2654435761u; }\n"
            "    barrier(CLK_LOCAL_MEM_FENCE);\n"
            "    for (uint i = lid; i < n; i += size) { out[i] = tmp[n - 1 - i]; }\n"
            "}\n"
            "__kernel void lsw_const(__constant uint* c, __global uint* out, uint n) {\n"
            "    size_t i = get_global_id(0);\n"
            "    if (i < n) { out[i] = c[i] ^ 0x5a5a5a5au; }\n"
            "}\n";
        ClProgram program;
        if (!Kit::compile(rig, program, source, options, problems, "limits kernels")) {
            return;
        }
        ClKernel ids = Kit::kernel(program, "lsw_ids", problems);
        ClKernel local = Kit::kernel(program, "lsw_local", problems);
        ClKernel constant = Kit::kernel(program, "lsw_const", problems);
        if (!ids || !local || !constant) {
            return;
        }
        const std::size_t device_max = rig.size_info(CL_DEVICE_MAX_WORK_GROUP_SIZE);
        const std::size_t kernel_max = ids.work_group_size();
        const cl_uint dims = rig.uint_info(CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS);
        std::size_t sizes[3] = {1, 1, 1};
        rig.api.GetDeviceInfo(rig.device, CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(sizes), sizes, nullptr);
        if (dims < 3) {
            problems.add("CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS is " + std::to_string(dims) + ", the specification minimum is 3");
            return;
        }
        if (kernel_max == 0 || kernel_max > device_max) {
            problems.add("CL_KERNEL_WORK_GROUP_SIZE " + std::to_string(kernel_max) + " is outside 1.." + std::to_string(device_max));
            return;
        }
        auto expected_ids = [](std::size_t gx, std::size_t gy, std::size_t gz, std::size_t lx, std::size_t ly, std::size_t lz) {
            std::vector<std::uint32_t> out(gx * gy * gz);
            for (std::size_t z = 0; z < gz; ++z) {
                for (std::size_t y = 0; y < gy; ++y) {
                    for (std::size_t x = 0; x < gx; ++x) {
                        const std::size_t g = x + y * gx + z * gx * gy;
                        const std::size_t l = (x % lx) + (y % ly) * lx + (z % lz) * lx * ly;
                        const std::size_t grp = x / lx + 31 * (y / ly) + 997 * (z / lz);
                        out[g] = static_cast<std::uint32_t>(g * 5 + l * 3 + grp);
                    }
                }
            }
            return out;
        };
        const std::size_t widest = std::min(kernel_max, sizes[0]);
        {
            const std::size_t global = widest * 3;
            ClOwned<cl_mem> out = Kit::buffer(rig, CL_MEM_WRITE_ONLY, global * 4, problems, "max work-group buffer");
            if (out && ids.set(out) == CL_SUCCESS) {
                const cl_int code = ids.run(global, widest);
                if (code != CL_SUCCESS) {
                    problems.add("a launch with the kernel's maximum work-group size " + std::to_string(widest) + " returned " + ClTools::error_name(code));
                } else {
                    std::vector<std::uint32_t> actual;
                    if (Kit::read_words(rig, out.get(), global, actual, problems, "max work-group")) {
                        Kit::same_words(context, problems, "max work-group size results", expected_ids(global, 1, 1, widest, 1, 1), actual);
                    }
                }
            }
        }
        {
            std::size_t lx = std::min<std::size_t>(sizes[0], 4);
            std::size_t ly = std::min<std::size_t>(sizes[1], 3);
            std::size_t lz = std::min<std::size_t>(sizes[2], 2);
            while (lx * ly * lz > kernel_max) {
                if (lz > 1) {
                    --lz;
                } else if (ly > 1) {
                    --ly;
                } else {
                    --lx;
                }
            }
            const std::size_t gx = lx * 3;
            const std::size_t gy = ly * 2;
            const std::size_t gz = lz * 2;
            ClOwned<cl_mem> out = Kit::buffer(rig, CL_MEM_WRITE_ONLY, gx * gy * gz * 4, problems, "3D range buffer");
            if (out && ids.set(out) == CL_SUCCESS) {
                const std::size_t global[3] = {gx, gy, gz};
                const std::size_t group[3] = {lx, ly, lz};
                const cl_int code = ids.enqueue(rig.queue, 3, nullptr, global, group);
                if (code != CL_SUCCESS) {
                    problems.add("a 3D launch " + std::to_string(gx) + "x" + std::to_string(gy) + "x" + std::to_string(gz) + " local " + std::to_string(lx) + "x" + std::to_string(ly) + "x" + std::to_string(lz) + " returned " + ClTools::error_name(code));
                } else {
                    std::vector<std::uint32_t> actual;
                    if (Kit::read_words(rig, out.get(), gx * gy * gz, actual, problems, "3D range")) {
                        Kit::same_words(context, problems, "3D range results", expected_ids(gx, gy, gz, lx, ly, lz), actual);
                    }
                }
            }
            const std::size_t global4[3] = {1, 1, 1};
            Kit::expect(problems, "clEnqueueNDRangeKernel(work_dim 4)", ids.enqueue(rig.queue, 4, nullptr, global4, nullptr), {CL_INVALID_WORK_DIMENSION});
            Kit::expect(problems, "clEnqueueNDRangeKernel(work_dim 0)", ids.enqueue(rig.queue, 0, nullptr, global4, nullptr), {CL_INVALID_WORK_DIMENSION});
            const std::size_t over = kernel_max + 1;
            const std::size_t global_over = over * 2;
            Kit::expect(problems, "a work-group of CL_KERNEL_WORK_GROUP_SIZE + 1 items", ids.run(global_over, over), {CL_INVALID_WORK_GROUP_SIZE, CL_INVALID_WORK_ITEM_SIZE});
            if (sizes[1] < (std::size_t(1) << 20)) {
                const std::size_t tall[3] = {1, sizes[1] + 1, 1};
                const std::size_t tall_global[3] = {1, (sizes[1] + 1) * 2, 1};
                Kit::expect(problems, "a work-group taller than CL_DEVICE_MAX_WORK_ITEM_SIZES[1]", ids.enqueue(rig.queue, 2, nullptr, tall_global, tall), {CL_INVALID_WORK_ITEM_SIZE, CL_INVALID_WORK_GROUP_SIZE});
            }
            if (options == "-cl-std=CL1.2" && kernel_max >= 2) {
                const std::size_t uneven_global = kernel_max * 2 + 1;
                Kit::expect(problems, "a global size not divisible by the local size (OpenCL C 1.2)", ids.run(uneven_global, kernel_max), {CL_INVALID_WORK_GROUP_SIZE});
            }
        }
        {
            const std::uint64_t local_total = rig.ulong_info(CL_DEVICE_LOCAL_MEM_SIZE);
            cl_ulong used = 0;
            rig.api.GetKernelWorkGroupInfo(local.get(), rig.device, CL_KERNEL_LOCAL_MEM_SIZE, sizeof(used), &used, nullptr);
            const std::uint64_t bytes = (local_total - std::min<std::uint64_t>(local_total, used)) / 4 * 4;
            const std::uint32_t n = static_cast<std::uint32_t>(bytes / 4);
            const std::size_t group = std::min<std::size_t>(kernel_max, 64);
            ClOwned<cl_mem> out = Kit::buffer(rig, CL_MEM_WRITE_ONLY, static_cast<std::size_t>(bytes), problems, "local memory result buffer");
            if (out) {
                cl_int code = local.set(out, ClLocal{static_cast<std::size_t>(bytes)}, n);
                if (code == CL_SUCCESS) {
                    code = local.run(group, group);
                }
                if (code != CL_SUCCESS) {
                    problems.add("a kernel using all " + ClTools::bytes_text(bytes) + " of CL_DEVICE_LOCAL_MEM_SIZE returned " + ClTools::error_name(code));
                } else {
                    std::vector<std::uint32_t> expected(n);
                    for (std::uint32_t i = 0; i < n; ++i) {
                        expected[i] = (n - 1 - i) * 2654435761u;
                    }
                    std::vector<std::uint32_t> actual;
                    if (Kit::read_words(rig, out.get(), n, actual, problems, "local memory")) {
                        Kit::same_words(context, problems, "local memory results", expected, actual);
                    }
                }
                const std::size_t excess = static_cast<std::size_t>(local_total + (4u << 20));
                cl_int set_code = local.set(out, ClLocal{excess}, n);
                if (set_code == CL_SUCCESS) {
                    set_code = local.run(group, group);
                    Kit::expect(problems, "a kernel with " + ClTools::bytes_text(excess) + " of local memory (CL_DEVICE_LOCAL_MEM_SIZE " + ClTools::bytes_text(local_total) + ")", set_code, {CL_OUT_OF_RESOURCES, CL_INVALID_WORK_GROUP_SIZE});
                } else {
                    Kit::expect(problems, "clSetKernelArg with " + ClTools::bytes_text(excess) + " of local memory", set_code, {CL_MAX_SIZE_RESTRICTION_EXCEEDED, CL_INVALID_ARG_SIZE});
                }
            }
        }
        {
            const std::uint64_t budget = std::min<std::uint64_t>({rig.ulong_info(CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE), rig.max_allocation(), rig.global_memory() / 8, 256ull << 20});
            const std::size_t bytes = static_cast<std::size_t>(budget / 4 * 4);
            const std::uint32_t n = static_cast<std::uint32_t>(bytes / 4);
            cl_int code = CL_SUCCESS;
            std::vector<std::uint32_t> data = Kit::words(77, n);
            ClOwned<cl_mem> in = Kit::buffer(rig, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, bytes, data.data(), code);
            ClOwned<cl_mem> out = Kit::buffer(rig, CL_MEM_WRITE_ONLY, bytes, nullptr, code);
            if (!in || !out) {
                problems.add("creating the " + ClTools::bytes_text(bytes) + " constant buffers failed: " + ClTools::error_name(code));
            } else {
                code = constant.set(in, out, n);
                std::size_t group = 0;
                if (code == CL_SUCCESS) {
                    group = std::min<std::size_t>(Kit::group_limit(constant, problems, "constant buffer kernel"), 64);
                    if (group != 0) {
                        code = constant.run(((n + group - 1) / group) * group, group);
                    }
                }
                if (code != CL_SUCCESS) {
                    problems.add("a kernel reading a " + ClTools::bytes_text(bytes) + " __constant buffer returned " + ClTools::error_name(code));
                } else if (group != 0) {
                    std::vector<std::uint32_t> expected(n);
                    for (std::uint32_t i = 0; i < n; ++i) {
                        expected[i] = data[i] ^ 0x5a5a5a5au;
                    }
                    std::vector<std::uint32_t> actual;
                    if (Kit::read_words(rig, out.get(), n, actual, problems, "constant buffer")) {
                        Kit::same_words(context, problems, "constant buffer results", expected, actual);
                    }
                }
            }
            context.measure("cl.constant_bytes_tested", bytes);
        }
    });
}

Outcome DeviceChecks::both_adapters(Context& context) {
    ClApi api;
    ClDeviceList list;
    std::string error;
    if (!ClRig::list_devices(api, list, error)) {
        return Kit::open_failed(error);
    }
    Problems problems;
    if (list.refs.size() < list.adapters) {
        problems.add(std::to_string(list.adapters) + " accel-virt adapter(s) but only " + std::to_string(list.refs.size()) + " OpenCL device(s) matched them");
    }
    struct Slot {
        Kit::Pair add;
        Kit::Pair reduce;
        Problems problems;
        bool opened = false;
    };
    std::vector<Slot> slots(list.refs.size());
    std::atomic<std::size_t> ready{0};
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < list.refs.size(); ++i) {
        threads.emplace_back([&, i]() {
            Slot& slot = slots[i];
            ClRig rig;
            std::string open_error;
            if (!rig.open_ref(list.refs[i], ClRigOptions{}, open_error)) {
                slot.problems.add(open_error);
                ++ready;
                return;
            }
            slot.opened = true;
            ++ready;
            while (ready.load() < slots.size()) {
                std::this_thread::yield();
            }
            for (int round = 0; round < 3; ++round) {
                Kit::Pair add;
                if (!Kit::vector_add(rig, 1 << 20, 100 + static_cast<std::uint64_t>(round), add, slot.problems, "vector add")) {
                    break;
                }
                if (add.expected != add.actual) {
                    slot.add = std::move(add);
                    break;
                }
                Kit::Pair reduce;
                if (!Kit::reduction(rig, 1 << 20, 64, 200 + static_cast<std::uint64_t>(round), reduce, slot.problems, "reduction")) {
                    break;
                }
                if (reduce.expected != reduce.actual) {
                    slot.reduce = std::move(reduce);
                    break;
                }
            }
            rig.drain();
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const std::string label = list.refs[i].label();
        if (!slots[i].problems.empty()) {
            problems.add(label + ": " + slots[i].problems.text());
            continue;
        }
        Problems local;
        if (!slots[i].add.expected.empty()) {
            Kit::same_words(context, local, "vector add", slots[i].add.expected, slots[i].add.actual);
        }
        if (!slots[i].reduce.expected.empty()) {
            Kit::same_words(context, local, "reduction", slots[i].reduce.expected, slots[i].reduce.actual);
        }
        if (!local.empty()) {
            problems.add(label + ": " + local.text());
        }
    }
    context.measure("cl.devices_run_concurrently", slots.size());
    return problems.outcome();
}

Outcome DeviceChecks::create_sub_devices_error(Context& context) {
    return Kit::each_device(context, [&](ClRig& rig, Problems& problems) {
        std::size_t bytes = 0;
        std::vector<cl_device_partition_property> supported;
        if (rig.version.at_least(1, 2)) {
            rig.api.GetDeviceInfo(rig.device, CL_DEVICE_PARTITION_PROPERTIES, 0, nullptr, &bytes);
            supported.resize(bytes / sizeof(cl_device_partition_property));
            if (bytes != 0) {
                rig.api.GetDeviceInfo(rig.device, CL_DEVICE_PARTITION_PROPERTIES, bytes, supported.data(), nullptr);
            }
        }
        auto offers = [&](cl_device_partition_property kind) {
            return std::find(supported.begin(), supported.end(), kind) != supported.end();
        };
        if (!rig.version.at_least(1, 2) || rig.api.CreateSubDevices == nullptr) {
            context.measure("cl.partition_properties", "none (OpenCL < 1.2)");
            return;
        }
        const cl_uint max_sub = rig.uint_info(CL_DEVICE_PARTITION_MAX_SUB_DEVICES);
        context.measure("cl.partition_equally", offers(CL_DEVICE_PARTITION_EQUALLY));
        context.measure("cl.partition_by_counts", offers(CL_DEVICE_PARTITION_BY_COUNTS));
        context.measure("cl.partition_by_affinity", offers(CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN));
        const cl_device_partition_property equally[] = {CL_DEVICE_PARTITION_EQUALLY, 1, 0};
        const cl_device_partition_property counts[] = {CL_DEVICE_PARTITION_BY_COUNTS, 1, CL_DEVICE_PARTITION_BY_COUNTS_LIST_END, 0};
        const cl_device_partition_property affinity[] = {CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN, CL_DEVICE_AFFINITY_DOMAIN_NEXT_PARTITIONABLE, 0};
        cl_uint made = 0;
        Kit::expect(problems, "clCreateSubDevices(NULL device)", rig.api.CreateSubDevices(nullptr, equally, 0, nullptr, &made), {CL_INVALID_DEVICE});
        Kit::expect(problems, "clCreateSubDevices(NULL properties)", rig.api.CreateSubDevices(rig.device, nullptr, 0, nullptr, &made), {CL_INVALID_VALUE});
        cl_device_id sink = nullptr;
        const struct {
            const char* name;
            const cl_device_partition_property* properties;
            cl_device_partition_property kind;
        } kinds[] = {{"CL_DEVICE_PARTITION_EQUALLY", equally, CL_DEVICE_PARTITION_EQUALLY},
                     {"CL_DEVICE_PARTITION_BY_COUNTS", counts, CL_DEVICE_PARTITION_BY_COUNTS},
                     {"CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN", affinity, CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN}};
        for (const auto& kind : kinds) {
            const std::string what = std::string("clCreateSubDevices(") + kind.name + ")";
            made = 0;
            if (!offers(kind.kind)) {
                const cl_int code = rig.api.CreateSubDevices(rig.device, kind.properties, 0, nullptr, &made);
                Kit::expect(problems, what + " on a device that does not list it", code, {CL_INVALID_VALUE, CL_DEVICE_PARTITION_FAILED});
                continue;
            }
            cl_int code = rig.api.CreateSubDevices(rig.device, kind.properties, 0, nullptr, &made);
            if (code != CL_SUCCESS || made == 0) {
                problems.add(what + " count query returned " + ClTools::error_name(code) + " with " + std::to_string(made) + " sub-device(s) although the device lists it");
                continue;
            }
            if (made > max_sub) {
                problems.add(what + " yields " + std::to_string(made) + " sub-devices, CL_DEVICE_PARTITION_MAX_SUB_DEVICES is " + std::to_string(max_sub));
            }
            Kit::expect(problems, what + " with a zero-entry output array", rig.api.CreateSubDevices(rig.device, kind.properties, 0, &sink, nullptr), {CL_INVALID_VALUE});
            std::vector<cl_device_id> subs(made);
            code = rig.api.CreateSubDevices(rig.device, kind.properties, made, subs.data(), nullptr);
            if (code != CL_SUCCESS) {
                problems.add(what + " returned " + ClTools::error_name(code) + " on the fill call");
                continue;
            }
            for (std::size_t s = 0; s < subs.size(); ++s) {
                cl_device_id parent = nullptr;
                rig.api.GetDeviceInfo(subs[s], CL_DEVICE_PARENT_DEVICE, sizeof(parent), &parent, nullptr);
                if (parent != rig.device) {
                    problems.add(what + ": sub-device " + std::to_string(s) + " reports a different parent");
                }
                cl_device_partition_property type[4] = {};
                rig.api.GetDeviceInfo(subs[s], CL_DEVICE_PARTITION_TYPE, sizeof(type), type, nullptr);
                if (type[0] != kind.kind) {
                    problems.add(what + ": sub-device " + std::to_string(s) + " reports partition type 0x" + Text::hex(static_cast<std::uint64_t>(type[0]), 4));
                }
            }
            if (!subs.empty()) {
                ClRig sub_rig;
                ClDeviceRef sub_ref = rig.ref;
                sub_ref.device = subs[0];
                std::string error;
                if (!sub_rig.open_ref(sub_ref, ClRigOptions{}, error)) {
                    problems.add(what + ": using sub-device 0: " + error);
                } else {
                    Kit::Pair pair;
                    if (Kit::vector_add(sub_rig, 4096, 31, pair, problems, what + " sub-device vector add")) {
                        Kit::same_words(context, problems, what + " sub-device vector add", pair.expected, pair.actual);
                    }
                }
            }
            for (cl_device_id sub : subs) {
                rig.api.ReleaseDevice(sub);
            }
        }
    });
}

}  // namespace lsw::suites::sanity::cl
