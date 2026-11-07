#include "sanity/err/limit_checks.hpp"

#include "sanity/sync/sync_device.hpp"
#include "support/api_policy.hpp"
#include "support/cl_kernel.hpp"
#include "support/cl_program.hpp"
#include "support/cl_rig.hpp"
#include "support/cl_tools.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_tools.hpp"
#include "support/problems.hpp"
#include "support/process_sampler.hpp"
#include "support/target_adapter.hpp"
#include "support/vulkan_rig.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::sanity::err {

namespace {

constexpr int kObjects = 100000;
constexpr std::uint64_t kPrivateSlack = 96ull << 20;
constexpr std::uint32_t kHandleSlack = 64;

void judge_baseline(Problems& problems, const std::string& api, const ProcessSample& before, const ProcessSample& after) {
    if (after.private_bytes > before.private_bytes + kPrivateSlack) {
        problems.add(api + ": private bytes grew from " + std::to_string(before.private_bytes >> 20) + " to " + std::to_string(after.private_bytes >> 20) + " MiB after releasing every object");
    }
    if (after.handles > before.handles + kHandleSlack) {
        problems.add(api + ": handles grew from " + std::to_string(before.handles) + " to " + std::to_string(after.handles) + " after releasing every object");
    }
}

void many_d3d11(Context& context, Problems& problems) {
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        problems.add(error);
        return;
    }
    const ProcessSample before = ProcessSampler::take();
    int made = 0;
    {
        std::vector<Com<ID3D11Buffer>> buffers;
        buffers.reserve(kObjects);
        HRESULT last = S_OK;
        for (; made < kObjects; ++made) {
            Com<ID3D11Buffer> buffer = D3d11Tools::buffer(device, 256, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, nullptr, &last);
            if (!buffer) {
                break;
            }
            buffers.push_back(std::move(buffer));
        }
        context.measure("d3d11.buffers", made);
        if (made < kObjects && !D3d12Tools::in(last, {E_OUTOFMEMORY, DXGI_ERROR_DEVICE_REMOVED, E_FAIL})) {
            problems.add("d3d11: creating buffer " + std::to_string(made) + " failed with " + D3d12Tools::hr(last) + " instead of E_OUTOFMEMORY");
        }
        if (!buffers.empty()) {
            device.context->UpdateSubresource(buffers.back().get(), 0, nullptr, "x", 0, 0);
        }
    }
    static_cast<void>(device.wait_idle(error));
    judge_baseline(problems, "d3d11", before, ProcessSampler::take());
}

void many_d3d12(Context& context, Problems& problems) {
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        problems.add(error);
        return;
    }
    const ProcessSample before = ProcessSampler::take();
    {
        std::vector<Com<ID3D12Fence>> fences;
        fences.reserve(kObjects);
        HRESULT last = S_OK;
        for (int i = 0; i < kObjects; ++i) {
            Com<ID3D12Fence> fence;
            last = rig.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), fence.put_void());
            if (FAILED(last)) {
                if (!D3d12Tools::in(last, {E_OUTOFMEMORY})) {
                    problems.add("d3d12: creating fence " + std::to_string(i) + " failed with " + D3d12Tools::hr(last));
                }
                break;
            }
            fences.push_back(std::move(fence));
        }
        context.measure("d3d12.fences", static_cast<long long>(fences.size()));
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 1000000;
        Com<ID3D12DescriptorHeap> heap;
        const HRESULT heap_result = rig.device()->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), heap.put_void());
        context.measure("d3d12.descriptor_heap", D3d12Tools::hr(heap_result));
        if (FAILED(heap_result) && !D3d12Tools::in(heap_result, {E_OUTOFMEMORY, E_INVALIDARG, DXGI_ERROR_UNSUPPORTED})) {
            problems.add("d3d12: a heap of 1000000 descriptors failed with " + D3d12Tools::hr(heap_result));
        }
        std::vector<Com<ID3D12Resource>> buffers;
        const D3D12_HEAP_PROPERTIES props = D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC buffer_desc = D3d12Tools::buffer_desc(65536);
        for (int i = 0; i < 4096; ++i) {
            Com<ID3D12Resource> resource;
            const HRESULT result = rig.device()->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &buffer_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), resource.put_void());
            if (FAILED(result)) {
                if (!D3d12Tools::in(result, {E_OUTOFMEMORY})) {
                    problems.add("d3d12: committed buffer " + std::to_string(i) + " failed with " + D3d12Tools::hr(result));
                }
                break;
            }
            buffers.push_back(std::move(resource));
        }
        context.measure("d3d12.committed", static_cast<long long>(buffers.size()));
    }
    if (!rig.idle(error)) {
        problems.add(error);
    }
    judge_baseline(problems, "d3d12", before, ProcessSampler::take());
}

