#include "sanity/interop/vk_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/cl/kit.hpp"
#include "sanity/interop/kit.hpp"
#include "sanity/vk/kit.hpp"
#include "support/api_policy.hpp"
#include "support/child_runs.hpp"
#include "support/cl_interop.hpp"
#include "support/d3d11_device.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_rig.hpp"
#include "support/d3d12_tools.hpp"
#include "support/gl_rig.hpp"
#include "support/swap_rig.hpp"
#include "support/vulkan_interop.hpp"

#include <cstring>
#include <thread>

namespace lsw::suites::sanity::interop {

namespace {

namespace vkkit = lsw::suites::sanity::vk;
namespace clkit = lsw::suites::sanity::cl;

constexpr UINT kEdge = 64;
constexpr VkDeviceSize kBuffer = 64 * 1024;

template <typename Fence>
bool reached(Fence* fence, std::uint64_t value, DWORD milliseconds) {
    HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event == nullptr) {
        return false;
    }
    bool done = fence->GetCompletedValue() >= value;
    if (!done && SUCCEEDED(fence->SetEventOnCompletion(value, event))) {
        done = ::WaitForSingleObject(event, milliseconds) == WAIT_OBJECT_0;
    }
    ::CloseHandle(event);
    return done;
}

std::vector<std::uint8_t> inverted(std::vector<std::uint8_t> data) {
    for (std::uint8_t& byte : data) {
        byte = static_cast<std::uint8_t>(byte ^ 0xA5u);
    }
    return data;
}

VkBufferMemoryBarrier buffer_barrier(VkBuffer buffer, std::uint32_t from, std::uint32_t to, VkAccessFlags source, VkAccessFlags destination) {
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = source;
    barrier.dstAccessMask = destination;
    barrier.srcQueueFamilyIndex = from;
    barrier.dstQueueFamilyIndex = to;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    return barrier;
}

VkImageMemoryBarrier image_barrier(VkImage image, VkImageLayout from_layout, VkImageLayout to_layout, std::uint32_t from, std::uint32_t to, VkAccessFlags source, VkAccessFlags destination) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = source;
    barrier.dstAccessMask = destination;
    barrier.oldLayout = from_layout;
    barrier.newLayout = to_layout;
    barrier.srcQueueFamilyIndex = from;
    barrier.dstQueueFamilyIndex = to;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return barrier;
}

std::vector<std::uint8_t> rgba_frame(int frame, int width, int height) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint32_t c = FrameImage::at(frame, width, height, x, y);
            std::uint8_t* px = &out[Kit::at(x, y, width) * 4u];
            px[0] = static_cast<std::uint8_t>((c >> 16) & 255u);
            px[1] = static_cast<std::uint8_t>((c >> 8) & 255u);
            px[2] = static_cast<std::uint8_t>(c & 255u);
            px[3] = 255;
        }
    }
    return out;
}

std::vector<std::uint8_t> pattern_bytes(std::uint64_t seed, std::size_t count) {
    std::vector<std::uint8_t> out(count);
    Pattern::fill(seed, 0, out.data(), out.size());
    return out;
}

}  // namespace

Outcome VkChecks::import_d3d11_texture(Context& context) {
    if (const std::string why = Kit::missing({"vulkan", "d3d11"}); !why.empty()) {
        return Context::skip(why);
    }
    return vkkit::Kit::each_device(context, VulkanInterop::options(), [&](VulkanRig& rig, Problems& problems) -> std::string {
        std::string reason;
        if (!VulkanInterop::ready(rig, reason)) {
            return reason;
        }
        if (ApiPolicy::is_skipped("d3d11")) {
            return "api excluded by LSW_SUITES_SKIP_APIS";
        }
        VkResult code = VK_SUCCESS;
        const VkExternalMemoryFeatureFlags features = VulkanInterop::image_features(rig, VulkanInterop::kD3d11, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &code);
        if ((features & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) == 0) {
            return "D3D11_TEXTURE images are not importable on " + rig.describe() + " (features 0x" + Text::hex(features, 2) + ", query " + VulkanTools::result_name(code) + ")";
        }
        D3d11Device d3d;
        std::string error;
        if (!d3d.create_ex(rig.adapter.luid, nullptr, 0, 0, error)) {
            problems.add(error);
            return {};
        }
        D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(kEdge, kEdge, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        Com<ID3D11Texture2D> texture = D3d11Tools::texture(d3d, desc);
        Com<IDXGIResource1> resource = Kit::query<IDXGIResource1>(texture.get());
        HANDLE handle = nullptr;
        if (!texture || !resource || FAILED(resource->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle))) {
            problems.add("a D3D11 texture with an NT shared handle cannot be created");
            return {};
        }
        Com<ID3D11Device5> device5 = Kit::query<ID3D11Device5>(d3d.device);
        Com<ID3D11DeviceContext4> context4 = Kit::query<ID3D11DeviceContext4>(d3d.context);
        Com<ID3D11Fence> fence;
        VulkanOwned<VkSemaphore> semaphore;
        HANDLE fence_handle = nullptr;
        bool fenced = false;
        if (rig.timeline && rig.enabled_has(vkw::kExternalSemaphoreWin32) && device5 && context4 &&
            (VulkanInterop::semaphore_features(rig, VulkanInterop::kD3d12Fence, true) & VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT) != 0 &&
            SUCCEEDED(device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), fence.put_void())) && SUCCEEDED(fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &fence_handle))) {
            if (VulkanInterop::make_semaphore(rig, semaphore, true, 0) == VK_SUCCESS) {
                const VkResult imported = VulkanInterop::import_semaphore(rig, semaphore.get(), VulkanInterop::kD3d12Fence, fence_handle);
                fenced = imported == VK_SUCCESS;
                if (!fenced) {
                    problems.add("D3D12_FENCE timeline semaphores are importable according to the query but importing the D3D11 shared fence returned " + VulkanTools::result_name(imported));
                }
            }
        }
        context.measure("fence_ordering", fenced);
        if (fence_handle != nullptr) {
            ::CloseHandle(fence_handle);
        }
        VulkanBuffer host;
        if (!vkkit::Kit::succeeds(problems, "host buffer", vkkit::Kit::host_buffer(rig, host, kEdge * kEdge * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT))) {
            ::CloseHandle(handle);
            return {};
        }
        const std::uint32_t family = rig.main().family;
        for (int frame = 0; frame < 6 && problems.empty(); ++frame) {
            const std::vector<std::uint8_t> data = Kit::frame_bytes(Kit::seed(90), frame, kEdge, kEdge);
            d3d.context->UpdateSubresource(texture.get(), 0, nullptr, data.data(), kEdge * 4u, 0);
            std::vector<VulkanStep> waits;
            if (fenced) {
                if (FAILED(context4->Signal(fence.get(), static_cast<UINT64>(frame) + 1))) {
                    problems.add("ID3D11DeviceContext4::Signal failed");
                    break;
                }
                d3d.context->Flush();
                waits.push_back({semaphore.get(), static_cast<std::uint64_t>(frame) + 1});
            } else if (!D3d11Tools::wait_event(d3d, d3d.context, error)) {
                problems.add(error);
                break;
            }
            std::memset(host.mapped, 0xCD, kEdge * kEdge * 4);
            host.publish();
            VulkanSharedMemory imported;
            const VkResult import_result = imported.import_image(rig, VulkanInterop::kD3d11, handle, VK_FORMAT_R8G8B8A8_UNORM, kEdge, kEdge, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
            if (import_result != VK_SUCCESS) {
                problems.add("importing the D3D11 shared texture returned " + VulkanTools::result_name(import_result) + " although D3D11_TEXTURE images are reported importable");
                break;
            }
            const VkResult ran = VulkanInterop::run(
                rig,
                [&](VkCommandBuffer commands) {
                    const VkImageMemoryBarrier acquire = image_barrier(imported.image, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_QUEUE_FAMILY_EXTERNAL, family, 0,
                                                                       VK_ACCESS_TRANSFER_READ_BIT);
                    rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &acquire);
                    VkBufferImageCopy region{};
                    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    region.imageExtent = {kEdge, kEdge, 1};
                    rig.fn.vkCmdCopyImageToBuffer(commands, imported.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, host.buffer, 1, &region);
                    VkMemoryBarrier visible{};
                    visible.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                    visible.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    visible.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                    rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &visible, 0, nullptr, 0, nullptr);
                },
                waits, {});
            if (ran != VK_SUCCESS) {
                problems.add("frame " + std::to_string(frame) + ": the Vulkan copy of the imported texture returned " + VulkanTools::result_name(ran));
                break;
            }
            host.acquire();
            vkkit::Kit::same_bytes(context, problems, "d3d11_to_vulkan_frame_" + std::to_string(frame), data.data(), host.mapped, data.size());
            imported.destroy();
        }
        rig.wait_idle();
        ::CloseHandle(handle);
        static_cast<void>(D3d11Tools::wait_event(d3d, d3d.context, error));
        return {};
    });
}

