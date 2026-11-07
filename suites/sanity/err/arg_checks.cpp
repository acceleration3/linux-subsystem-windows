#include "sanity/err/arg_checks.hpp"

#include "sanity/cl/error_checks.hpp"
#include "sanity/d3d11/err_checks.hpp"
#include "sanity/d3d9/fail_checks.hpp"
#include "sanity/err/delegate.hpp"
#include "sanity/gl/ext_checks.hpp"
#include "support/api_policy.hpp"
#include "support/cl_kernel.hpp"
#include "support/cl_program.hpp"
#include "support/cl_rig.hpp"
#include "support/cl_tools.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_tools.hpp"
#include "support/problems.hpp"
#include "support/target_adapter.hpp"
#include "support/test_window.hpp"
#include "support/vulkan_rig.hpp"

#include <dxgi1_2.h>

#include <cstring>

namespace lsw::suites::sanity::err {

namespace {

template <typename E>
E raw_enum(unsigned value) {
    E result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

void expect_one_of(Problems& problems, const std::string& what, HRESULT got, std::initializer_list<HRESULT> accepted) {
    if (!D3d12Tools::in(got, accepted)) {
        problems.add(what + " returned " + D3d12Tools::hr(got) + ", expected " + D3d12Tools::accepted_text(accepted));
    }
}

}  // namespace

Outcome ArgChecks::d3d11_invalid_params(Context& context) {
    return d3d11::ErrChecks::bad_parameters(context);
}

Outcome ArgChecks::d3d9_invalid_params(Context& context) {
    return d3d9::FailChecks::bad_parameters(context);
}

Outcome ArgChecks::gl_error_flags(Context& context) {
    return gl::ExtChecks::getError_contracts(context);
}

Outcome ArgChecks::d3d12_invalid_params(Context&) {
    if (ApiPolicy::is_skipped("d3d12")) {
        return Context::skip("d3d12 excluded by LSW_SUITES_SKIP_APIS");
    }
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    ID3D12Device* device = rig.device();
    Problems problems;
    const auto invalid = {E_INVALIDARG, DXGI_ERROR_UNSUPPORTED};
    const D3D12_HEAP_PROPERTIES heap = D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT);
    {
        D3D12_RESOURCE_DESC desc = D3d12Tools::buffer_desc(0);
        Com<ID3D12Resource> resource;
        expect_one_of(problems, "CreateCommittedResource with a zero-width buffer", device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), resource.put_void()), invalid);
    }
    {
        D3D12_RESOURCE_DESC desc = D3d12Tools::texture_desc(0, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
        Com<ID3D12Resource> resource;
        expect_one_of(problems, "CreateCommittedResource with a zero-sized texture", device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), resource.put_void()), invalid);
    }
    {
        D3D12_RESOURCE_DESC desc = D3d12Tools::texture_desc(64, 64, DXGI_FORMAT_UNKNOWN);
        Com<ID3D12Resource> resource;
        expect_one_of(problems, "CreateCommittedResource with an unknown texture format", device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), resource.put_void()), invalid);
    }
    {
        D3D12_RESOURCE_DESC desc = D3d12Tools::buffer_desc(256);
        desc.Dimension = raw_enum<D3D12_RESOURCE_DIMENSION>(77);
        Com<ID3D12Resource> resource;
        expect_one_of(problems, "CreateCommittedResource with an invalid dimension", device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), resource.put_void()), invalid);
    }
    {
        D3D12_HEAP_PROPERTIES bad = heap;
        bad.Type = raw_enum<D3D12_HEAP_TYPE>(99);
        D3D12_RESOURCE_DESC desc = D3d12Tools::buffer_desc(256);
        Com<ID3D12Resource> resource;
        expect_one_of(problems, "CreateCommittedResource with an invalid heap type", device->CreateCommittedResource(&bad, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), resource.put_void()), invalid);
    }
    {
        D3D12_COMMAND_QUEUE_DESC desc{};
        desc.Type = raw_enum<D3D12_COMMAND_LIST_TYPE>(99);
        Com<ID3D12CommandQueue> queue;
        expect_one_of(problems, "CreateCommandQueue with an invalid type", device->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue), queue.put_void()), invalid);
    }
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 0;
        Com<ID3D12DescriptorHeap> heap_object;
        expect_one_of(problems, "CreateDescriptorHeap with no descriptors", device->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), heap_object.put_void()), invalid);
        desc.Type = raw_enum<D3D12_DESCRIPTOR_HEAP_TYPE>(77);
        desc.NumDescriptors = 8;
        expect_one_of(problems, "CreateDescriptorHeap with an invalid type", device->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), heap_object.put_void()), invalid);
    }
    {
        const std::uint8_t garbage[64] = {1, 2, 3, 4, 5, 6, 7, 8};
        Com<ID3D12RootSignature> signature;
        expect_one_of(problems, "CreateRootSignature with a garbage blob", device->CreateRootSignature(0, garbage, sizeof(garbage), __uuidof(ID3D12RootSignature), signature.put_void()), invalid);
    }
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        Com<ID3D12PipelineState> state;
        expect_one_of(problems, "CreateGraphicsPipelineState with an empty description", device->CreateGraphicsPipelineState(&desc, __uuidof(ID3D12PipelineState), state.put_void()), invalid);
        D3D12_COMPUTE_PIPELINE_STATE_DESC compute{};
        expect_one_of(problems, "CreateComputePipelineState with an empty description", device->CreateComputePipelineState(&compute, __uuidof(ID3D12PipelineState), state.put_void()), invalid);
    }
    {
        Com<ID3D12CommandAllocator> allocator;
        expect_one_of(problems, "CreateCommandAllocator with an invalid type", device->CreateCommandAllocator(raw_enum<D3D12_COMMAND_LIST_TYPE>(99), __uuidof(ID3D12CommandAllocator), allocator.put_void()), invalid);
    }
    {
        Com<ID3D12Fence> fence;
        expect_one_of(problems, "CreateFence with an undefined flag", device->CreateFence(0, raw_enum<D3D12_FENCE_FLAGS>(0x80), __uuidof(ID3D12Fence), fence.put_void()), invalid);
    }
    ID3D12Resource* buffer = D3d12Tools::device_buffer(rig, 4096, D3D12_RESOURCE_STATE_COMMON);
    D3d12Tools::Bytes bytes;
    if (buffer == nullptr || !D3d12Tools::write_buffer(rig, buffer, D3D12_RESOURCE_STATE_COMMON, 0, "abcdefgh", 8, error) ||
        !D3d12Tools::read_buffer(rig, buffer, D3D12_RESOURCE_STATE_COMMON, 0, 8, bytes, error) || bytes.size() < 8 || std::memcmp(bytes.data(), "abcdefgh", 8) != 0) {
        problems.add("the device is not usable after the invalid calls: " + error);
    }
    return problems.outcome();
}

