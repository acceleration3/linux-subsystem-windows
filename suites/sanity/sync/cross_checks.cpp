#include "sanity/sync/cross_checks.hpp"

#include "core/text.hpp"
#include "sanity/interop/gld3d_checks.hpp"
#include "support/api_policy.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_root.hpp"
#include "support/d3d12_tools.hpp"
#include "support/problems.hpp"
#include "support/target_adapter.hpp"
#include "support/vulkan_buffer.hpp"
#include "support/vulkan_compute.hpp"
#include "support/vulkan_interop.hpp"
#include "support/vulkan_shaders.hpp"

#include <d3d11_4.h>

#include <cstring>

namespace lsw::suites::sanity::sync {

namespace {

constexpr UINT kSize = 64;

std::uint32_t pack(int r, int g, int b) {
    return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) | (static_cast<std::uint32_t>(b) << 16) | 0xff000000u;
}

bool reaches(ID3D12Fence* fence, std::uint64_t value, DWORD timeout) {
    if (fence->GetCompletedValue() >= value) {
        return true;
    }
    HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event == nullptr || FAILED(fence->SetEventOnCompletion(value, event))) {
        if (event != nullptr) {
            ::CloseHandle(event);
        }
        return false;
    }
    const DWORD waited = ::WaitForSingleObject(event, timeout);
    ::CloseHandle(event);
    return waited == WAIT_OBJECT_0;
}

}  // namespace