Outcome VkChecks::import_d3d12_resource(Context& context) {
    if (const std::string why = Kit::missing({"vulkan", "d3d12"}); !why.empty()) {
        return Context::skip(why);
    }
    return vkkit::Kit::each_device(context, VulkanInterop::options(), [&](VulkanRig& rig, Problems& problems) -> std::string {
        std::string reason;
        if (!VulkanInterop::ready(rig, reason)) {
            return reason;
        }
        if (ApiPolicy::is_skipped("d3d12")) {
            return "api excluded by LSW_SUITES_SKIP_APIS";
        }
        const VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if ((VulkanInterop::buffer_features(rig, VulkanInterop::kD3d12Resource, usage) & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) == 0) {
            return "D3D12_RESOURCE buffers are not importable on " + rig.describe();
        }
        const bool fence_import = rig.timeline && rig.enabled_has(vkw::kExternalSemaphoreWin32) &&
                                  (VulkanInterop::semaphore_features(rig, VulkanInterop::kD3d12Fence, true) & VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT) != 0;
        if (!fence_import) {
            return "D3D12_FENCE timeline semaphores are not importable on " + rig.describe();
        }
        std::string error;
        D3d12Rig d3d12;
        if (!d3d12.open_luid(rig.adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
            problems.add(error);
            return {};
        }
        HRESULT made = S_OK;
        ID3D12Resource* resource = D3d12Tools::committed(d3d12, D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_SHARED, D3d12Tools::buffer_desc(kBuffer),
                                                         D3D12_RESOURCE_STATE_COMMON, nullptr, &made);
        Com<ID3D12Fence> fence_owner;
        HANDLE resource_handle = nullptr;
        HANDLE fence_handle = nullptr;
        if (resource == nullptr || FAILED(d3d12.device()->CreateSharedHandle(resource, nullptr, GENERIC_ALL, nullptr, &resource_handle)) ||
            FAILED(d3d12.device()->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), fence_owner.put_void())) ||
            FAILED(d3d12.device()->CreateSharedHandle(fence_owner.get(), nullptr, GENERIC_ALL, nullptr, &fence_handle))) {
            problems.add("a shared D3D12 buffer or fence cannot be created (" + Kit::hr(made) + ")");
            return {};
        }
        ID3D12Fence* fence = d3d12.hold(std::move(fence_owner));
        VulkanOwned<VkSemaphore> semaphore;
        VulkanSharedMemory imported;
        VkResult result = VulkanInterop::make_semaphore(rig, semaphore, true, 0);
        if (result == VK_SUCCESS) {
            result = VulkanInterop::import_semaphore(rig, semaphore.get(), VulkanInterop::kD3d12Fence, fence_handle);
        }
        ::CloseHandle(fence_handle);
        if (result != VK_SUCCESS) {
            problems.add("importing the D3D12 fence as a timeline semaphore returned " + VulkanTools::result_name(result));
            ::CloseHandle(resource_handle);
            return {};
        }
        result = imported.import_buffer(rig, VulkanInterop::kD3d12Resource, resource_handle, kBuffer, usage);
        ::CloseHandle(resource_handle);
        if (result != VK_SUCCESS) {
            problems.add("importing the D3D12 resource returned " + VulkanTools::result_name(result) + " although D3D12_RESOURCE buffers are reported importable");
            return {};
        }
        VulkanBuffer read_back;
        VulkanBuffer reply;
        if (!vkkit::Kit::succeeds(problems, "read back buffer", vkkit::Kit::host_buffer(rig, read_back, kBuffer, VK_BUFFER_USAGE_TRANSFER_DST_BIT)) ||
            !vkkit::Kit::succeeds(problems, "reply buffer", vkkit::Kit::host_buffer(rig, reply, kBuffer, VK_BUFFER_USAGE_TRANSFER_SRC_BIT))) {
            return {};
        }
        const std::uint32_t family = rig.main().family;
        for (int round = 0; round < 8 && problems.empty(); ++round) {
            const std::vector<std::uint8_t> data = pattern_bytes(Kit::seed(91, round), kBuffer);
            const std::vector<std::uint8_t> answer = inverted(data);
            if (!D3d12Tools::write_buffer(d3d12, resource, D3D12_RESOURCE_STATE_COMMON, 0, data.data(), data.size(), error)) {
                problems.add("round " + std::to_string(round) + ": " + error);
                break;
            }
            const std::uint64_t first = static_cast<std::uint64_t>(round) * 2 + 1;
            if (FAILED(d3d12.direct().queue()->Signal(fence, first))) {
                problems.add("ID3D12CommandQueue::Signal failed");
                break;
            }
            d3d12.direct().touch();
            std::memset(read_back.mapped, 0xCD, kBuffer);
            read_back.publish();
            std::memcpy(reply.mapped, answer.data(), answer.size());
            reply.publish();
            const VkResult ran = VulkanInterop::run(
                rig,
                [&](VkCommandBuffer commands) {
                    VkBufferMemoryBarrier acquire = buffer_barrier(imported.buffer, VK_QUEUE_FAMILY_EXTERNAL, family, 0, VK_ACCESS_TRANSFER_READ_BIT);
                    rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &acquire, 0, nullptr);
                    const VkBufferCopy whole{0, 0, kBuffer};
                    rig.fn.vkCmdCopyBuffer(commands, imported.buffer, read_back.buffer, 1, &whole);
                    VkBufferMemoryBarrier between = buffer_barrier(imported.buffer, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
                    rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &between, 0, nullptr);
                    rig.fn.vkCmdCopyBuffer(commands, reply.buffer, imported.buffer, 1, &whole);
                    VkBufferMemoryBarrier release = buffer_barrier(imported.buffer, family, VK_QUEUE_FAMILY_EXTERNAL, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
                    rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &release, 0, nullptr);
                },
                {{semaphore.get(), first}}, {{semaphore.get(), first + 1}});
            if (ran != VK_SUCCESS) {
                problems.add("round " + std::to_string(round) + ": the Vulkan submission waiting on the D3D12 fence returned " + VulkanTools::result_name(ran));
                break;
            }
            read_back.acquire();
            vkkit::Kit::same_bytes(context, problems, "d3d12_to_vulkan_round_" + std::to_string(round), data.data(), read_back.mapped, data.size());
            if (!reached(fence, first + 1, 20000)) {
                problems.add("round " + std::to_string(round) + ": the fence never reached " + std::to_string(first + 1) + " in D3D12 after Vulkan signalled it");
                break;
            }
            d3d12.direct().gpu_wait_fence(fence, first + 1);
            std::vector<std::uint8_t> seen;
            if (!D3d12Tools::read_buffer(d3d12, resource, D3D12_RESOURCE_STATE_COMMON, 0, kBuffer, seen, error)) {
                problems.add("round " + std::to_string(round) + ": " + error);
                break;
            }
            vkkit::Kit::same_bytes(context, problems, "vulkan_to_d3d12_round_" + std::to_string(round), answer.data(), seen.data(), answer.size());
        }
        rig.wait_idle();
        imported.destroy();
        semaphore.reset();
        static_cast<void>(d3d12.idle(error));
        d3d12.shutdown();
        return {};
    });
}