Outcome ArgChecks::vulkan_invalid_handles_null(Context&) {
    std::string error;
    const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(error);
    if (adapters.empty()) {
        return Context::skip("Vulkan is unavailable: " + error);
    }
    VulkanRig rig;
    if (!rig.open(adapters.front(), VulkanRigOptions{}, error)) {
        return Outcome::fail(error);
    }
    VulkanTable& fn = rig.fn;
    VkDevice device = rig.device;
    fn.vkDestroyBuffer(device, VK_NULL_HANDLE, nullptr);
    fn.vkDestroyBufferView(device, VK_NULL_HANDLE, nullptr);
    fn.vkFreeMemory(device, VK_NULL_HANDLE, nullptr);
    fn.vkDestroyPipeline(device, VK_NULL_HANDLE, nullptr);
    fn.vkDestroySampler(device, VK_NULL_HANDLE, nullptr);
    fn.vkDestroyEvent(device, VK_NULL_HANDLE, nullptr);
    fn.vkFreeCommandBuffers(device, rig.main().pool, 0, nullptr);
    Problems problems;
    VulkanOwned<VkFence> fence;
    if (rig.make_fence(fence) != VK_SUCCESS) {
        return Outcome::fail("creating a fence failed");
    }
    VkFence raw = fence.get();
    const VkResult status = fn.vkGetFenceStatus(device, raw);
    if (status != VK_NOT_READY) {
        problems.add("vkGetFenceStatus on an unsignalled fence returned " + VulkanTools::result_name(status) + ", expected VK_NOT_READY");
    }
    const VkResult waited = fn.vkWaitForFences(device, 1, &raw, VK_TRUE, 0);
    if (waited != VK_TIMEOUT) {
        problems.add("vkWaitForFences with a zero timeout on an unsignalled fence returned " + VulkanTools::result_name(waited) + ", expected VK_TIMEOUT");
    }
    for (std::uint32_t heap = 0; heap < rig.memory.memoryHeapCount; ++heap) {
        std::uint32_t type = rig.memory.memoryTypeCount;
        for (std::uint32_t i = 0; i < rig.memory.memoryTypeCount; ++i) {
            if (rig.memory.memoryTypes[i].heapIndex == heap) {
                type = i;
                break;
            }
        }
        if (type == rig.memory.memoryTypeCount) {
            continue;
        }
        VkMemoryAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        info.allocationSize = rig.memory.memoryHeaps[heap].size * 2 + (1ull << 20);
        info.memoryTypeIndex = type;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        const VkResult result = fn.vkAllocateMemory(device, &info, nullptr, &memory);
        if (!VulkanTools::code_in(result, {VK_ERROR_OUT_OF_DEVICE_MEMORY, VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_TOO_MANY_OBJECTS})) {
            problems.add("vkAllocateMemory of twice heap " + std::to_string(heap) + " returned " + VulkanTools::result_name(result) + ", expected " +
                         VulkanTools::accepted_text({VK_ERROR_OUT_OF_DEVICE_MEMORY, VK_ERROR_OUT_OF_HOST_MEMORY}));
            if (result == VK_SUCCESS) {
                fn.vkFreeMemory(device, memory, nullptr);
            }
        }
    }
    VulkanOwned<VkFence> again;
    if (rig.make_fence(again) != VK_SUCCESS || rig.run([](VkCommandBuffer) {}) != VK_SUCCESS) {
        problems.add("the device is unusable after the null-handle and out-of-memory calls");
    }
    return problems.outcome();
}

