#include "support/opencl_backend.hpp"

#include "support/av_adapters.hpp"

#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>
#include <CL/cl_ext.h>
#include <d3d11.h>
#include <CL/cl_d3d11.h>

#include <cstring>

namespace lsw::suites {

struct OpenClRuntime {
    DynLibrary library;
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;
    cl_context context = nullptr;
    bool luid_matched = false;
    std::string extensions;
    cl_int(__stdcall* get_platform_ids)(cl_uint, cl_platform_id*, cl_uint*) = nullptr;
    cl_int(__stdcall* get_device_ids)(cl_platform_id, cl_device_type, cl_uint, cl_device_id*, cl_uint*) = nullptr;
    cl_int(__stdcall* get_device_info)(cl_device_id, cl_device_info, size_t, void*, size_t*) = nullptr;
    cl_context(__stdcall* create_context)(const cl_context_properties*, cl_uint, const cl_device_id*, void*, void*, cl_int*) = nullptr;
    cl_int(__stdcall* release_context)(cl_context) = nullptr;
    cl_command_queue(__stdcall* create_queue)(cl_context, cl_device_id, cl_command_queue_properties, cl_int*) = nullptr;
    cl_int(__stdcall* release_queue)(cl_command_queue) = nullptr;
    cl_mem(__stdcall* create_buffer)(cl_context, cl_mem_flags, size_t, void*, cl_int*) = nullptr;
    cl_int(__stdcall* release_mem)(cl_mem) = nullptr;
    cl_int(__stdcall* write_buffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, const void*, cl_uint, const cl_event*, cl_event*) = nullptr;
    cl_int(__stdcall* read_buffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, void*, cl_uint, const cl_event*, cl_event*) = nullptr;
    cl_int(__stdcall* copy_buffer)(cl_command_queue, cl_mem, cl_mem, size_t, size_t, size_t, cl_uint, const cl_event*, cl_event*) = nullptr;
    void*(__stdcall* map_buffer)(cl_command_queue, cl_mem, cl_bool, cl_map_flags, size_t, size_t, cl_uint, const cl_event*, cl_event*, cl_int*) = nullptr;
    cl_int(__stdcall* unmap)(cl_command_queue, cl_mem, void*, cl_uint, const cl_event*, cl_event*) = nullptr;
    cl_int(__stdcall* finish)(cl_command_queue) = nullptr;
    cl_program(__stdcall* create_program)(cl_context, cl_uint, const char**, const size_t*, cl_int*) = nullptr;
    cl_int(__stdcall* build_program)(cl_program, cl_uint, const cl_device_id*, const char*, void*, void*) = nullptr;
    cl_kernel(__stdcall* create_kernel)(cl_program, const char*, cl_int*) = nullptr;
    cl_int(__stdcall* set_arg)(cl_kernel, cl_uint, size_t, const void*) = nullptr;
    cl_int(__stdcall* nd_range)(cl_command_queue, cl_kernel, cl_uint, const size_t*, const size_t*, const size_t*, cl_uint, const cl_event*, cl_event*) = nullptr;
    cl_int(__stdcall* release_kernel)(cl_kernel) = nullptr;
    cl_int(__stdcall* release_program)(cl_program) = nullptr;
    void*(__stdcall* ext_address)(cl_platform_id, const char*) = nullptr;
    cl_int(__stdcall* read_image)(cl_command_queue, cl_mem, cl_bool, const size_t*, const size_t*, size_t, size_t, void*, cl_uint, const cl_event*, cl_event*) = nullptr;

    ~OpenClRuntime() {
        if (context != nullptr && release_context != nullptr) {
            release_context(context);
        }
    }
};

namespace {

class BufferCl : public GpuBuffer {
public:
    ~BufferCl() override {
        if (runtime != nullptr && mem != nullptr) {
            runtime->release_mem(mem);
        }
    }