void many_vulkan(Context& context, Problems& problems) {
    std::string error;
    const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(error);
    if (adapters.empty()) {
        return;
    }
    VulkanRig rig;
    if (!rig.open(adapters.front(), VulkanRigOptions{}, error)) {
        problems.add(error);
        return;
    }
    const ProcessSample before = ProcessSampler::take();
    std::vector<VkBuffer> buffers;
    buffers.reserve(kObjects);
    VkResult last = VK_SUCCESS;
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = 256;
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    for (int i = 0; i < kObjects; ++i) {
        VkBuffer buffer = VK_NULL_HANDLE;
        last = rig.fn.vkCreateBuffer(rig.device, &info, nullptr, &buffer);
        if (last != VK_SUCCESS) {
            break;
        }
        buffers.push_back(buffer);
    }
    context.measure("vulkan.buffers", static_cast<long long>(buffers.size()));
    if (last != VK_SUCCESS && !VulkanTools::code_in(last, {VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_OUT_OF_DEVICE_MEMORY, VK_ERROR_TOO_MANY_OBJECTS})) {
        problems.add("vulkan: creating buffer " + std::to_string(buffers.size()) + " failed with " + VulkanTools::result_name(last));
    }
    for (VkBuffer buffer : buffers) {
        rig.fn.vkDestroyBuffer(rig.device, buffer, nullptr);
    }
    judge_baseline(problems, "vulkan", before, ProcessSampler::take());
}

void many_opencl(Context& context, Problems& problems) {
    ClRig rig;
    std::string error;
    if (!rig.open(ClRigOptions{}, error)) {
        return;
    }
    const ProcessSample before = ProcessSampler::take();
    std::vector<cl_mem> buffers;
    buffers.reserve(kObjects);
    cl_int last = CL_SUCCESS;
    for (int i = 0; i < kObjects; ++i) {
        cl_mem buffer = rig.api.CreateBuffer(rig.context, CL_MEM_READ_WRITE, 256, nullptr, &last);
        if (last != CL_SUCCESS || buffer == nullptr) {
            break;
        }
        buffers.push_back(buffer);
    }
    context.measure("opencl.buffers", static_cast<long long>(buffers.size()));
    if (last != CL_SUCCESS && last != CL_OUT_OF_HOST_MEMORY && last != CL_OUT_OF_RESOURCES && last != CL_MEM_OBJECT_ALLOCATION_FAILURE) {
        problems.add("opencl: creating buffer " + std::to_string(buffers.size()) + " failed with " + ClTools::error_name(last));
    }
    for (cl_mem buffer : buffers) {
        rig.api.ReleaseMemObject(buffer);
    }
    rig.drain();
    judge_baseline(problems, "opencl", before, ProcessSampler::take());
}

}  // namespace

Outcome LimitChecks::handle_count(Context& context) {
    Problems problems;
    if (!ApiPolicy::is_skipped("d3d11")) {
        many_d3d11(context, problems);
    }
    if (!ApiPolicy::is_skipped("d3d12")) {
        many_d3d12(context, problems);
    }
    if (!ApiPolicy::is_skipped("vulkan")) {
        many_vulkan(context, problems);
    }
    if (!ApiPolicy::is_skipped("opencl")) {
        many_opencl(context, problems);
    }
    return problems.outcome();
}