Outcome ArgChecks::opencl_error_codes(Context& context) {
    return Delegate::all(context, {cl::ErrorChecks::codes_documented, [](Context&) -> Outcome {
        ClRig rig;
        std::string error;
        if (!rig.open(ClRigOptions{}, error)) {
            return Context::skip("OpenCL is unavailable: " + error);
        }
        Problems problems;
        auto expect = [&](const std::string& what, cl_int got, cl_int wanted) {
            if (got != wanted) {
                problems.add(what + " returned " + ClTools::error_name(got) + ", expected " + ClTools::error_name(wanted));
            }
        };
        cl_int code = CL_SUCCESS;
        cl_mem none = rig.api.CreateBuffer(rig.context, CL_MEM_READ_WRITE, 0, nullptr, &code);
        expect("clCreateBuffer of size 0", code, CL_INVALID_BUFFER_SIZE);
        if (none != nullptr) {
            rig.api.ReleaseMemObject(none);
        }
        cl_mem both = rig.api.CreateBuffer(rig.context, CL_MEM_READ_ONLY | CL_MEM_WRITE_ONLY, 64, nullptr, &code);
        expect("clCreateBuffer with READ_ONLY and WRITE_ONLY", code, CL_INVALID_VALUE);
        if (both != nullptr) {
            rig.api.ReleaseMemObject(both);
        }
        ClProgram program;
        if (!program.build(rig, "__kernel void touch(__global uint* v, uint n) { v[get_global_id(0)] = n; }", "", error)) {
            problems.add(error);
            return problems.outcome();
        }
        ClKernel kernel = program.kernel("touch", error);
        ClOwned<cl_mem> buffer(rig, rig.api.CreateBuffer(rig.context, CL_MEM_READ_WRITE, 4096, nullptr, &code));
        if (!kernel || !buffer) {
            return Outcome::fail("creating the kernel or buffer failed: " + error);
        }
        expect("clEnqueueNDRangeKernel with unset arguments", kernel.run(16), CL_INVALID_KERNEL_ARGS);
        std::size_t limit = 0;
        rig.info(CL_DEVICE_MAX_WORK_GROUP_SIZE, limit);
        const std::size_t too_many = limit + 1;
        const cl_uint count = 1;
        cl_int set = kernel.set(buffer, count);
        expect("clSetKernelArg", set, CL_SUCCESS);
        expect("clEnqueueNDRangeKernel with a work-group larger than CL_DEVICE_MAX_WORK_GROUP_SIZE", kernel.run(too_many, too_many), CL_INVALID_WORK_GROUP_SIZE);
        expect("clEnqueueNDRangeKernel with a global size not divisible by the local size", kernel.run(10, 4), CL_INVALID_WORK_GROUP_SIZE);
        expect("a valid launch after the failures", kernel.run(16), CL_SUCCESS);
        if (rig.api.Finish(rig.queue) != CL_SUCCESS) {
            problems.add("clFinish failed after the invalid calls");
        }
        return problems.outcome();
    }});
}