    OpenClRuntime* runtime = nullptr;
    cl_mem mem = nullptr;
    void* mapped = nullptr;
};

bool is_oom(cl_int code) {
    return code == CL_MEM_OBJECT_ALLOCATION_FAILURE || code == CL_OUT_OF_RESOURCES || code == CL_OUT_OF_HOST_MEMORY;
}

}  // namespace

OpenClBackend::OpenClBackend() = default;

OpenClBackend::~OpenClBackend() {
    if (queue_ != nullptr && runtime_ != nullptr) {
        runtime_->release_queue(static_cast<cl_command_queue>(queue_));
    }
}

bool OpenClBackend::init(const LUID& luid, std::string& error) {
    runtime_ = std::make_shared<OpenClRuntime>();
    OpenClRuntime& rt = *runtime_;
    if (!rt.library.load("OpenCL.dll")) {
        error = "OpenCL.dll cannot be loaded";
        return false;
    }
#define LSW_CL(field, name) rt.field = rt.library.symbol<decltype(rt.field)>(name)
    LSW_CL(get_platform_ids, "clGetPlatformIDs");
    LSW_CL(get_device_ids, "clGetDeviceIDs");
    LSW_CL(get_device_info, "clGetDeviceInfo");
    LSW_CL(create_context, "clCreateContext");
    LSW_CL(release_context, "clReleaseContext");
    LSW_CL(create_queue, "clCreateCommandQueue");
    LSW_CL(release_queue, "clReleaseCommandQueue");
    LSW_CL(create_buffer, "clCreateBuffer");
    LSW_CL(release_mem, "clReleaseMemObject");
    LSW_CL(write_buffer, "clEnqueueWriteBuffer");
    LSW_CL(read_buffer, "clEnqueueReadBuffer");
    LSW_CL(copy_buffer, "clEnqueueCopyBuffer");
    LSW_CL(map_buffer, "clEnqueueMapBuffer");
    LSW_CL(unmap, "clEnqueueUnmapMemObject");
    LSW_CL(finish, "clFinish");
    LSW_CL(create_program, "clCreateProgramWithSource");
    LSW_CL(build_program, "clBuildProgram");
    LSW_CL(create_kernel, "clCreateKernel");
    LSW_CL(set_arg, "clSetKernelArg");
    LSW_CL(nd_range, "clEnqueueNDRangeKernel");
    LSW_CL(release_kernel, "clReleaseKernel");
    LSW_CL(release_program, "clReleaseProgram");
    LSW_CL(ext_address, "clGetExtensionFunctionAddressForPlatform");
    LSW_CL(read_image, "clEnqueueReadImage");
    if (rt.get_platform_ids == nullptr || rt.finish == nullptr || rt.unmap == nullptr || rt.create_queue == nullptr) {
        error = "OpenCL.dll lacks entry points";
        return false;
    }
    cl_uint platform_count = 0;
    rt.get_platform_ids(0, nullptr, &platform_count);
    std::vector<cl_platform_id> platforms(platform_count);
    if (platform_count != 0) {
        rt.get_platform_ids(platform_count, platforms.data(), nullptr);
    }
    cl_device_id first_device = nullptr;
    cl_platform_id first_platform = nullptr;
    for (cl_platform_id platform : platforms) {
        cl_uint device_count = 0;
        rt.get_device_ids(platform, CL_DEVICE_TYPE_ALL, 0, nullptr, &device_count);
        std::vector<cl_device_id> devices(device_count);
        if (device_count == 0 || rt.get_device_ids(platform, CL_DEVICE_TYPE_ALL, device_count, devices.data(), nullptr) != CL_SUCCESS) {
            continue;
        }
        for (cl_device_id device : devices) {
            if (first_device == nullptr) {
                first_device = device;
                first_platform = platform;
            }
            cl_bool valid = CL_FALSE;
            cl_uchar bytes[CL_LUID_SIZE_KHR] = {};
            if (rt.get_device_info(device, CL_DEVICE_LUID_VALID_KHR, sizeof(valid), &valid, nullptr) == CL_SUCCESS &&
                valid != CL_FALSE && rt.get_device_info(device, CL_DEVICE_LUID_KHR, sizeof(bytes), bytes, nullptr) == CL_SUCCESS &&
                std::memcmp(bytes, &luid, sizeof(luid)) == 0) {
                rt.device = device;
                rt.platform = platform;
                rt.luid_matched = true;
            }
        }
    }
    if (rt.device == nullptr) {
        rt.device = first_device;
        rt.platform = first_platform;
    }
    if (rt.device == nullptr) {
        error = "OpenCL exposes no device";
        return false;
    }
    cl_context_properties properties[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(rt.platform), 0};
    cl_int code = CL_SUCCESS;
    rt.context = rt.create_context(properties, 1, &rt.device, nullptr, nullptr, &code);
    if (rt.context == nullptr) {
        error = "clCreateContext failed: " + std::to_string(code);
        return false;
    }
    queue_ = rt.create_queue(rt.context, rt.device, 0, &code);
    if (queue_ == nullptr) {
        error = "clCreateCommandQueue failed: " + std::to_string(code);
        return false;
    }
    return true;
}

std::unique_ptr<GpuBackend> OpenClBackend::new_context() {
    auto other = std::make_unique<OpenClBackend>();
    other->runtime_ = runtime_;
    cl_int code = CL_SUCCESS;
    other->queue_ = runtime_->create_queue(runtime_->context, runtime_->device, 0, &code);
    if (other->queue_ == nullptr) {
        return nullptr;
    }
    return other;
}

std::unique_ptr<GpuBuffer> OpenClBackend::create(std::uint64_t size, BufferKind kind, AllocStatus& status,
                                                 std::string& detail) {
    cl_int code = CL_SUCCESS;
    const cl_mem_flags flags = kind == BufferKind::Device ? CL_MEM_READ_WRITE : CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR;
    cl_mem mem = runtime_->create_buffer(runtime_->context, flags, static_cast<size_t>(size), nullptr, &code);
    if (mem == nullptr || code != CL_SUCCESS) {
        status = is_oom(code) ? AllocStatus::OutOfMemory : code == CL_INVALID_BUFFER_SIZE ? AllocStatus::TooLarge : AllocStatus::Failed;
        detail = "clCreateBuffer failed: " + std::to_string(code);
        return nullptr;
    }
    auto buffer = std::make_unique<BufferCl>();
    buffer->runtime = runtime_.get();
    buffer->mem = mem;
    buffer->size = size;
    buffer->kind = kind;
    status = AllocStatus::Ok;
    return buffer;
}

bool OpenClBackend::map(GpuBuffer& buffer, void** pointer, std::string& error) {
    auto& b = static_cast<BufferCl&>(buffer);
    cl_int code = CL_SUCCESS;
    b.mapped = runtime_->map_buffer(static_cast<cl_command_queue>(queue_), b.mem, CL_TRUE, CL_MAP_READ | CL_MAP_WRITE, 0,
                                    static_cast<size_t>(b.size), 0, nullptr, nullptr, &code);
    if (b.mapped == nullptr || code != CL_SUCCESS) {
        error = "clEnqueueMapBuffer failed: " + std::to_string(code);
        return false;
    }
    *pointer = b.mapped;
    return true;
}

bool OpenClBackend::unmap(GpuBuffer& buffer, std::string& error) {
    auto& b = static_cast<BufferCl&>(buffer);
    if (b.mapped == nullptr) {
        return true;
    }
    const cl_int code = runtime_->unmap(static_cast<cl_command_queue>(queue_), b.mem, b.mapped, 0, nullptr, nullptr);
    b.mapped = nullptr;
    if (code != CL_SUCCESS) {
        error = "clEnqueueUnmapMemObject failed: " + std::to_string(code);
        return false;
    }
    return finish(error);
}

bool OpenClBackend::upload(GpuBuffer& buffer, std::uint64_t offset, const void* data, std::uint64_t size,
                           std::string& error) {
    const cl_int code = runtime_->write_buffer(static_cast<cl_command_queue>(queue_), static_cast<BufferCl&>(buffer).mem,
                                               CL_TRUE, static_cast<size_t>(offset), static_cast<size_t>(size), data, 0,
                                               nullptr, nullptr);
    if (code != CL_SUCCESS) {
        error = "clEnqueueWriteBuffer failed: " + std::to_string(code);
        return false;
    }
    return true;
}

bool OpenClBackend::download(GpuBuffer& buffer, std::uint64_t offset, void* out, std::uint64_t size,
                             std::string& error) {
    const cl_int code = runtime_->read_buffer(static_cast<cl_command_queue>(queue_), static_cast<BufferCl&>(buffer).mem,
                                              CL_TRUE, static_cast<size_t>(offset), static_cast<size_t>(size), out, 0,
                                              nullptr, nullptr);
    if (code != CL_SUCCESS) {
        error = "clEnqueueReadBuffer failed: " + std::to_string(code);
        return false;
    }
    return true;
}

bool OpenClBackend::copy(GpuBuffer& source, std::uint64_t source_offset, GpuBuffer& destination,
                         std::uint64_t destination_offset, std::uint64_t size, std::string& error) {
    const cl_int code = runtime_->copy_buffer(static_cast<cl_command_queue>(queue_), static_cast<BufferCl&>(source).mem,
                                              static_cast<BufferCl&>(destination).mem, static_cast<size_t>(source_offset),
                                              static_cast<size_t>(destination_offset), static_cast<size_t>(size), 0,
                                              nullptr, nullptr);
    if (code != CL_SUCCESS) {
        error = "clEnqueueCopyBuffer failed: " + std::to_string(code);
        return false;
    }
    return true;
}

bool OpenClBackend::finish(std::string& error) {
    const cl_int code = runtime_->finish(static_cast<cl_command_queue>(queue_));
    if (code != CL_SUCCESS) {
        error = "clFinish failed: " + std::to_string(code);
        return false;
    }
    return true;
}

std::uint64_t OpenClBackend::max_allocation() {
    cl_ulong value = 0;
    runtime_->get_device_info(runtime_->device, CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(value), &value, nullptr);
    return value;
}

std::uint64_t OpenClBackend::available_memory() {
    cl_ulong value = 0;
    runtime_->get_device_info(runtime_->device, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(value), &value, nullptr);
    return value;
}

std::uint64_t OpenClBackend::oversize_request() {
    return available_memory() * 2 + (1ull << 30);
}

bool OpenClBackend::d3d11_sharing_advertised() {
    std::size_t size = 0;
    runtime_->get_device_info(runtime_->device, CL_DEVICE_EXTENSIONS, 0, nullptr, &size);
    std::string extensions(size, '\0');
    runtime_->get_device_info(runtime_->device, CL_DEVICE_EXTENSIONS, size, extensions.data(), nullptr);
    return (" " + extensions + " ").find(" cl_khr_d3d11_sharing ") != std::string::npos;
}

bool OpenClBackend::read_d3d11_texture(void* d3d11_device, void* d3d11_texture, std::uint32_t width, std::uint32_t height,
                                       std::vector<std::uint8_t>& tight, std::string& error) {
    OpenClRuntime& rt = *runtime_;
    if (rt.ext_address == nullptr || rt.read_image == nullptr) {
        error = "the OpenCL loader has no extension lookup";
        return false;
    }
    using GetIds = cl_int(__stdcall*)(cl_platform_id, cl_d3d11_device_source_khr, void*, cl_d3d11_device_set_khr, cl_uint, cl_device_id*, cl_uint*);
    using CreateTexture = cl_mem(__stdcall*)(cl_context, cl_mem_flags, void*, UINT, cl_int*);
    using Acquire = cl_int(__stdcall*)(cl_command_queue, cl_uint, const cl_mem*, cl_uint, const cl_event*, cl_event*);
    auto get_ids = reinterpret_cast<GetIds>(rt.ext_address(rt.platform, "clGetDeviceIDsFromD3D11KHR"));
    auto create_texture = reinterpret_cast<CreateTexture>(rt.ext_address(rt.platform, "clCreateFromD3D11Texture2DKHR"));
    auto acquire = reinterpret_cast<Acquire>(rt.ext_address(rt.platform, "clEnqueueAcquireD3D11ObjectsKHR"));
    auto release = reinterpret_cast<Acquire>(rt.ext_address(rt.platform, "clEnqueueReleaseD3D11ObjectsKHR"));
    if (get_ids == nullptr || create_texture == nullptr || acquire == nullptr || release == nullptr) {
        error = "the platform does not resolve the cl_khr_d3d11_sharing entry points";
        return false;
    }
    cl_device_id device = nullptr;
    cl_uint count = 0;
    cl_int code = get_ids(rt.platform, CL_D3D11_DEVICE_KHR, d3d11_device, CL_PREFERRED_DEVICES_FOR_D3D11_KHR, 1, &device, &count);
    if (code != CL_SUCCESS || count == 0) {
        error = "clGetDeviceIDsFromD3D11KHR returned " + std::to_string(code) + " with " + std::to_string(count) + " device(s)";
        return false;
    }
    cl_context_properties properties[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(rt.platform),
                                          CL_CONTEXT_D3D11_DEVICE_KHR, reinterpret_cast<cl_context_properties>(d3d11_device), 0};
    cl_context context = rt.create_context(properties, 1, &device, nullptr, nullptr, &code);
    if (context == nullptr) {
        error = "clCreateContext with the D3D11 device failed: " + std::to_string(code);
        return false;
    }
    cl_command_queue queue = rt.create_queue(context, device, 0, &code);
    cl_mem image = queue != nullptr ? create_texture(context, CL_MEM_READ_ONLY, d3d11_texture, 0, &code) : nullptr;
    bool ok = image != nullptr;
    if (!ok) {
        error = "clCreateFromD3D11Texture2DKHR failed: " + std::to_string(code);
    } else {
        tight.assign(static_cast<std::size_t>(width) * height * 4, 0);
        const size_t origin[3] = {0, 0, 0};
        const size_t region[3] = {width, height, 1};
        ok = acquire(queue, 1, &image, 0, nullptr, nullptr) == CL_SUCCESS &&
             rt.read_image(queue, image, CL_TRUE, origin, region, 0, 0, tight.data(), 0, nullptr, nullptr) == CL_SUCCESS;
        release(queue, 1, &image, 0, nullptr, nullptr);
        rt.finish(queue);
        if (!ok) {
            error = "acquiring or reading the shared D3D11 texture failed";
        }
    }
    if (image != nullptr) {
        rt.release_mem(image);
    }
    if (queue != nullptr) {
        rt.release_queue(queue);
    }
    rt.release_context(context);
    return ok;
}

bool OpenClBackend::churn_program(std::string& error) {
    OpenClRuntime& rt = *runtime_;
    const char* source = "__kernel void k(__global int* d) { d[get_global_id(0)] += 1; }";
    cl_int code = CL_SUCCESS;
    cl_program program = rt.create_program(rt.context, 1, &source, nullptr, &code);
    if (program == nullptr) {
        error = "clCreateProgramWithSource failed: " + std::to_string(code);
        return false;
    }
    code = rt.build_program(program, 1, &rt.device, nullptr, nullptr, nullptr);
    cl_kernel kernel = code == CL_SUCCESS ? rt.create_kernel(program, "k", &code) : nullptr;
    if (kernel != nullptr) {
        rt.release_kernel(kernel);
    }
    rt.release_program(program);
    if (code != CL_SUCCESS) {
        error = "building the OpenCL program failed: " + std::to_string(code);
        return false;
    }
    return true;
}

bool OpenClBackend::run_kernel_load(double seconds, std::string& error) {
    OpenClRuntime& rt = *runtime_;
    const char* source =
        "__kernel void churn(__global float* data, int rounds) {"
        "  size_t i = get_global_id(0); float v = data[i];"
        "  for (int r = 0; r < rounds; ++r) { v = v * 1.0001f + 0.5f; }"
        "  data[i] = v; }";
    cl_int code = CL_SUCCESS;
    cl_program program = rt.create_program(rt.context, 1, &source, nullptr, &code);
    if (program == nullptr || rt.build_program(program, 1, &rt.device, nullptr, nullptr, nullptr) != CL_SUCCESS) {
        error = "building the OpenCL load kernel failed";
        return false;
    }
    cl_kernel kernel = rt.create_kernel(program, "churn", &code);
    const size_t items = 1 << 20;
    cl_mem buffer = rt.create_buffer(rt.context, CL_MEM_READ_WRITE, items * sizeof(float), nullptr, &code);
    if (kernel == nullptr || buffer == nullptr) {
        error = "creating the OpenCL load objects failed";
        return false;
    }
    const cl_int rounds = 2000;
    rt.set_arg(kernel, 0, sizeof(buffer), &buffer);
    rt.set_arg(kernel, 1, sizeof(rounds), &rounds);
    const DWORD end = ::GetTickCount() + static_cast<DWORD>(seconds * 1000);
    bool ok = true;
    while (static_cast<LONG>(::GetTickCount() - end) < 0 && ok) {
        ok = rt.nd_range(static_cast<cl_command_queue>(queue_), kernel, 1, nullptr, &items, nullptr, 0, nullptr, nullptr) == CL_SUCCESS &&
             rt.finish(static_cast<cl_command_queue>(queue_)) == CL_SUCCESS;
    }
    rt.release_mem(buffer);
    rt.release_kernel(kernel);
    rt.release_program(program);
    if (!ok) {
        error = "the OpenCL load kernel failed to run";
    }
    return ok;
}

std::uint64_t OpenClBackend::map_alignment() {
    cl_uint bits = 0;
    runtime_->get_device_info(runtime_->device, CL_DEVICE_MEM_BASE_ADDR_ALIGN, sizeof(bits), &bits, nullptr);
    return bits / 8;
}

std::uint64_t OpenClBackend::free_memory_kb(bool& exposed) {
    std::size_t size = 0;
    runtime_->get_device_info(runtime_->device, CL_DEVICE_EXTENSIONS, 0, nullptr, &size);
    std::string extensions(size, '\0');
    runtime_->get_device_info(runtime_->device, CL_DEVICE_EXTENSIONS, size, extensions.data(), nullptr);
    exposed = extensions.find("cl_amd_device_attribute_query") != std::string::npos;
    cl_ulong values[2] = {};
    const cl_int code = runtime_->get_device_info(runtime_->device, 0x4039, sizeof(values), values, nullptr);
    if (code != CL_SUCCESS) {
        if (exposed) {
            exposed = false;
        }
        return 0;
    }
    exposed = true;
    return values[0];
}

}  // namespace lsw::suites