Outcome LimitChecks::max_texture_dims(Context& context) {
    Problems problems;
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    int ran = 0;
    if (!ApiPolicy::is_skipped("d3d11")) {
        D3d11Device device;
        if (device.create(adapter.luid, error)) {
            ++ran;
            const UINT max = device.level >= D3D_FEATURE_LEVEL_11_0 ? 16384 : device.level >= D3D_FEATURE_LEVEL_10_0 ? 8192 : device.level >= D3D_FEATURE_LEVEL_9_3 ? 4096 : 2048;
            context.measure("d3d11.max_dimension", static_cast<long long>(max));
            for (auto [w, h] : {std::pair<UINT, UINT>{max, 1}, {1, max}, {max, max}}) {
                HRESULT result = S_OK;
                Com<ID3D11Texture2D> texture = D3d11Tools::texture(device, D3d11Tools::desc2d(w, h, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE), nullptr, &result);
                if (!texture) {
                    problems.add("d3d11: a " + std::to_string(w) + "x" + std::to_string(h) + " R8 texture failed with " + D3d12Tools::hr(result));
                    continue;
                }
                const std::uint8_t data[64] = {1, 2, 3, 4, 5, 6, 7, 8};
                const D3D11_BOX box{w >= 64 ? w - 64 : 0, h - 1, 0, w, h, 1};
                device.context->UpdateSubresource(texture.get(), 0, &box, data, 64, 0);
                Com<ID3D11Texture2D> staging = D3d11Tools::texture(device, D3d11Tools::desc2d(64, 1, DXGI_FORMAT_R8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
                const D3D11_BOX source{w >= 64 ? w - 64 : 0, h - 1, 0, w, h, 1};
                device.context->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, texture.get(), 0, &source);
                TexelImage image;
                if (!staging || !D3d11Tools::read_texture(device, staging.get(), 0, image, error)) {
                    problems.add("d3d11: reading back the far corner of the maximum texture failed: " + error);
                } else if (std::memcmp(image.data.data(), data, std::min<std::size_t>(64, image.data.size())) != 0 && w >= 64) {
                    problems.add("d3d11: the far corner of a " + std::to_string(w) + "x" + std::to_string(h) + " texture does not hold what was written");
                }
            }
            HRESULT result = S_OK;
            Com<ID3D11Texture2D> over = D3d11Tools::texture(device, D3d11Tools::desc2d(max + 1, 1, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE), nullptr, &result);
            if (over || !D3d12Tools::in(result, {E_INVALIDARG, E_OUTOFMEMORY})) {
                problems.add("d3d11: a texture one pixel wider than the maximum returned " + D3d12Tools::hr(result));
            }
        }
    }
    if (!ApiPolicy::is_skipped("d3d12")) {
        D3d12Rig rig;
        if (rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
            ++ran;
            constexpr UINT max = 16384;
            for (auto [w, h] : {std::pair<UINT, UINT>{max, 1}, {1, max}, {max, max}}) {
                HRESULT result = S_OK;
                ID3D12Resource* texture = D3d12Tools::texture(rig, D3d12Tools::texture_desc(w, h, DXGI_FORMAT_R8_UNORM), D3D12_RESOURCE_STATE_COMMON, nullptr, &result);
                if (texture == nullptr) {
                    problems.add("d3d12: a " + std::to_string(w) + "x" + std::to_string(h) + " R8 texture failed with " + D3d12Tools::hr(result));
                    continue;
                }
                if (w * h <= max) {
                    std::vector<std::uint8_t> data(static_cast<std::size_t>(w) * h);
                    for (std::size_t i = 0; i < data.size(); ++i) {
                        data[i] = static_cast<std::uint8_t>(i * 7 + 3);
                    }
                    TexelImage image;
                    if (!D3d12Tools::write_texture(rig, texture, D3D12_RESOURCE_STATE_COMMON, 0, data.data(), error) ||
                        !D3d12Tools::read_texture(rig, texture, D3D12_RESOURCE_STATE_COMMON, 0, image, error)) {
                        problems.add("d3d12: using the " + std::to_string(w) + "x" + std::to_string(h) + " texture failed: " + error);
                    } else if (image.data.size() < data.size() || std::memcmp(image.data.data(), data.data(), data.size()) != 0) {
                        problems.add("d3d12: the " + std::to_string(w) + "x" + std::to_string(h) + " texture does not hold what was written");
                    }
                }
                rig.release(texture);
            }
            HRESULT result = S_OK;
            ID3D12Resource* over = D3d12Tools::texture(rig, D3d12Tools::texture_desc(max + 1, 1, DXGI_FORMAT_R8_UNORM), D3D12_RESOURCE_STATE_COMMON, nullptr, &result);
            if (over != nullptr || !D3d12Tools::in(result, {E_INVALIDARG, DXGI_ERROR_UNSUPPORTED})) {
                problems.add("d3d12: a texture one pixel wider than 16384 returned " + D3d12Tools::hr(result));
            }
        }
    }
    if (!ApiPolicy::is_skipped("opencl")) {
        ClRig rig;
        if (rig.open(ClRigOptions{}, error) && rig.images()) {
            ++ran;
            const std::size_t width = rig.size_info(CL_DEVICE_IMAGE2D_MAX_WIDTH);
            const std::size_t height = rig.size_info(CL_DEVICE_IMAGE2D_MAX_HEIGHT);
            context.measure("opencl.image2d_max", static_cast<long long>(width * 100000 + height));
            const cl_image_format format{CL_R, CL_UNORM_INT8};
            for (auto [w, h] : {std::pair<std::size_t, std::size_t>{width, 1}, {1, height}}) {
                cl_image_desc desc{};
                desc.image_type = CL_MEM_OBJECT_IMAGE2D;
                desc.image_width = w;
                desc.image_height = h;
                cl_int code = CL_SUCCESS;
                cl_mem image = rig.api.CreateImage(rig.context, CL_MEM_READ_WRITE, &format, &desc, nullptr, &code);
                if (code != CL_SUCCESS || image == nullptr) {
                    problems.add("opencl: a " + std::to_string(w) + "x" + std::to_string(h) + " image failed with " + ClTools::error_name(code));
                    continue;
                }
                std::vector<std::uint8_t> data(w * h);
                for (std::size_t i = 0; i < data.size(); ++i) {
                    data[i] = static_cast<std::uint8_t>(i * 5 + 1);
                }
                std::vector<std::uint8_t> back(data.size());
                const std::size_t origin[3] = {0, 0, 0};
                const std::size_t region[3] = {w, h, 1};
                if (rig.api.EnqueueWriteImage(rig.queue, image, CL_TRUE, origin, region, 0, 0, data.data(), 0, nullptr, nullptr) != CL_SUCCESS ||
                    rig.api.EnqueueReadImage(rig.queue, image, CL_TRUE, origin, region, 0, 0, back.data(), 0, nullptr, nullptr) != CL_SUCCESS || back != data) {
                    problems.add("opencl: the " + std::to_string(w) + "x" + std::to_string(h) + " image does not roundtrip");
                }
                rig.api.ReleaseMemObject(image);
            }
            cl_image_desc over{};
            over.image_type = CL_MEM_OBJECT_IMAGE2D;
            over.image_width = width + 1;
            over.image_height = 1;
            cl_int code = CL_SUCCESS;
            cl_mem image = rig.api.CreateImage(rig.context, CL_MEM_READ_WRITE, &format, &over, nullptr, &code);
            if (image != nullptr || code != CL_INVALID_IMAGE_SIZE) {
                problems.add("opencl: an image one pixel wider than CL_DEVICE_IMAGE2D_MAX_WIDTH returned " + ClTools::error_name(code));
                if (image != nullptr) {
                    rig.api.ReleaseMemObject(image);
                }
            }
        }
    }
    if (ran == 0 && problems.empty()) {
        return Context::skip("no API was available");
    }
    return problems.outcome();
}

Outcome LimitChecks::max_threads_workgroup(Context& context) {
    Problems problems;
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    int ran = 0;
    auto hlsl = [](UINT threads) {
        return "RWStructuredBuffer<uint> o : register(u0);\n[numthreads(" + std::to_string(threads) + ", 1, 1)]\nvoid main(uint3 id : SV_DispatchThreadID, uint3 gid : SV_GroupID) { o[id.x] = id.x * 3u + 1u; }\n";
    };
    if (!ApiPolicy::is_skipped("d3d11")) {
        D3d11Device device;
        if (device.create(adapter.luid, error) && device.level >= D3D_FEATURE_LEVEL_11_0) {
            ++ran;
            Com<ID3D11ComputeShader> big = D3d11Tools::cs(device, hlsl(1024), error);
            Com<ID3D11Buffer> buffer = D3d11Tools::buffer(device, 2048 * 4, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, nullptr);
            Com<ID3D11UnorderedAccessView> view = buffer ? D3d11Tools::structured_uav(device, buffer.get(), 2048) : Com<ID3D11UnorderedAccessView>();
            if (!big || !view) {
                problems.add("d3d11: the 1024-thread shader or its buffer could not be created: " + error);
            } else {
                ID3D11UnorderedAccessView* uav = view.get();
                device.context->CSSetShader(big.get(), nullptr, 0);
                device.context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
                device.context->Dispatch(2, 1, 1);
                const std::vector<std::uint32_t> words = D3d11Tools::read_words(device, buffer.get(), error);
                bool good = words.size() == 2048;
                for (std::size_t i = 0; good && i < words.size(); ++i) {
                    good = words[i] == i * 3u + 1u;
                }
                if (!good) {
                    problems.add("d3d11: dispatching 2 groups of 1024 threads gave wrong results");
                }
            }
            std::string refusal;
            Com<ID3D11ComputeShader> over = D3d11Tools::cs(device, hlsl(1025), refusal);
            if (over) {
                problems.add("d3d11: a compute shader with 1025 threads per group was accepted");
            }
        }
    }
    if (!ApiPolicy::is_skipped("d3d12")) {
        D3d12Rig rig;
        if (rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
            ++ran;
            D3d12Tools::Bytes code;
            std::string refusal;
            if (D3d12Tools::compile(hlsl(1025), "main", "cs_5_1", code, refusal)) {
                problems.add("d3d12: a compute shader with 1025 threads per group compiled");
            }
            if (!D3d12Tools::compile(hlsl(1024), "main", "cs_5_1", code, error)) {
                problems.add("d3d12: the 1024-thread shader did not compile: " + error);
            }
        }
    }
    if (!ApiPolicy::is_skipped("opencl")) {
        ClRig rig;
        if (rig.open(ClRigOptions{}, error)) {
            ++ran;
            ClProgram program;
            if (!program.build(rig, "__kernel void fill(__global uint* o) { o[get_global_id(0)] = (uint)get_local_id(0) + 1u; }", "", error)) {
                problems.add(error);
            } else {
                ClKernel kernel = program.kernel("fill", error);
                const std::size_t device_max = rig.size_info(CL_DEVICE_MAX_WORK_GROUP_SIZE);
                const std::size_t local = kernel ? std::min(device_max, kernel.work_group_size()) : 0;
                cl_int code = CL_SUCCESS;
                ClOwned<cl_mem> buffer(rig, rig.api.CreateBuffer(rig.context, CL_MEM_READ_WRITE, local * 2 * 4 + 4, nullptr, &code));
                if (!kernel || !buffer || local == 0 || kernel.set(buffer) != CL_SUCCESS) {
                    problems.add("opencl: preparing the kernel failed: " + error);
                } else {
                    context.measure("opencl.local_size", static_cast<long long>(local));
                    code = kernel.run(local * 2, local);
                    std::vector<std::uint32_t> out(local * 2);
                    if (code != CL_SUCCESS || rig.api.EnqueueReadBuffer(rig.queue, buffer.get(), CL_TRUE, 0, out.size() * 4, out.data(), 0, nullptr, nullptr) != CL_SUCCESS) {
                        problems.add("opencl: a launch at the maximum work-group size " + std::to_string(local) + " failed: " + ClTools::error_name(code));
                    } else {
                        for (std::size_t i = 0; i < out.size(); ++i) {
                            if (out[i] != i % local + 1) {
                                problems.add("opencl: work item " + std::to_string(i) + " at the maximum work-group size wrote " + std::to_string(out[i]));
                                break;
                            }
                        }
                    }
                    code = kernel.run((device_max + 1) * 2, device_max + 1);
                    if (code != CL_INVALID_WORK_GROUP_SIZE) {
                        problems.add("opencl: a work-group of CL_DEVICE_MAX_WORK_GROUP_SIZE + 1 returned " + ClTools::error_name(code));
                    }
                }
            }
        }
    }
    if (ran == 0 && problems.empty()) {
        return Context::skip("no API was available");
    }
    return problems.outcome();
}

Outcome LimitChecks::many_contexts_per_process(Context& context) {
    constexpr int kGuaranteed = 64;
    constexpr int kCap = 256;
    Problems problems;
    int ran = 0;
    for (const std::string& api : ApiPolicy::filter({"d3d11", "d3d12", "vulkan", "opencl"})) {
        std::vector<std::unique_ptr<sync::SyncDevice>> devices;
        std::string error;
        for (int i = 0; i < kCap; ++i) {
            std::unique_ptr<sync::SyncDevice> next = sync::SyncDevice::make(api);
            if (next == nullptr || !next->open(error)) {
                break;
            }
            devices.push_back(std::move(next));
        }
        if (devices.empty()) {
            continue;
        }
        ++ran;
        context.measure(api + ".contexts", static_cast<long long>(devices.size()));
        if (devices.size() < kGuaranteed) {
            problems.add(api + ": only " + std::to_string(devices.size()) + " contexts could be created before the first failure (" + error + "), expected at least " + std::to_string(kGuaranteed));
        }
        for (std::size_t i = 0; i < devices.size(); i += std::max<std::size_t>(1, devices.size() / 8)) {
            std::string detail;
            std::uint32_t value = 0;
            if (!devices[i]->set(0, static_cast<std::uint32_t>(i) + 1, error) || !devices[i]->signal(error) || devices[i]->wait(30000, detail) != sync::SyncState::Done || !devices[i]->read(0, value, error) ||
                value != i + 1) {
                problems.add(api + ": context " + std::to_string(i) + " does not compute correctly: " + error + detail);
            }
        }
    }
    if (ran == 0 && problems.empty()) {
        return Context::skip("no API was available");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::err