Outcome VkChecks::export_to_d3d12(Context& context) {
    if (const std::string why = Kit::missing({"vulkan", "d3d12"}); !why.empty()) {
        return Context::skip(why);
    }
    return vkkit::Kit::each_device(context, VulkanInterop::options(), [&](VulkanRig& rig, Problems& problems) -> std::string {
        std::string reason;
        if (!VulkanInterop::ready(rig, reason)) {
            return reason;
        }
        if (ApiPolicy::is_skipped("d3d12")) {
            return "api excluded by LSW_SUITES_SKIP_APIS";
        }
        const VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VkExternalMemoryHandleTypeFlagBits type = VulkanInterop::kD3d12Resource;
        if ((VulkanInterop::buffer_features(rig, type, usage) & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) == 0) {
            type = VulkanInterop::kD3d12Heap;
            if ((VulkanInterop::buffer_features(rig, type, usage) & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) == 0) {
                return "neither D3D12_RESOURCE nor D3D12_HEAP buffer memory is exportable on " + rig.describe();
            }
        }
        std::string error;
        D3d12Rig d3d12;
        if (!d3d12.open_luid(rig.adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
            problems.add(error);
            return {};
        }
        VulkanSharedMemory exported;
        VkResult result = exported.export_buffer(rig, type, kBuffer, usage);
        if (result != VK_SUCCESS) {
            problems.add("exporting buffer memory as a D3D12 handle returned " + VulkanTools::result_name(result) + " although it is reported exportable");
            return {};
        }
        VulkanBuffer host;
        VulkanBuffer back;
        if (!vkkit::Kit::succeeds(problems, "host buffer", vkkit::Kit::host_buffer(rig, host, kBuffer, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) ||
            !vkkit::Kit::succeeds(problems, "read back buffer", vkkit::Kit::host_buffer(rig, back, kBuffer, VK_BUFFER_USAGE_TRANSFER_DST_BIT))) {
            return {};
        }
        std::vector<std::uint8_t> data(kBuffer);
        Pattern::fill(Kit::seed(92), 0, data.data(), data.size());
        std::memcpy(host.mapped, data.data(), data.size());
        host.publish();
        const std::uint32_t family = rig.main().family;
        result = VulkanInterop::run(
            rig,
            [&](VkCommandBuffer commands) {
                const VkBufferCopy whole{0, 0, kBuffer};
                rig.fn.vkCmdCopyBuffer(commands, host.buffer, exported.buffer, 1, &whole);
                VkBufferMemoryBarrier release = buffer_barrier(exported.buffer, family, VK_QUEUE_FAMILY_EXTERNAL, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
                rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &release, 0, nullptr);
            },
            {}, {});
        rig.wait_idle();
        HANDLE handle = nullptr;
        if (result != VK_SUCCESS || exported.handle(type, handle) != VK_SUCCESS || handle == nullptr) {
            problems.add("writing the exported memory or getting its handle failed");
            return {};
        }
        ID3D12Resource* opened = nullptr;
        if (type == VulkanInterop::kD3d12Resource) {
            Com<ID3D12Resource> owner;
            const HRESULT opened_result = d3d12.device()->OpenSharedHandle(handle, __uuidof(ID3D12Resource), owner.put_void());
            if (FAILED(opened_result)) {
                problems.add("ID3D12Device::OpenSharedHandle of the Vulkan-exported D3D12_RESOURCE memory returned " + Kit::hr(opened_result));
            } else {
                opened = d3d12.hold(std::move(owner));
            }
        } else {
            Com<ID3D12Heap> heap_owner;
            Com<ID3D12Resource> owner;
            const D3D12_RESOURCE_DESC desc = D3d12Tools::buffer_desc(kBuffer);
            const HRESULT opened_result = d3d12.device()->OpenSharedHandle(handle, __uuidof(ID3D12Heap), heap_owner.put_void());
            if (FAILED(opened_result)) {
                problems.add("ID3D12Device::OpenSharedHandle of the Vulkan-exported D3D12_HEAP memory returned " + Kit::hr(opened_result));
            } else {
                ID3D12Heap* heap = d3d12.hold(std::move(heap_owner));
                const HRESULT placed = d3d12.device()->CreatePlacedResource(heap, 0, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), owner.put_void());
                if (FAILED(placed)) {
                    problems.add("CreatePlacedResource over the exported heap returned " + Kit::hr(placed));
                } else {
                    opened = d3d12.hold(std::move(owner));
                }
            }
        }
        ::CloseHandle(handle);
        if (opened != nullptr) {
            const D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
            std::vector<std::uint8_t> seen;
            if (!D3d12Tools::read_buffer(d3d12, opened, state, 0, kBuffer, seen, error)) {
                problems.add(error);
            } else {
                vkkit::Kit::same_bytes(context, problems, "vulkan_to_d3d12", data.data(), seen.data(), data.size());
            }
            const std::vector<std::uint8_t> answer = inverted(data);
            if (D3d12Tools::write_buffer(d3d12, opened, state, 0, answer.data(), answer.size(), error)) {
                std::memset(back.mapped, 0xCD, kBuffer);
                back.publish();
                result = VulkanInterop::run(
                    rig,
                    [&](VkCommandBuffer commands) {
                        VkBufferMemoryBarrier acquire = buffer_barrier(exported.buffer, VK_QUEUE_FAMILY_EXTERNAL, family, 0, VK_ACCESS_TRANSFER_READ_BIT);
                        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &acquire, 0, nullptr);
                        const VkBufferCopy whole{0, 0, kBuffer};
                        rig.fn.vkCmdCopyBuffer(commands, exported.buffer, back.buffer, 1, &whole);
                    },
                    {}, {});
                back.acquire();
                if (result == VK_SUCCESS) {
                    vkkit::Kit::same_bytes(context, problems, "d3d12_to_vulkan", answer.data(), back.mapped, answer.size());
                } else {
                    problems.add("reading the exported memory back in Vulkan returned " + VulkanTools::result_name(result));
                }
            } else {
                problems.add("D3D12 cannot write the opened resource: " + error);
            }
        }
        static_cast<void>(d3d12.idle(error));
        d3d12.shutdown();
        rig.wait_idle();
        return {};
    });
}