Outcome CrossChecks::d3d11_d3d12_fence(Context&) {
    if (ApiPolicy::is_skipped("d3d11") || ApiPolicy::is_skipped("d3d12")) {
        return Context::skip("d3d11 or d3d12 excluded by LSW_SUITES_SKIP_APIS");
    }
    AvAdapter adapter;
    std::string error;
    D3d11Device device11;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !device11.create(adapter.luid, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    ID3D11Device5* raw5 = nullptr;
    ID3D11DeviceContext4* raw4 = nullptr;
    device11.device->QueryInterface(__uuidof(ID3D11Device5), reinterpret_cast<void**>(&raw5));
    device11.context->QueryInterface(__uuidof(ID3D11DeviceContext4), reinterpret_cast<void**>(&raw4));
    Com<ID3D11Device5> device_fence(raw5);
    Com<ID3D11DeviceContext4> context_fence(raw4);
    if (!device_fence || !context_fence) {
        return Context::skip("ID3D11Device5 or ID3D11DeviceContext4 is not exposed, so fences are unsupported on D3D11");
    }
    Com<ID3D12Fence> fence12;
    const HRESULT made = rig.device()->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), fence12.put_void());
    if (FAILED(made)) {
        return Context::skip("shared fences are unsupported: CreateFence(SHARED) returned " + D3d12Tools::hr(made));
    }
    HANDLE fence_handle = nullptr;
    if (FAILED(rig.device()->CreateSharedHandle(fence12.get(), nullptr, GENERIC_ALL, nullptr, &fence_handle))) {
        return Outcome::fail("CreateSharedHandle of the D3D12 fence failed");
    }
    Com<ID3D11Fence> fence11;
    const HRESULT opened = device_fence->OpenSharedFence(fence_handle, __uuidof(ID3D11Fence), fence11.put_void());
    ::CloseHandle(fence_handle);
    if (FAILED(opened)) {
        return Outcome::fail("OpenSharedFence of a D3D12 shared fence failed: " + D3d11Tools::hr(opened));
    }
    D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    Com<ID3D11Texture2D> texture = D3d11Tools::texture(device11, desc);
    Com<IDXGIResource1> resource;
    if (texture) {
        texture->QueryInterface(__uuidof(IDXGIResource1), resource.put_void());
    }
    HANDLE texture_handle = nullptr;
    if (!resource || FAILED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &texture_handle))) {
        return Context::skip("an NT-handle shared D3D11 texture cannot be created, so D3D11/D3D12 resource sharing is unsupported");
    }
    Com<ID3D12Resource> texture12;
    const HRESULT shared = rig.device()->OpenSharedHandle(texture_handle, __uuidof(ID3D12Resource), texture12.put_void());
    ::CloseHandle(texture_handle);
    if (FAILED(shared)) {
        return Outcome::fail("D3D12 cannot open the D3D11 shared texture: " + D3d12Tools::hr(shared));
    }
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device11, texture.get());
    Com<ID3D11Texture2D> staging = D3d11Tools::texture(device11, D3d11Tools::desc2d(kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
    if (!view || !staging) {
        return Outcome::fail("creating the D3D11 views failed");
    }
    Problems problems;
    std::uint64_t value = 0;
    for (int round = 0; round < 24 && problems.empty(); ++round) {
        const std::uint32_t color11 = pack(10 + round * 9, 200 - round * 5, 30 + round * 7);
        const float clear[4] = {static_cast<float>(color11 & 255) / 255.0f, static_cast<float>((color11 >> 8) & 255) / 255.0f, static_cast<float>((color11 >> 16) & 255) / 255.0f, 1.0f};
        if (value != 0) {
            context_fence->Wait(fence11.get(), value);
        }
        device11.context->ClearRenderTargetView(view.get(), clear);
        context_fence->Signal(fence11.get(), ++value);
        device11.context->Flush();
        rig.direct().gpu_wait_fence(fence12.get(), value);
        TexelImage image;
        if (!D3d12Tools::read_texture(rig, texture12.get(), D3D12_RESOURCE_STATE_COMMON, 0, image, error)) {
            return Outcome::fail("round " + std::to_string(round) + ": " + error);
        }
        if (image.pixel(0, 0) != color11 || image.pixel(kSize - 1, kSize - 1) != color11) {
            problems.add("round " + std::to_string(round) + ": D3D12 read 0x" + Text::hex(image.pixel(0, 0), 8) + " after waiting for the D3D11 signal, expected 0x" + Text::hex(color11, 8));
            break;
        }
        rig.direct().queue()->Signal(fence12.get(), ++value);
        rig.direct().touch();
        const std::uint32_t color12 = pack(240 - round * 8, 15 + round * 4, 120 + round);
        std::vector<std::uint32_t> pixels(kSize * kSize, color12);
        if (!D3d12Tools::write_texture(rig, texture12.get(), D3D12_RESOURCE_STATE_COMMON, 0, pixels.data(), error)) {
            return Outcome::fail("round " + std::to_string(round) + ": " + error);
        }
        rig.direct().queue()->Signal(fence12.get(), ++value);
        rig.direct().touch();
        context_fence->Wait(fence11.get(), value);
        device11.context->CopyResource(staging.get(), texture.get());
        TexelImage back;
        if (!D3d11Tools::read_texture(device11, staging.get(), 0, back, error)) {
            return Outcome::fail("round " + std::to_string(round) + ": " + error);
        }
        if (back.pixel(0, 0) != color12 || back.pixel(kSize / 2, kSize / 2) != color12) {
            problems.add("round " + std::to_string(round) + ": D3D11 read 0x" + Text::hex(back.pixel(0, 0), 8) + " after waiting for the D3D12 signal, expected 0x" + Text::hex(color12, 8));
        }
    }
    if (!rig.idle(error)) {
        problems.add(error);
    }
    if (fence11->GetCompletedValue() != value) {
        problems.add("the shared fence completed value " + std::to_string(fence11->GetCompletedValue()) + " is not the last signalled value " + std::to_string(value));
    }
    return problems.outcome();
}

Outcome CrossChecks::vulkan_d3d12_semaphore(Context& context) {
    if (ApiPolicy::is_skipped("vulkan") || ApiPolicy::is_skipped("d3d12")) {
        return Context::skip("vulkan or d3d12 excluded by LSW_SUITES_SKIP_APIS");
    }
    std::string error;
    const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(error);
    if (adapters.empty()) {
        return Context::skip("Vulkan is unavailable: " + error);
    }
    VulkanRig vk;
    if (!vk.open(adapters.front(), VulkanInterop::options(), error)) {
        return Outcome::fail(error);
    }
    D3d12Rig rig;
    if (!rig.open_luid(vk.adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    const VkExternalSemaphoreFeatureFlags features = VulkanInterop::semaphore_features(vk, VulkanInterop::kD3d12Fence, true);
    context.measure("d3d12_fence_features", static_cast<int>(features));
    Com<ID3D12Fence> fence;
    const HRESULT made = rig.device()->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), fence.put_void());
    HANDLE handle = nullptr;
    if (SUCCEEDED(made)) {
        rig.device()->CreateSharedHandle(fence.get(), nullptr, GENERIC_ALL, nullptr, &handle);
    }
    VulkanOwned<VkSemaphore> semaphore;
    VkResult result = vk.timeline ? VulkanInterop::make_semaphore(vk, semaphore, true, 0) : VK_ERROR_FEATURE_NOT_PRESENT;
    if ((features & VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT) == 0 || !vk.timeline) {
        if (handle != nullptr && semaphore.valid()) {
            const VkResult imported = VulkanInterop::import_semaphore(vk, semaphore.get(), VulkanInterop::kD3d12Fence, handle);
            ::CloseHandle(handle);
            if (imported == VK_SUCCESS) {
                return Outcome::fail("importing a D3D12 fence succeeded although the capability query reports it not importable");
            }
            context.note("import of a D3D12 fence failed with " + VulkanTools::result_name(imported) + ", consistent with the absent capability");
        } else if (handle != nullptr) {
            ::CloseHandle(handle);
        }
        return Context::skip("D3D12 fence import into Vulkan is reported absent and importing it fails");
    }
    if (handle == nullptr || result != VK_SUCCESS) {
        return Outcome::fail("the capability is reported but creating the shared fence or the timeline semaphore failed");
    }
    result = VulkanInterop::import_semaphore(vk, semaphore.get(), VulkanInterop::kD3d12Fence, handle);
    ::CloseHandle(handle);
    if (result != VK_SUCCESS) {
        return Outcome::fail("the capability is reported but importing the D3D12 fence failed: " + VulkanTools::result_name(result));
    }
    VulkanBuffer buffer;
    VulkanCompute compute;
    result = buffer.create(vk, 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (result == VK_SUCCESS) {
        buffer.fill_value(0);
        buffer.publish();
        result = compute.create(vk, VulkanShaders::words(VulkanShader::Step), {VulkanBinding{}}, 16);
    }
    if (result != VK_SUCCESS) {
        return Outcome::fail("creating the Vulkan objects failed: " + VulkanTools::result_name(result));
    }
    compute.write_buffer(0, buffer.buffer);
    VulkanQueue& queue = vk.main();
    std::vector<VkCommandBuffer> issued;
    auto submit = [&](std::uint32_t op, std::uint32_t value, VkSemaphore wait, std::uint64_t wait_value, VkSemaphore signal, std::uint64_t signal_value) -> VkResult {
        VkCommandBufferAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = queue.pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        VkCommandBuffer commands = nullptr;
        VkResult code = vk.fn.vkAllocateCommandBuffers(vk.device, &allocate, &commands);
        if (code != VK_SUCCESS) {
            return code;
        }
        issued.push_back(commands);
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        code = vk.fn.vkBeginCommandBuffer(commands, &begin);
        if (code != VK_SUCCESS) {
            return code;
        }
        const std::uint32_t values[4] = {op, value, 0, 0};
        compute.bind(commands);
        compute.push(commands, values, sizeof(values));
        compute.dispatch(commands, 1);
        VkMemoryBarrier memory{};
        memory.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memory.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        memory.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vk.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &memory, 0, nullptr, 0, nullptr);
        code = vk.fn.vkEndCommandBuffer(commands);
        if (code != VK_SUCCESS) {
            return code;
        }
        VkTimelineSemaphoreSubmitInfo timeline{};
        timeline.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timeline.waitSemaphoreValueCount = wait != VK_NULL_HANDLE ? 1 : 0;
        timeline.pWaitSemaphoreValues = &wait_value;
        timeline.signalSemaphoreValueCount = signal != VK_NULL_HANDLE ? 1 : 0;
        timeline.pSignalSemaphoreValues = &signal_value;
        const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        VkSubmitInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.pNext = &timeline;
        info.waitSemaphoreCount = timeline.waitSemaphoreValueCount;
        info.pWaitSemaphores = &wait;
        info.pWaitDstStageMask = &stage;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &commands;
        info.signalSemaphoreCount = timeline.signalSemaphoreValueCount;
        info.pSignalSemaphores = &signal;
        return vk.submit(queue, 1, &info, VK_NULL_HANDLE);
    };
    for (int round = 0; round < 20; ++round) {
        const std::uint64_t base = static_cast<std::uint64_t>(round) * 3;
        const std::uint32_t seed = 7u + static_cast<std::uint32_t>(round);
        VkResult code = submit(1, 5, semaphore.get(), base + 2, semaphore.get(), base + 3);
        if (code == VK_SUCCESS) {
            code = submit(0, seed, VK_NULL_HANDLE, 0, semaphore.get(), base + 1);
        }
        if (code != VK_SUCCESS) {
            return Outcome::fail("round " + std::to_string(round) + ": submitting failed: " + VulkanTools::result_name(code));
        }
        rig.direct().gpu_wait_fence(fence.get(), base + 1);
        rig.direct().queue()->Signal(fence.get(), base + 2);
        rig.direct().touch();
        if (!reaches(fence.get(), base + 3, 30000)) {
            return Outcome::fail("round " + std::to_string(round) + ": the chain Vulkan, D3D12, Vulkan did not reach its last value; the shared fence stopped at " + std::to_string(fence->GetCompletedValue()));
        }
        if (vk.wait_idle() != VK_SUCCESS) {
            return Outcome::fail("vkDeviceWaitIdle failed");
        }
        for (VkCommandBuffer commands : issued) {
            vk.fn.vkFreeCommandBuffers(vk.device, queue.pool, 1, &commands);
        }
        issued.clear();
        buffer.acquire();
        if (buffer.words()[0] != seed + 5u) {
            return Outcome::fail("round " + std::to_string(round) + ": slot 0 holds " + std::to_string(buffer.words()[0]) + ", the fence-ordered result is " + std::to_string(seed + 5u));
        }
    }
    return rig.idle(error) ? Outcome::ok() : Outcome::fail(error);
}

Outcome CrossChecks::gl_d3d11_interop(Context& context) {
    const Outcome objects = interop::GlD3dChecks::d3d11_objects(context);
    if (objects.status() == Status::Fail) {
        return objects;
    }
    const Outcome ordering = interop::GlD3dChecks::lock_ordering(context);
    if (ordering.status() == Status::Fail) {
        return ordering;
    }
    return objects.status() == Status::Skip ? objects : ordering;
}

}  // namespace lsw::suites::sanity::sync