Outcome ArgChecks::dxgi_invalid_swapchain(Context&) {
    if (ApiPolicy::is_skipped("d3d11")) {
        return Context::skip("d3d11 excluded by LSW_SUITES_SKIP_APIS");
    }
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    TestWindow window;
    TestWindowConfig config;
    config.visible = false;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error) || !window.create(config, error)) {
        return Outcome::fail(error);
    }
    Com<IDXGIDevice> dxgi_device;
    Com<IDXGIAdapter> dxgi_adapter;
    Com<IDXGIFactory2> factory;
    device.device->QueryInterface(__uuidof(IDXGIDevice), dxgi_device.put_void());
    if (!dxgi_device || FAILED(dxgi_device->GetAdapter(dxgi_adapter.put())) || FAILED(dxgi_adapter->GetParent(__uuidof(IDXGIFactory2), factory.put_void()))) {
        return Context::skip("IDXGIFactory2 is not exposed");
    }
    Problems problems;
    auto attempt = [&](const std::string& what, DXGI_SWAP_CHAIN_DESC1 desc, bool expect_failure) {
        Com<IDXGISwapChain1> chain;
        const HRESULT result = factory->CreateSwapChainForHwnd(device.device, window.hwnd(), &desc, nullptr, nullptr, chain.put());
        if (expect_failure && !D3d12Tools::in(result, {DXGI_ERROR_INVALID_CALL, E_INVALIDARG, DXGI_ERROR_UNSUPPORTED})) {
            problems.add(what + " returned " + D3d12Tools::hr(result) + ", expected DXGI_ERROR_INVALID_CALL or E_INVALIDARG");
        }
        if (!expect_failure && FAILED(result)) {
            problems.add(what + " failed with " + D3d12Tools::hr(result));
        }
    };
    DXGI_SWAP_CHAIN_DESC1 valid{};
    valid.Width = 128;
    valid.Height = 128;
    valid.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    valid.SampleDesc.Count = 1;
    valid.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    valid.BufferCount = 2;
    valid.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    attempt("a valid flip-model swap chain", valid, false);
    DXGI_SWAP_CHAIN_DESC1 desc = valid;
    desc.BufferCount = 0;
    attempt("a flip-model swap chain with no buffers", desc, true);
    desc = valid;
    desc.BufferCount = 1;
    attempt("a flip-model swap chain with one buffer", desc, true);
    desc = valid;
    desc.BufferCount = 99;
    attempt("a swap chain with 99 buffers", desc, true);
    desc = valid;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    attempt("a flip-model swap chain with DXGI_FORMAT_UNKNOWN", desc, true);
    desc = valid;
    desc.Format = DXGI_FORMAT_R32G32B32A32_TYPELESS;
    attempt("a swap chain with a typeless 128-bit format", desc, true);
    desc = valid;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    attempt("a swap chain with a depth format", desc, true);
    desc = valid;
    desc.SampleDesc.Count = 4;
    attempt("a flip-model swap chain with four samples", desc, true);
    desc = valid;
    desc.Width = 1u << 24;
    desc.Height = 1u << 24;
    attempt("a swap chain of 16777216 x 16777216", desc, true);
    desc = valid;
    desc.SwapEffect = raw_enum<DXGI_SWAP_EFFECT>(99);
    attempt("a swap chain with an invalid swap effect", desc, true);
    desc = valid;
    desc.Scaling = raw_enum<DXGI_SCALING>(99);
    attempt("a swap chain with an invalid scaling mode", desc, true);
    attempt("a valid swap chain after the invalid ones", valid, false);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::err