Outcome VkChecks::export_to_gl(Context& context) {
    if (const std::string why = Kit::missing({"vulkan", "gl"}); !why.empty()) {
        return Context::skip(why);
    }
    GlRig gl;
    std::string error;
    GlRigOptions gl_options;
    gl_options.visible = false;
    if (!gl.open(gl_options, error)) {
        return Outcome::fail(error);
    }
    gl.record(context);
    const GlApi& api = gl.gl;
    const bool memory_ext = api.has_extension("GL_EXT_memory_object") && api.has_extension("GL_EXT_memory_object_win32");
    const bool semaphore_ext = api.has_extension("GL_EXT_semaphore") && api.has_extension("GL_EXT_semaphore_win32");
    if (!memory_ext || !semaphore_ext) {
        return Context::skip("GL_EXT_memory_object_win32 and GL_EXT_semaphore_win32 are not both advertised by the context");
    }
    if (api.glCreateMemoryObjectsEXT == nullptr || api.glImportMemoryWin32HandleEXT == nullptr || api.glBufferStorageMemEXT == nullptr || api.glGenSemaphoresEXT == nullptr ||
        api.glImportSemaphoreWin32HandleEXT == nullptr || api.glWaitSemaphoreEXT == nullptr || api.glSignalSemaphoreEXT == nullptr) {
        return Outcome::fail("the memory object and semaphore extensions are advertised but an entry point does not resolve");
    }
    return vkkit::Kit::each_device(context, VulkanInterop::options(), [&](VulkanRig& rig, Problems& problems) -> std::string {
        std::string reason;
        if (!VulkanInterop::ready(rig, reason)) {
            return reason;
        }
        if (rig.adapter.luid_key() != gl.adapter.luid_key()) {
            return "this Vulkan device is not on the OpenGL context's adapter";
        }
        const VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if ((VulkanInterop::buffer_features(rig, VulkanInterop::kOpaque, usage) & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) == 0 ||
            (VulkanInterop::semaphore_features(rig, VulkanInterop::kOpaqueSemaphore, false) & VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT) == 0) {
            return "OPAQUE_WIN32 buffer memory or binary semaphores are not exportable on " + rig.describe();
        }
        VulkanSharedMemory exported;
        VulkanOwned<VkSemaphore> to_gl;
        VulkanOwned<VkSemaphore> from_gl;
        HANDLE memory_handle = nullptr;
        HANDLE to_gl_handle = nullptr;
        HANDLE from_gl_handle = nullptr;
        VkResult result = exported.export_buffer(rig, VulkanInterop::kOpaque, kBuffer, usage);
        std::string step = result == VK_SUCCESS ? std::string() : "creating the exportable buffer memory returned " + VulkanTools::result_name(result);
        if (result == VK_SUCCESS) {
            result = VulkanInterop::make_semaphore(rig, to_gl, false, VulkanInterop::kOpaqueSemaphore);
            step = result == VK_SUCCESS ? step : "creating the first exportable semaphore returned " + VulkanTools::result_name(result);
        }
        if (result == VK_SUCCESS) {
            result = VulkanInterop::make_semaphore(rig, from_gl, false, VulkanInterop::kOpaqueSemaphore);
            step = result == VK_SUCCESS ? step : "creating the second exportable semaphore returned " + VulkanTools::result_name(result);
        }
        VkResult exported_memory = VK_SUCCESS;
        VkResult exported_first = VK_SUCCESS;
        VkResult exported_second = VK_SUCCESS;
        if (result == VK_SUCCESS) {
            exported_memory = exported.handle(VulkanInterop::kOpaque, memory_handle);
            exported_first = VulkanInterop::semaphore_handle(rig, to_gl.get(), VulkanInterop::kOpaqueSemaphore, to_gl_handle);
            exported_second = VulkanInterop::semaphore_handle(rig, from_gl.get(), VulkanInterop::kOpaqueSemaphore, from_gl_handle);
            if (exported_memory != VK_SUCCESS) {
                step = "vkGetMemoryWin32HandleKHR returned " + VulkanTools::result_name(exported_memory);
            } else if (exported_first != VK_SUCCESS) {
                step = "vkGetSemaphoreWin32HandleKHR for the first semaphore returned " + VulkanTools::result_name(exported_first);
            } else if (exported_second != VK_SUCCESS) {
                step = "vkGetSemaphoreWin32HandleKHR for the second semaphore returned " + VulkanTools::result_name(exported_second);
            }
        }
        if (!step.empty()) {
            problems.add("the exportable memory or semaphores cannot be created or exported: " + step);
            return {};
        }
        (void)api.drain_errors();
        unsigned memory_object = 0;
        unsigned semaphores[2] = {0, 0};
        unsigned shared = 0;
        unsigned staging = 0;
        api.glCreateMemoryObjectsEXT(1, &memory_object);
        const int dedicated = 1;
        api.glMemoryObjectParameterivEXT(memory_object, glc::DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
        api.glImportMemoryWin32HandleEXT(memory_object, exported.allocation, glc::HANDLE_TYPE_OPAQUE_WIN32_EXT, memory_handle);
        api.glGenBuffers(1, &shared);
        api.glBindBuffer(glc::ARRAY_BUFFER, shared);
        api.glBufferStorageMemEXT(glc::ARRAY_BUFFER, static_cast<std::ptrdiff_t>(kBuffer), memory_object, 0);
        api.glGenSemaphoresEXT(2, semaphores);
        api.glImportSemaphoreWin32HandleEXT(semaphores[0], glc::HANDLE_TYPE_OPAQUE_WIN32_EXT, to_gl_handle);
        api.glImportSemaphoreWin32HandleEXT(semaphores[1], glc::HANDLE_TYPE_OPAQUE_WIN32_EXT, from_gl_handle);
        api.glGenBuffers(1, &staging);
        const unsigned setup = api.drain_errors();
        ::CloseHandle(memory_handle);
        ::CloseHandle(to_gl_handle);
        ::CloseHandle(from_gl_handle);
        if (setup != 0) {
            problems.add("importing the Vulkan memory and semaphores into GL gave " + GlApi::error_name(setup) + " although both APIs advertise OPAQUE_WIN32");
        } else {
            VulkanBuffer host;
            VulkanBuffer back;
            if (vkkit::Kit::succeeds(problems, "host buffer", vkkit::Kit::host_buffer(rig, host, kBuffer, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) &&
                vkkit::Kit::succeeds(problems, "read back buffer", vkkit::Kit::host_buffer(rig, back, kBuffer, VK_BUFFER_USAGE_TRANSFER_DST_BIT))) {
                const std::uint32_t family = rig.main().family;
                for (int round = 0; round < 4 && problems.empty(); ++round) {
                    std::vector<std::uint8_t> data(kBuffer);
                    Pattern::fill(Kit::seed(93, round), 0, data.data(), data.size());
                    std::memcpy(host.mapped, data.data(), data.size());
                    host.publish();
                    result = VulkanInterop::run(
                        rig,
                        [&](VkCommandBuffer commands) {
                            const VkBufferCopy whole{0, 0, kBuffer};
                            rig.fn.vkCmdCopyBuffer(commands, host.buffer, exported.buffer, 1, &whole);
                            VkBufferMemoryBarrier release = buffer_barrier(exported.buffer, family, VK_QUEUE_FAMILY_EXTERNAL, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
                            rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &release, 0, nullptr);
                        },
                        {}, {{to_gl.get(), 0}});
                    if (result != VK_SUCCESS) {
                        problems.add("round " + std::to_string(round) + ": Vulkan writing the exported memory returned " + VulkanTools::result_name(result));
                        break;
                    }
                    (void)api.drain_errors();
                    api.glWaitSemaphoreEXT(semaphores[0], 1, &shared, 0, nullptr, nullptr);
                    std::vector<std::uint8_t> seen(kBuffer);
                    api.glBindBuffer(glc::ARRAY_BUFFER, shared);
                    api.glGetBufferSubData(glc::ARRAY_BUFFER, 0, static_cast<std::ptrdiff_t>(kBuffer), seen.data());
                    vkkit::Kit::same_bytes(context, problems, "vulkan_to_gl_round_" + std::to_string(round), data.data(), seen.data(), data.size());
                    const std::vector<std::uint8_t> answer = inverted(data);
                    api.glBindBuffer(glc::COPY_READ_BUFFER, staging);
                    api.glBufferData(glc::COPY_READ_BUFFER, static_cast<std::ptrdiff_t>(kBuffer), answer.data(), glc::STATIC_DRAW);
                    api.glBindBuffer(glc::COPY_WRITE_BUFFER, shared);
                    api.glCopyBufferSubData(glc::COPY_READ_BUFFER, glc::COPY_WRITE_BUFFER, 0, 0, static_cast<std::ptrdiff_t>(kBuffer));
                    api.glSignalSemaphoreEXT(semaphores[1], 1, &shared, 0, nullptr, nullptr);
                    api.glFlush();
                    api.glFinish();
                    const unsigned gl_error = api.drain_errors();
                    if (gl_error != 0) {
                        problems.add("round " + std::to_string(round) + ": the GL semaphore hand-off gave " + GlApi::error_name(gl_error));
                        break;
                    }
                    std::memset(back.mapped, 0xCD, kBuffer);
                    back.publish();
                    result = VulkanInterop::run(
                        rig,
                        [&](VkCommandBuffer commands) {
                            VkBufferMemoryBarrier acquire = buffer_barrier(exported.buffer, VK_QUEUE_FAMILY_EXTERNAL, family, 0, VK_ACCESS_TRANSFER_READ_BIT);
                            rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &acquire, 0, nullptr);
                            const VkBufferCopy whole{0, 0, kBuffer};
                            rig.fn.vkCmdCopyBuffer(commands, exported.buffer, back.buffer, 1, &whole);
                        },
                        {{from_gl.get(), 0}}, {});
                    back.acquire();
                    if (result != VK_SUCCESS) {
                        problems.add("round " + std::to_string(round) + ": Vulkan waiting on the GL signal returned " + VulkanTools::result_name(result));
                        break;
                    }
                    vkkit::Kit::same_bytes(context, problems, "gl_to_vulkan_round_" + std::to_string(round), answer.data(), back.mapped, answer.size());
                }
            }
        }
        api.glFinish();
        api.glBindBuffer(glc::ARRAY_BUFFER, 0);
        api.glDeleteBuffers(1, &shared);
        api.glDeleteBuffers(1, &staging);
        api.glDeleteSemaphoresEXT(2, semaphores);
        api.glDeleteMemoryObjectsEXT(1, &memory_object);
        api.glFinish();
        rig.wait_idle();
        return {};
    });
}

Outcome VkChecks::opencl_external_memory_caps(Context& context) {
    if (const std::string why = Kit::missing({"vulkan", "opencl"}); !why.empty()) {
        return Context::skip(why);
    }
    return vkkit::Kit::each_device(context, VulkanInterop::options(), [&](VulkanRig& rig, Problems& problems) -> std::string {
        std::string reason;
        if (!VulkanInterop::ready(rig, reason)) {
            return reason;
        }
        if (ApiPolicy::is_skipped("opencl")) {
            return "api excluded by LSW_SUITES_SKIP_APIS";
        }
        ClApi api;
        ClDeviceList list;
        std::string error;
        if (!ClRig::list_devices(api, list, error)) {
            problems.add("OpenCL: " + error);
            return {};
        }
        const ClDeviceRef* pick = nullptr;
        for (const ClDeviceRef& ref : list.refs) {
            if (ref.adapter.luid_key() == rig.adapter.luid_key()) {
                pick = &ref;
            }
        }
        if (pick == nullptr) {
            return "no OpenCL device on the adapter of this Vulkan device";
        }
        ClRig cl;
        if (!cl.open_ref(*pick, ClRigOptions{}, error)) {
            problems.add(error);
            return {};
        }
        const bool advertised = cl.has_extension("cl_khr_external_memory");
        std::vector<cl_external_memory_handle_type_khr> types;
        std::size_t bytes = 0;
        const cl_int query = cl.api.GetDeviceInfo(cl.device, CL_DEVICE_EXTERNAL_MEMORY_IMPORT_HANDLE_TYPES_KHR, 0, nullptr, &bytes);
        if (query == CL_SUCCESS && bytes != 0) {
            types.resize(bytes / sizeof(cl_external_memory_handle_type_khr));
            cl.api.GetDeviceInfo(cl.device, CL_DEVICE_EXTERNAL_MEMORY_IMPORT_HANDLE_TYPES_KHR, bytes, types.data(), nullptr);
        }
        context.measure("cl_khr_external_memory", advertised);
        context.measure("import_handle_types", static_cast<long long>(types.size()));
        using CreateWithProperties = decltype(&clCreateBufferWithProperties);
        auto create = reinterpret_cast<CreateWithProperties>(ClInterop::find(cl, "clCreateBufferWithProperties"));
        auto acquire = reinterpret_cast<clEnqueueAcquireExternalMemObjectsKHR_fn>(ClInterop::find(cl, "clEnqueueAcquireExternalMemObjectsKHR"));
        auto release = reinterpret_cast<clEnqueueReleaseExternalMemObjectsKHR_fn>(ClInterop::find(cl, "clEnqueueReleaseExternalMemObjectsKHR"));
        const bool opaque = std::find(types.begin(), types.end(), static_cast<cl_external_memory_handle_type_khr>(CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32_KHR)) != types.end();
        if (!advertised) {
            if (query == CL_SUCCESS && bytes != 0) {
                problems.add("CL_DEVICE_EXTERNAL_MEMORY_IMPORT_HANDLE_TYPES_KHR lists handle types although cl_khr_external_memory is not advertised");
            }
            if (create != nullptr) {
                cl_int code = CL_SUCCESS;
                HANDLE bogus = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
                const cl_mem_properties props[] = {CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32_KHR, reinterpret_cast<cl_mem_properties>(bogus), 0};
                cl_mem made = create(cl.context, props, CL_MEM_READ_WRITE, 4096, nullptr, &code);
                if (made != nullptr) {
                    cl.api.ReleaseMemObject(made);
                    problems.add("clCreateBufferWithProperties accepts an external memory handle although cl_khr_external_memory is not advertised");
                }
                if (bogus != nullptr) {
                    ::CloseHandle(bogus);
                }
            }
            return {};
        }
        if (!opaque || create == nullptr || acquire == nullptr || release == nullptr) {
            context.note("cl_khr_external_memory is advertised without OPAQUE_WIN32 import (or without the entry points), so no Vulkan memory is imported");
            return {};
        }
        if ((VulkanInterop::buffer_features(rig, VulkanInterop::kOpaque, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT) & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) == 0) {
            return "OPAQUE_WIN32 buffer memory is not exportable by Vulkan";
        }
        VulkanSharedMemory exported;
        const VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VulkanBuffer host;
        VulkanBuffer back;
        HANDLE handle = nullptr;
        if (exported.export_buffer(rig, VulkanInterop::kOpaque, kBuffer, usage) != VK_SUCCESS || exported.handle(VulkanInterop::kOpaque, handle) != VK_SUCCESS ||
            !vkkit::Kit::succeeds(problems, "host buffer", vkkit::Kit::host_buffer(rig, host, kBuffer, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) ||
            !vkkit::Kit::succeeds(problems, "read back buffer", vkkit::Kit::host_buffer(rig, back, kBuffer, VK_BUFFER_USAGE_TRANSFER_DST_BIT))) {
            problems.add("the exportable Vulkan memory cannot be created");
            return {};
        }
        std::vector<std::uint8_t> data(kBuffer);
        Pattern::fill(Kit::seed(94), 0, data.data(), data.size());
        std::memcpy(host.mapped, data.data(), data.size());
        host.publish();
        const std::uint32_t family = rig.main().family;
        VkResult ran = VulkanInterop::run(
            rig,
            [&](VkCommandBuffer commands) {
                const VkBufferCopy whole{0, 0, kBuffer};
                rig.fn.vkCmdCopyBuffer(commands, host.buffer, exported.buffer, 1, &whole);
                VkBufferMemoryBarrier release_barrier = buffer_barrier(exported.buffer, family, VK_QUEUE_FAMILY_EXTERNAL, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
                rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &release_barrier, 0, nullptr);
            },
            {}, {});
        rig.wait_idle();
        cl_int code = CL_SUCCESS;
        const cl_mem_properties props[] = {CL_MEM_DEVICE_HANDLE_LIST_KHR, reinterpret_cast<cl_mem_properties>(cl.device), CL_MEM_DEVICE_HANDLE_LIST_END_KHR,
                                           CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32_KHR, reinterpret_cast<cl_mem_properties>(handle), 0};
        ClOwned<cl_mem> imported;
        imported.adopt(cl, ran == VK_SUCCESS ? create(cl.context, props, CL_MEM_READ_WRITE, kBuffer, nullptr, &code) : nullptr);
        ::CloseHandle(handle);
        if (!imported) {
            problems.add("OpenCL lists OPAQUE_WIN32 as an import handle type but clCreateBufferWithProperties on memory exported by Vulkan returned " + ClTools::error_name(code));
            return {};
        }
        cl_mem mem = imported.get();
        code = acquire(cl.queue, 1, &mem, 0, nullptr, nullptr);
        std::vector<std::uint8_t> seen(kBuffer);
        if (code != CL_SUCCESS) {
            problems.add("clEnqueueAcquireExternalMemObjectsKHR returned " + ClTools::error_name(code));
        } else {
            code = cl.api.EnqueueReadBuffer(cl.queue, imported.get(), CL_TRUE, 0, kBuffer, seen.data(), 0, nullptr, nullptr);
            if (code != CL_SUCCESS) {
                problems.add("reading the imported buffer returned " + ClTools::error_name(code));
            } else {
                clkit::Kit::same_bytes(context, problems, "vulkan_to_opencl", data.data(), seen.data(), data.size());
            }
            const std::vector<std::uint8_t> answer = inverted(data);
            code = cl.api.EnqueueWriteBuffer(cl.queue, imported.get(), CL_TRUE, 0, kBuffer, answer.data(), 0, nullptr, nullptr);
            const cl_int released = release(cl.queue, 1, &mem, 0, nullptr, nullptr);
            cl.api.Finish(cl.queue);
            if (code != CL_SUCCESS || released != CL_SUCCESS) {
                problems.add("the OpenCL write or release returned " + ClTools::error_name(code) + " / " + ClTools::error_name(released));
            } else {
                std::memset(back.mapped, 0xCD, kBuffer);
                back.publish();
                ran = VulkanInterop::run(
                    rig,
                    [&](VkCommandBuffer commands) {
                        VkBufferMemoryBarrier acquire_barrier = buffer_barrier(exported.buffer, VK_QUEUE_FAMILY_EXTERNAL, family, 0, VK_ACCESS_TRANSFER_READ_BIT);
                        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &acquire_barrier, 0, nullptr);
                        const VkBufferCopy whole{0, 0, kBuffer};
                        rig.fn.vkCmdCopyBuffer(commands, exported.buffer, back.buffer, 1, &whole);
                    },
                    {}, {});
                back.acquire();
                if (ran != VK_SUCCESS) {
                    problems.add("reading the memory back in Vulkan returned " + VulkanTools::result_name(ran));
                } else {
                    vkkit::Kit::same_bytes(context, problems, "opencl_to_vulkan", answer.data(), back.mapped, answer.size());
                }
            }
        }
        imported.reset();
        cl.drain();
        rig.wait_idle();
        return {};
    });
}

Outcome VkChecks::kmt_handles_caps(Context& context) {
    const std::string role = ChildMode::variable(L"LSW_SUITES_KMT_ROLE");
    if (ChildMode::active() && !role.empty()) {
        const std::string handle_text = ChildMode::variable(L"LSW_SUITES_KMT_HANDLE");
        const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(handle_text)));
        AvAdapter adapter;
        std::string error;
        if (!TargetAdapter::pick(adapter, error)) {
            return Outcome::fail(error);
        }
        const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(error);
        VulkanRig rig;
        bool opened = false;
        for (const AvAdapter& candidate : adapters) {
            if (candidate.luid_key() == adapter.luid_key()) {
                opened = rig.open(candidate, VulkanInterop::options(), error);
            }
        }
        std::string reason;
        if (!opened || !VulkanInterop::ready(rig, reason)) {
            return Outcome::fail("the consuming process cannot open Vulkan: " + error + reason);
        }
        VulkanBuffer host;
        Problems host_problems;
        if (!vkkit::Kit::succeeds(host_problems, "host", vkkit::Kit::host_buffer(rig, host, role == "memory" ? kBuffer : kEdge * kEdge * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT))) {
            return Outcome::fail("the host buffer cannot be created: " + host_problems.text());
        }
        VulkanSharedMemory imported;
        std::vector<std::uint8_t> want;
        VkResult result = VK_SUCCESS;
        if (role == "memory") {
            want.resize(kBuffer);
            Pattern::fill(Kit::seed(95), 0, want.data(), want.size());
            result = imported.import_buffer(rig, VulkanInterop::kOpaqueKmt, handle, kBuffer, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            if (result == VK_SUCCESS) {
                const std::uint32_t family = rig.main().family;
                result = VulkanInterop::run(
                    rig,
                    [&](VkCommandBuffer commands) {
                        VkBufferMemoryBarrier acquire = buffer_barrier(imported.buffer, VK_QUEUE_FAMILY_EXTERNAL, family, 0, VK_ACCESS_TRANSFER_READ_BIT);
                        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &acquire, 0, nullptr);
                        const VkBufferCopy whole{0, 0, kBuffer};
                        rig.fn.vkCmdCopyBuffer(commands, imported.buffer, host.buffer, 1, &whole);
                    },
                    {}, {});
            }
        } else {
            want = Kit::frame_bytes(Kit::seed(96), 0, kEdge, kEdge);
            result = imported.import_image(rig, VulkanInterop::kD3d11Kmt, handle, VK_FORMAT_R8G8B8A8_UNORM, kEdge, kEdge, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
            if (result == VK_SUCCESS) {
                const std::uint32_t family = rig.main().family;
                result = VulkanInterop::run(
                    rig,
                    [&](VkCommandBuffer commands) {
                        const VkImageMemoryBarrier acquire = image_barrier(imported.image, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_QUEUE_FAMILY_EXTERNAL, family, 0,
                                                                           VK_ACCESS_TRANSFER_READ_BIT);
                        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &acquire);
                        VkBufferImageCopy region{};
                        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        region.imageExtent = {kEdge, kEdge, 1};
                        rig.fn.vkCmdCopyImageToBuffer(commands, imported.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, host.buffer, 1, &region);
                    },
                    {}, {});
            }
        }
        rig.wait_idle();
        host.acquire();
        context.measure("role_done", true);
        if (result != VK_SUCCESS) {
            return Outcome::fail("importing or reading the KMT handle in the second process returned " + VulkanTools::result_name(result));
        }
        if (std::memcmp(host.mapped, want.data(), want.size()) != 0) {
            return Outcome::fail("the second process reads different data through the KMT handle");
        }
        return Outcome::ok();
    }
    if (ApiPolicy::is_skipped("vulkan")) {
        return Context::skip("api excluded by LSW_SUITES_SKIP_APIS");
    }
    return vkkit::Kit::each_device(context, VulkanInterop::options(), [&](VulkanRig& rig, Problems& problems) -> std::string {
        std::string reason;
        if (!VulkanInterop::ready(rig, reason)) {
            return reason;
        }
        const VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        const VkExternalMemoryFeatureFlags buffer_kmt = VulkanInterop::buffer_features(rig, VulkanInterop::kOpaqueKmt, usage);
        VkResult image_code = VK_SUCCESS;
        const VkExternalMemoryFeatureFlags image_kmt = VulkanInterop::image_features(rig, VulkanInterop::kD3d11Kmt, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &image_code);
        context.measure("opaque_win32_kmt_buffer_features", static_cast<long long>(buffer_kmt));
        context.measure("d3d11_texture_kmt_image_features", static_cast<long long>(image_kmt));
        std::string error;
        if (buffer_kmt == 0) {
            VkResult other = VK_SUCCESS;
            if (VulkanInterop::image_features(rig, VulkanInterop::kOpaqueKmt, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &other) != 0) {
                problems.add("OPAQUE_WIN32_KMT is unsupported for buffers but reported for images");
            }
        } else if ((buffer_kmt & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) != 0 && (buffer_kmt & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0) {
            VulkanSharedMemory exported;
            VulkanBuffer host;
            HANDLE handle = nullptr;
            VkResult result = exported.export_buffer(rig, VulkanInterop::kOpaqueKmt, kBuffer, usage);
            if (result != VK_SUCCESS || exported.handle(VulkanInterop::kOpaqueKmt, handle) != VK_SUCCESS || handle == nullptr ||
                !vkkit::Kit::succeeds(problems, "host buffer", vkkit::Kit::host_buffer(rig, host, kBuffer, VK_BUFFER_USAGE_TRANSFER_SRC_BIT))) {
                problems.add("exporting buffer memory as OPAQUE_WIN32_KMT returned " + VulkanTools::result_name(result) + " although it is reported exportable");
            } else {
                std::vector<std::uint8_t> data(kBuffer);
                Pattern::fill(Kit::seed(95), 0, data.data(), data.size());
                std::memcpy(host.mapped, data.data(), data.size());
                host.publish();
                const std::uint32_t family = rig.main().family;
                result = VulkanInterop::run(
                    rig,
                    [&](VkCommandBuffer commands) {
                        const VkBufferCopy whole{0, 0, kBuffer};
                        rig.fn.vkCmdCopyBuffer(commands, host.buffer, exported.buffer, 1, &whole);
                        VkBufferMemoryBarrier release = buffer_barrier(exported.buffer, family, VK_QUEUE_FAMILY_EXTERNAL, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
                        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &release, 0, nullptr);
                    },
                    {}, {});
                rig.wait_idle();
                ChildRunOptions options;
                options.timeout_seconds = 90;
                options.env = {{"LSW_SUITES_KMT_ROLE", "memory"}, {"LSW_SUITES_KMT_HANDLE", Kit::handle_text(handle)}};
                const CheckResult child = ChildRuns::run_self("sanity.interop.vk.kmt_handles_caps", options);
                if (result != VK_SUCCESS) {
                    problems.add("writing the KMT-exported memory returned " + VulkanTools::result_name(result));
                } else if (child.status != Status::Ok) {
                    problems.add("the second process cannot use the OPAQUE_WIN32_KMT memory: " + child.reason);
                }
            }
        }
        if (image_kmt == 0) {
            if (image_code != VK_ERROR_FORMAT_NOT_SUPPORTED && image_code != VK_SUCCESS) {
                problems.add("the image format query for D3D11_TEXTURE_KMT returned " + VulkanTools::result_name(image_code) + ", expected VK_ERROR_FORMAT_NOT_SUPPORTED when unsupported");
            }
            if (image_code == VK_SUCCESS) {
                problems.add("the image format query for D3D11_TEXTURE_KMT succeeds with no external features");
            }
        } else if ((image_kmt & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0 && !ApiPolicy::is_skipped("d3d11")) {
            D3d11Device d3d;
            if (!d3d.create_ex(rig.adapter.luid, nullptr, 0, 0, error)) {
                problems.add(error);
            } else {
                D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(kEdge, kEdge, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
                desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
                Com<ID3D11Texture2D> texture = D3d11Tools::texture(d3d, desc);
                Com<IDXGIResource> resource = Kit::query<IDXGIResource>(texture.get());
                HANDLE handle = nullptr;
                if (!texture || !resource || FAILED(resource->GetSharedHandle(&handle)) || handle == nullptr) {
                    problems.add("a legacy shared D3D11 texture cannot be created");
                } else {
                    const std::vector<std::uint8_t> data = Kit::frame_bytes(Kit::seed(96), 0, kEdge, kEdge);
                    d3d.context->UpdateSubresource(texture.get(), 0, nullptr, data.data(), kEdge * 4u, 0);
                    d3d.context->Flush();
                    ChildRunOptions options;
                    options.timeout_seconds = 90;
                    options.env = {{"LSW_SUITES_KMT_ROLE", "texture"}, {"LSW_SUITES_KMT_HANDLE", Kit::handle_text(handle)}};
                    if (D3d11Tools::wait_event(d3d, d3d.context, error)) {
                        const CheckResult child = ChildRuns::run_self("sanity.interop.vk.kmt_handles_caps", options);
                        if (child.status != Status::Ok) {
                            problems.add("the second process cannot import the legacy shared D3D11 texture through D3D11_TEXTURE_KMT: " + child.reason);
                        }
                    }
                }
                static_cast<void>(D3d11Tools::wait_event(d3d, d3d.context, error));
            }
        }
        return {};
    });
}

Outcome VkChecks::cross_process_present_chain(Context& context) {
    const std::string role = ChildMode::variable(L"LSW_SUITES_CHAIN_ROLE");
    constexpr int kSide = 128;
    constexpr int kFrames = 6;
    if (ChildMode::active() && role == "vulkan") {
        std::string error;
        AvAdapter adapter;
        if (!TargetAdapter::pick(adapter, error)) {
            return Outcome::fail(error);
        }
        const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(error);
        VulkanRig rig;
        bool opened = false;
        for (const AvAdapter& candidate : adapters) {
            if (candidate.luid_key() == adapter.luid_key()) {
                opened = rig.open(candidate, VulkanInterop::options(), error);
            }
        }
        std::string reason;
        if (!opened || !VulkanInterop::ready(rig, reason)) {
            return Outcome::fail("the Vulkan process cannot open Vulkan: " + error + reason);
        }
        HANDLE handle = Kit::adopt_handle(ChildMode::variable(L"LSW_SUITES_CHAIN_PID"), ChildMode::variable(L"LSW_SUITES_CHAIN_TEXTURE"));
        HANDLE ready = ::OpenEventW(EVENT_ALL_ACCESS, FALSE, Text::wide(ChildMode::variable(L"LSW_SUITES_CHAIN_READY")).c_str());
        HANDLE consumed = ::OpenEventW(EVENT_ALL_ACCESS, FALSE, Text::wide(ChildMode::variable(L"LSW_SUITES_CHAIN_CONSUMED")).c_str());
        if (handle == nullptr || ready == nullptr || consumed == nullptr) {
            return Outcome::fail("the Vulkan process cannot duplicate the texture or open the hand-off events");
        }
        VulkanBuffer host;
        Problems local;
        if (!vkkit::Kit::succeeds(local, "host buffer", vkkit::Kit::host_buffer(rig, host, static_cast<VkDeviceSize>(kSide) * kSide * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT))) {
            return Outcome::fail(local.text());
        }
        const std::uint32_t family = rig.main().family;
        Outcome outcome = Outcome::ok();
        for (int frame = 0; frame < kFrames; ++frame) {
            if (::WaitForSingleObject(consumed, 30000) != WAIT_OBJECT_0) {
                outcome = Outcome::fail("frame " + std::to_string(frame) + ": the composing process never released the texture");
                break;
            }
            const std::vector<std::uint8_t> pixels = rgba_frame(frame + 5, kSide, kSide);
            std::memcpy(host.mapped, pixels.data(), pixels.size());
            host.publish();
            VulkanSharedMemory imported;
            VkResult result = imported.import_image(rig, VulkanInterop::kD3d11, handle, VK_FORMAT_R8G8B8A8_UNORM, kSide, kSide, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
            if (result == VK_SUCCESS) {
                result = VulkanInterop::run(
                    rig,
                    [&](VkCommandBuffer commands) {
                        const VkImageMemoryBarrier acquire = image_barrier(imported.image, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_QUEUE_FAMILY_EXTERNAL, family, 0,
                                                                           VK_ACCESS_TRANSFER_WRITE_BIT);
                        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &acquire);
                        VkBufferImageCopy region{};
                        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        region.imageExtent = {kSide, kSide, 1};
                        rig.fn.vkCmdCopyBufferToImage(commands, host.buffer, imported.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                        const VkImageMemoryBarrier release = image_barrier(imported.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, family, VK_QUEUE_FAMILY_EXTERNAL,
                                                                           VK_ACCESS_TRANSFER_WRITE_BIT, 0);
                        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &release);
                    },
                    {}, {});
            }
            rig.wait_idle();
            imported.destroy();
            if (result != VK_SUCCESS) {
                outcome = Outcome::fail("frame " + std::to_string(frame) + ": importing or writing the shared texture returned " + VulkanTools::result_name(result));
                break;
            }
            ::SetEvent(ready);
        }
        ::CloseHandle(handle);
        ::CloseHandle(ready);
        ::CloseHandle(consumed);
        context.measure("role_done", true);
        return outcome;
    }
    if (ApiPolicy::is_skipped("d3d11") || ApiPolicy::is_skipped("vulkan")) {
        return Context::skip("this row needs both Vulkan and Direct3D 11");
    }
    {
        std::string probe_error;
        AvAdapter probe_adapter;
        if (!TargetAdapter::pick(probe_adapter, probe_error)) {
            return Outcome::fail(probe_error);
        }
        VulkanRig probe_rig;
        bool probe_opened = false;
        for (const AvAdapter& candidate : VulkanRig::target_adapters(probe_error)) {
            if (candidate.luid_key() == probe_adapter.luid_key()) {
                probe_opened = probe_rig.open(candidate, VulkanInterop::options(), probe_error);
            }
        }
        std::string reason;
        if (!probe_opened || !VulkanInterop::ready(probe_rig, reason)) {
            return Outcome::fail("the Vulkan device cannot be opened: " + probe_error + reason);
        }
        VkResult code = VK_SUCCESS;
        const VkExternalMemoryFeatureFlags features = VulkanInterop::image_features(probe_rig, VulkanInterop::kD3d11, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT, &code);
        if ((features & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) == 0) {
            return Context::skip("D3D11_TEXTURE images are not importable on " + probe_rig.describe() + " (features " + "0x" + Text::hex(features, 2) + ", query " + VulkanTools::result_name(code) +
                                 "); VUID-VkImportMemoryWin32HandleInfoKHR-handleType-09861: if handleType is not 0, it must be supported for import");
        }
    }
    SwapRig swap;
    SwapOptions options;
    options.width = kSide;
    options.height = kSide;
    options.usage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    std::string error;
    if (!swap.open(options, error)) {
        return Outcome::fail(error);
    }
    D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(kSide, kSide, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    Com<ID3D11Texture2D> shared = D3d11Tools::texture(swap.device, desc);
    Com<IDXGIResource1> resource = Kit::query<IDXGIResource1>(shared.get());
    HANDLE handle = nullptr;
    if (!shared || !resource || FAILED(resource->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle))) {
        return Outcome::fail("the shared D3D11 texture cannot be created");
    }
    Com<ID3D11ShaderResourceView> view = D3d11Tools::srv(swap.device, shared.get());
    Com<ID3D11PixelShader> ps = D3d11Tools::ps(swap.device, "Texture2D t : register(t0);\nfloat4 main(float4 p : SV_Position) : SV_Target { return t.Load(int3(p.xy, 0)); }\n", error);
    const std::string stem = "Local\\lsw-suites-chain-" + std::to_string(::GetCurrentProcessId());
    HANDLE ready = ::CreateEventW(nullptr, FALSE, FALSE, Text::wide(stem + "-ready").c_str());
    HANDLE consumed = ::CreateEventW(nullptr, FALSE, TRUE, Text::wide(stem + "-consumed").c_str());
    if (!view || !ps || ready == nullptr || consumed == nullptr) {
        ::CloseHandle(handle);
        return Outcome::fail("the composing pipeline or the hand-off events cannot be created: " + error);
    }
    ChildRunOptions child_options;
    child_options.timeout_seconds = 120;
    child_options.env = {{"LSW_SUITES_CHAIN_ROLE", "vulkan"}, {"LSW_SUITES_CHAIN_PID", std::to_string(::GetCurrentProcessId())}, {"LSW_SUITES_CHAIN_TEXTURE", Kit::handle_text(handle)},
                         {"LSW_SUITES_CHAIN_READY", stem + "-ready"}, {"LSW_SUITES_CHAIN_CONSUMED", stem + "-consumed"}};
    CheckResult child;
    std::thread runner([&] { child = ChildRuns::run_self("sanity.interop.vk.cross_process_present_chain", child_options); });
    Problems problems;
    int composed = 0;
    for (int frame = 0; frame < kFrames; ++frame) {
        if (::WaitForSingleObject(ready, 30000) != WAIT_OBJECT_0) {
            problems.add("frame " + std::to_string(frame) + ": the Vulkan process never finished rendering");
            break;
        }
        Com<ID3D11RenderTargetView> back = swap.back_view(0);
        if (!back) {
            problems.add("no render target view over the back buffer");
            ::SetEvent(consumed);
            break;
        }
        ID3D11ShaderResourceView* views[1] = {view.get()};
        swap.device.context->PSSetShaderResources(0, 1, views);
        const bool drawn = D3d11Tools::draw_fullscreen(swap.device, ps.get(), back.get(), kSide, kSide, error);
        ID3D11ShaderResourceView* none[1] = {nullptr};
        swap.device.context->PSSetShaderResources(0, 1, none);
        swap.device.context->OMSetRenderTargets(0, nullptr, nullptr);
        back.reset();
        if (!drawn) {
            problems.add(error);
            ::SetEvent(consumed);
            break;
        }
        const HRESULT presented = swap.chain->Present(1, 0);
        static_cast<void>(D3d11Tools::wait_event(swap.device, swap.device.context, error));
        ::SetEvent(consumed);
        if (FAILED(presented)) {
            problems.add("frame " + std::to_string(frame) + ": Present returned " + Kit::hr(presented));
            break;
        }
        std::string detail;
        if (!Kit::settle_screen(context, "chain_frame_" + std::to_string(frame), ScreenCapture::client_on_screen(swap.window.hwnd()), FrameImage::render(frame + 5, kSide, kSide), 0, 3000, detail)) {
            problems.add("frame " + std::to_string(frame) + " rendered by the Vulkan process and composed by the Direct3D 11 process is wrong: " + detail);
            break;
        }
        ++composed;
    }
    if (composed != kFrames) {
        ::SetEvent(consumed);
    }
    runner.join();
    if (child.status != Status::Ok) {
        problems.add("the Vulkan process failed: " + child.reason);
    }
    context.measure("frames_composed", composed);
    static_cast<void>(D3d11Tools::wait_event(swap.device, swap.device.context, error));
    ::CloseHandle(handle);
    ::CloseHandle(ready);
    ::CloseHandle(consumed);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::interop
