#include "sanity/vk/resource_checks.hpp"

#include "sanity/vk/kit.hpp"
#include "support/vulkan_formats.hpp"
#include "support/vulkan_image_io.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::vk {

namespace {

constexpr VkBufferUsageFlags kEveryUsage =
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
    VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

VkResult upload(VulkanRig& rig, VulkanBuffer& target, const void* data, std::size_t size) {
    VulkanBuffer staging;
    VkResult result = Kit::host_buffer(rig, staging, size);
    if (result != VK_SUCCESS) {
        return result;
    }
    std::memcpy(staging.mapped, data, size);
    staging.publish();
    return Kit::copy(rig, staging.buffer, target.buffer, size);
}

VkResult download(VulkanRig& rig, VulkanBuffer& source, std::vector<std::uint8_t>& out, std::size_t size) {
    VulkanBuffer staging;
    VkResult result = Kit::host_buffer(rig, staging, size);
    if (result != VK_SUCCESS) {
        return result;
    }
    staging.fill_value(0xCD);
    staging.publish();
    result = Kit::copy(rig, source.buffer, staging.buffer, size);
    if (result != VK_SUCCESS) {
        return result;
    }
    staging.acquire();
    out.assign(staging.bytes(), staging.bytes() + size);
    return VK_SUCCESS;
}

bool is_pow2(std::uint64_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

void check_buffer_usages(Context& context, VulkanRig& rig, Problems& problems) {
    const VkDeviceSize size = 64 * 1024;
    const std::size_t words = 1024;
    VulkanBuffer a;
    VulkanBuffer b;
    VulkanBuffer uniform;
    if (!Kit::succeeds(problems, "buffer with every legal usage flag", a.create(rig, size, kEveryUsage, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false)) ||
        !Kit::succeeds(problems, "second buffer with every usage flag", b.create(rig, size, kEveryUsage, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false)) ||
        !Kit::succeeds(problems, "third buffer with every usage flag", uniform.create(rig, 256, kEveryUsage, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false))) {
        return;
    }
    const VkMemoryRequirements& need = a.requirements;
    const VkPhysicalDeviceLimits& limits = rig.props.limits;
    if (!is_pow2(need.alignment) || need.size < size || need.memoryTypeBits == 0) {
        problems.add("memory requirements of the all-usage buffer are invalid (size " + std::to_string(need.size) + ", alignment " +
                     std::to_string(need.alignment) + ", types 0x" + Kit::hex(need.memoryTypeBits) + ")");
    }
    if (need.alignment % limits.minTexelBufferOffsetAlignment != 0 || need.alignment % limits.minUniformBufferOffsetAlignment != 0 ||
        need.alignment % limits.minStorageBufferOffsetAlignment != 0) {
        problems.add("memory requirement alignment " + std::to_string(need.alignment) +
                     " is not a multiple of the buffer offset alignment limits for the requested usages");
    }
    const std::vector<std::uint32_t> input = Kit::words(0x7E57, words);
    std::uint32_t params[4] = {3, 5, static_cast<std::uint32_t>(words), 0};
    if (!Kit::succeeds(problems, "upload to the storage buffer", upload(rig, a, input.data(), words * 4)) ||
        !Kit::succeeds(problems, "upload to the uniform buffer", upload(rig, uniform, params, sizeof(params)))) {
        return;
    }
    VulkanCompute ops;
    if (Kit::succeeds(problems, "compute pipeline",
                      ops.create(rig, VulkanShaders::words(VulkanShader::Ops),
                                 {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}}, 8))) {
        ops.write_buffer(0, a.buffer, 0, words * 4);
        ops.write_buffer(1, b.buffer, 0, words * 4);
        ops.write_buffer(2, uniform.buffer, 0, 16);
        const std::uint32_t push[2] = {0, 11};
        if (Kit::succeeds(problems, "dispatch reading a storage and a uniform buffer", ops.run(push, sizeof(push), static_cast<std::uint32_t>(words / 64)))) {
            std::vector<std::uint8_t> raw;
            if (Kit::succeeds(problems, "readback of the storage output", download(rig, b, raw, words * 4))) {
                std::vector<std::uint32_t> expected(words);
                for (std::size_t i = 0; i < words; ++i) {
                    expected[i] = input[i] * 3 + 5 + 11;
                }
                std::vector<std::uint32_t> actual(words);
                std::memcpy(actual.data(), raw.data(), words * 4);
                Kit::same_words(context, problems, "storage and uniform buffer shader result", expected, actual);
            }
        }
    }
    const VkFormatProperties format = Kit::format_properties(rig, VK_FORMAT_R32_UINT);
    const VkFormatFeatureFlags need_texel = VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT | VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
    if ((format.bufferFeatures & need_texel) == need_texel) {
        VulkanBuffer texel_out;
        VulkanOwned<VkBufferView> source_view;
        VulkanOwned<VkBufferView> dest_view;
        if (Kit::succeeds(problems, "texel output buffer", texel_out.create(rig, size, kEveryUsage, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false))) {
            VkBufferViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
            view.format = VK_FORMAT_R32_UINT;
            view.range = words * 4;
            view.buffer = a.buffer;
            const VkResult first = rig.make(source_view, rig.fn.vkCreateBufferView, rig.fn.vkDestroyBufferView, view);
            view.buffer = texel_out.buffer;
            const VkResult second = rig.make(dest_view, rig.fn.vkCreateBufferView, rig.fn.vkDestroyBufferView, view);
            VulkanCompute texel;
            if (Kit::succeeds(problems, "uniform texel buffer view", first) && Kit::succeeds(problems, "storage texel buffer view", second) &&
                Kit::succeeds(problems, "texel pipeline",
                              texel.create(rig, VulkanShaders::words(VulkanShader::Texel),
                                           {{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1}}, 0))) {
                texel.write_texel(0, source_view.get());
                texel.write_texel(1, dest_view.get());
                if (Kit::succeeds(problems, "dispatch over texel buffers", texel.run(nullptr, 0, static_cast<std::uint32_t>(words / 64)))) {
                    std::vector<std::uint8_t> raw;
                    if (Kit::succeeds(problems, "readback of the texel output", download(rig, texel_out, raw, words * 4))) {
                        std::vector<std::uint32_t> expected(words);
                        for (std::size_t i = 0; i < words; ++i) {
                            expected[i] = input[i] ^ 0xA5A5A5A5u;
                        }
                        std::vector<std::uint32_t> actual(words);
                        std::memcpy(actual.data(), raw.data(), words * 4);
                        Kit::same_words(context, problems, "texel buffer shader result", expected, actual);
                    }
                }
            }
        }
    } else {
        context.note("R32_UINT does not report both texel buffer features; texel usage skipped");
    }
    const float positions[8] = {-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};
    const std::uint16_t indices[4] = {0, 1, 2, 3};
    const VkDrawIndirectCommand direct{4, 1, 0, 0};
    const VkDrawIndexedIndirectCommand indexed{4, 1, 0, 0, 0};
    VulkanBuffer vertex;
    VulkanBuffer index;
    VulkanBuffer indirect_plain;
    VulkanBuffer indirect_indexed;
    if (!Kit::succeeds(problems, "vertex buffer", vertex.create(rig, 256, kEveryUsage, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false)) ||
        !Kit::succeeds(problems, "index buffer", index.create(rig, 256, kEveryUsage, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false)) ||
        !Kit::succeeds(problems, "indirect buffer", indirect_plain.create(rig, 256, kEveryUsage, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false)) ||
        !Kit::succeeds(problems, "indexed indirect buffer", indirect_indexed.create(rig, 256, kEveryUsage, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false)) ||
        !Kit::succeeds(problems, "upload vertices", upload(rig, vertex, positions, sizeof(positions))) ||
        !Kit::succeeds(problems, "upload indices", upload(rig, index, indices, sizeof(indices))) ||
        !Kit::succeeds(problems, "upload indirect arguments", upload(rig, indirect_plain, &direct, sizeof(direct))) ||
        !Kit::succeeds(problems, "upload indexed indirect arguments", upload(rig, indirect_indexed, &indexed, sizeof(indexed)))) {
        return;
    }
    const struct {
        SceneDraw draw;
        const char* name;
    } modes[] = {{SceneDraw::Direct, "vertex buffer draw"}, {SceneDraw::Indexed, "index buffer draw"},
                 {SceneDraw::Indirect, "indirect buffer draw"}, {SceneDraw::IndexedIndirect, "indexed indirect buffer draw"}};
    for (const auto& mode : modes) {
        SceneOptions options;
        options.kind = SceneKind::Vertex;
        options.draw = mode.draw;
        options.vertex_buffer = vertex.buffer;
        options.index_buffer = index.buffer;
        options.indirect_buffer = mode.draw == SceneDraw::Indirect ? indirect_plain.buffer : indirect_indexed.buffer;
        VulkanScene scene;
        if (!Kit::succeeds(problems, std::string(mode.name) + " pipeline", scene.create(rig, options))) {
            continue;
        }
        SceneRect rect;
        rect.x0 = 8;
        rect.y0 = 8;
        rect.x1 = 24;
        rect.y1 = 24;
        rect.color[0] = 1.0f;
        rect.color[1] = 0.0f;
        rect.color[2] = 0.5f;
        const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        SceneResult out;
        if (Kit::succeeds(problems, std::string(mode.name) + " render", scene.render({rect}, clear, 1.0f, out))) {
            const std::vector<std::uint8_t> expected = scene.expected_color({rect}, clear, 1.0f, false);
            Kit::same_bytes(context, problems, mode.name, expected.data(), out.color.data(), expected.size());
        }
    }
}

struct FeatureUsage {
    VkFormatFeatureFlags feature;
    VkImageUsageFlags usage;
    const char* name;
};

const FeatureUsage kFeatureUsages[] = {
    {VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, VK_IMAGE_USAGE_SAMPLED_BIT, "SAMPLED"},
    {VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT, VK_IMAGE_USAGE_STORAGE_BIT, "STORAGE"},
    {VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, "COLOR_ATTACHMENT"},
    {VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, "DEPTH_STENCIL_ATTACHMENT"},
    {VK_FORMAT_FEATURE_TRANSFER_SRC_BIT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "TRANSFER_SRC"},
    {VK_FORMAT_FEATURE_TRANSFER_DST_BIT, VK_IMAGE_USAGE_TRANSFER_DST_BIT, "TRANSFER_DST"},
};

bool attachment_roundtrip(VulkanRig& rig, const VulkanFormatInfo& info, bool depth, const VkClearValue& clear, std::vector<std::uint8_t>& out,
                          VkResult& code) {
    const std::uint32_t side = 8;
    VulkanImage image;
    const VkImageUsageFlags usage = (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    code = image.create_2d(rig, info.format, side, side, usage);
    if (code != VK_SUCCESS) {
        return false;
    }
    VulkanOwned<VkImageView> view;
    const VkImageAspectFlags aspect = VulkanFormats::aspect(info);
    code = image.view(view, VK_IMAGE_VIEW_TYPE_2D, aspect);
    if (code != VK_SUCCESS) {
        return false;
    }
    VkAttachmentDescription attachment{};
    attachment.format = info.format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = info.kind == VulkanFormatKind::Stencil ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = info.kind == VulkanFormatKind::Stencil ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const VkAttachmentReference reference{0, depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = depth ? 0 : 1;
    subpass.pColorAttachments = depth ? nullptr : &reference;
    subpass.pDepthStencilAttachment = depth ? &reference : nullptr;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = 0;
    dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo pass_info{};
    pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass_info.attachmentCount = 1;
    pass_info.pAttachments = &attachment;
    pass_info.subpassCount = 1;
    pass_info.pSubpasses = &subpass;
    pass_info.dependencyCount = 1;
    pass_info.pDependencies = &dependency;
    VulkanOwned<VkRenderPass> pass;
    code = rig.make(pass, rig.fn.vkCreateRenderPass, rig.fn.vkDestroyRenderPass, pass_info);
    if (code != VK_SUCCESS) {
        return false;
    }
    VkImageView view_handle = view.get();
    VkFramebufferCreateInfo framebuffer_info{};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = pass.get();
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = &view_handle;
    framebuffer_info.width = side;
    framebuffer_info.height = side;
    framebuffer_info.layers = 1;
    VulkanOwned<VkFramebuffer> framebuffer;
    code = rig.make(framebuffer, rig.fn.vkCreateFramebuffer, rig.fn.vkDestroyFramebuffer, framebuffer_info);
    if (code != VK_SUCCESS) {
        return false;
    }
    VulkanBuffer readback;
    code = Kit::host_buffer(rig, readback, side * side * info.texel);
    if (code != VK_SUCCESS) {
        return false;
    }
    readback.fill_value(0xCD);
    readback.publish();
    code = rig.run([&](VkCommandBuffer commands) {
        VkRenderPassBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = pass.get();
        begin.framebuffer = framebuffer.get();
        begin.renderArea = {{0, 0}, {side, side}};
        begin.clearValueCount = 1;
        begin.pClearValues = &clear;
        rig.fn.vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
        rig.fn.vkCmdEndRenderPass(commands);
        const VkBufferImageCopy region{0, 0, 0, {aspect, 0, 0, 1}, {0, 0, 0}, {side, side, 1}};
        rig.fn.vkCmdCopyImageToBuffer(commands, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &region);
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0,
                                    nullptr);
    });
    if (code != VK_SUCCESS) {
        return false;
    }
    readback.acquire();
    out.assign(readback.bytes(), readback.bytes() + side * side * info.texel);
    return true;
}

void check_attachment(Context& context, VulkanRig& rig, const VulkanFormatInfo& info, std::uint32_t seed, Problems& problems) {
    const bool depth = info.kind == VulkanFormatKind::Depth || info.kind == VulkanFormatKind::Stencil;
    VkClearValue clear{};
    std::vector<std::uint8_t> pixel;
    if (depth) {
        if (info.kind == VulkanFormatKind::Stencil) {
            clear.depthStencil = {0.0f, 0x5Au};
            pixel = {0x5A};
        } else if (info.texel == 4) {
            clear.depthStencil = {0.25f, 0};
            const float value = 0.25f;
            pixel.resize(4);
            std::memcpy(pixel.data(), &value, 4);
        } else {
            clear.depthStencil = {0.25f, 0};
            pixel = {0x00, 0x40};
        }
    } else {
        clear.color = VulkanFormats::clear_value(info, seed);
        pixel = VulkanFormats::encode_clear(info, clear.color);
    }
    std::vector<std::uint8_t> out;
    VkResult code = VK_SUCCESS;
    const std::string what = VulkanTools::format_name(info.format) + " attachment clear";
    if (!attachment_roundtrip(rig, info, depth, clear, out, code)) {
        Kit::succeeds(problems, what, code);
        return;
    }
    std::vector<std::uint8_t> expected;
    for (int i = 0; i < 64; ++i) {
        expected.insert(expected.end(), pixel.begin(), pixel.end());
    }
    Kit::same_bytes(context, problems, what, expected.data(), out.data(), expected.size());
}

void check_sampling(Context& context, VulkanRig& rig, Problems& problems) {
    const VkFormatProperties props = Kit::format_properties(rig, VK_FORMAT_R8G8B8A8_UNORM);
    if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0) {
        return;
    }
    VulkanImage image;
    if (!Kit::succeeds(problems, "sampled image", image.create_2d(rig, VK_FORMAT_R8G8B8A8_UNORM, 4, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) ||
        !Kit::succeeds(problems, "sampled image layout", VulkanImageIo::to_general(rig, image))) {
        return;
    }
    const std::vector<std::uint8_t> texels = {10, 20, 30, 255, 200, 100, 50, 255, 60, 90, 120, 255, 250, 240, 0, 255};
    ImageRegion region;
    region.extent = {4, 1, 1};
    VulkanOwned<VkImageView> view;
    if (!Kit::succeeds(problems, "upload to the sampled image", VulkanImageIo::write(rig, image, region, texels)) ||
        !Kit::succeeds(problems, "sampled image view", image.view(view, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT))) {
        return;
    }
    const bool linear_ok = (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
    for (const bool linear : {false, true}) {
        if (linear && !linear_ok) {
            continue;
        }
        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        sampler_info.minFilter = sampler_info.magFilter;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.maxLod = 0.0f;
        VulkanOwned<VkSampler> sampler;
        VulkanCompute sample;
        VulkanBuffer output;
        const std::string what = linear ? "linear sampling" : "nearest sampling";
        if (!Kit::succeeds(problems, what + " sampler", rig.make(sampler, rig.fn.vkCreateSampler, rig.fn.vkDestroySampler, sampler_info)) ||
            !Kit::succeeds(problems, what + " pipeline",
                           sample.create(rig, VulkanShaders::words(VulkanShader::Sample),
                                         {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}}, 20)) ||
            !Kit::succeeds(problems, what + " output", Kit::host_buffer(rig, output, 256))) {
            continue;
        }
        output.fill_value(0xCD);
        output.publish();
        sample.write_image(0, view.get(), sampler.get(), VK_IMAGE_LAYOUT_GENERAL);
        sample.write_buffer(1, output.buffer);
        const std::uint32_t count = linear ? 3 : 4;
        const float push_values[4] = {linear ? 0.25f : 0.125f, 0.5f, 0.25f, 0.0f};
        std::uint8_t push[20];
        std::memcpy(push, push_values, 16);
        std::memcpy(push + 16, &count, 4);
        if (!Kit::succeeds(problems, what + " dispatch", sample.run(push, 20, 1))) {
            continue;
        }
        output.acquire();
        const float* got = reinterpret_cast<const float*>(output.bytes());
        for (std::uint32_t i = 0; i < count; ++i) {
            for (std::uint32_t c = 0; c < 4; ++c) {
                float expected = static_cast<float>(texels[i * 4 + c]) / 255.0f;
                if (linear) {
                    expected = (static_cast<float>(texels[i * 4 + c]) + static_cast<float>(texels[(i + 1) * 4 + c])) / 510.0f;
                }
                const float tolerance = linear ? 1.5f / 255.0f : 0.25f / 255.0f;
                if (std::fabs(got[i * 4 + c] - expected) > tolerance) {
                    problems.add(what + ": sample " + std::to_string(i) + " channel " + std::to_string(c) + " is " + std::to_string(got[i * 4 + c]) +
                                 ", expected " + std::to_string(expected));
                }
            }
        }
    }
    (void)context;
}

void check_storage(Context& context, VulkanRig& rig, Problems& problems) {
    const struct {
        VkFormat format;
        VulkanShader shader;
        bool rgba;
    } cases[] = {{VK_FORMAT_R32_UINT, VulkanShader::StoreUint, false}, {VK_FORMAT_R8G8B8A8_UNORM, VulkanShader::StoreRgba, true}};
    for (const auto& item : cases) {
        const VkFormatProperties props = Kit::format_properties(rig, item.format);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0) {
            continue;
        }
        const std::uint32_t side = 16;
        const std::string what = VulkanTools::format_name(item.format) + " storage image";
        VulkanImage image;
        VulkanOwned<VkImageView> view;
        VulkanCompute store;
        if (!Kit::succeeds(problems, what, image.create_2d(rig, item.format, side, side, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) ||
            !Kit::succeeds(problems, what + " layout", VulkanImageIo::to_general(rig, image)) ||
            !Kit::succeeds(problems, what + " view", image.view(view, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT)) ||
            !Kit::succeeds(problems, what + " pipeline", store.create(rig, VulkanShaders::words(item.shader), {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}}, 8))) {
            continue;
        }
        store.write_image(0, view.get(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);
        const std::uint32_t push[2] = {side, side * side};
        if (!Kit::succeeds(problems, what + " dispatch", store.run(push, 8, side * side / 8))) {
            continue;
        }
        ImageRegion region;
        region.extent = {side, side, 1};
        std::vector<std::uint8_t> out;
        if (!Kit::succeeds(problems, what + " readback", VulkanImageIo::read(rig, image, region, out, 4))) {
            continue;
        }
        std::vector<std::uint8_t> expected(side * side * 4);
        for (std::uint32_t i = 0; i < side * side; ++i) {
            if (!item.rgba) {
                const std::uint32_t value = i * 2654435761u;
                std::memcpy(expected.data() + i * 4, &value, 4);
            } else {
                expected[i * 4 + 0] = static_cast<std::uint8_t>(i & 255);
                expected[i * 4 + 1] = static_cast<std::uint8_t>(255 - (i & 255));
                expected[i * 4 + 2] = static_cast<std::uint8_t>((i * 7) & 255);
                expected[i * 4 + 3] = 255;
            }
        }
        Kit::same_bytes(context, problems, what + " shader store", expected.data(), out.data(), expected.size());
    }
}

}  // namespace

Outcome ResourceChecks::all_usages(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        check_buffer_usages(context, rig, problems);
        return {};
    });
}

Outcome ResourceChecks::formats_features_usage(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        std::size_t checked = 0;
        std::uint32_t seed = 1;
        for (const VulkanFormatInfo& info : VulkanFormats::all()) {
            const VkFormatProperties props = Kit::format_properties(rig, info.format);
            for (const VkImageTiling tiling : {VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_TILING_LINEAR}) {
                const VkFormatFeatureFlags features = tiling == VK_IMAGE_TILING_OPTIMAL ? props.optimalTilingFeatures : props.linearTilingFeatures;
                for (const FeatureUsage& item : kFeatureUsages) {
                    VkImageFormatProperties out{};
                    VkResult code = VK_SUCCESS;
                    const bool supported = Kit::image_supported(rig, info.format, VK_IMAGE_TYPE_2D, tiling, item.usage, 0, out, &code);
                    const bool reported = (features & item.feature) != 0;
                    const std::string what = VulkanTools::format_name(info.format) + (tiling == VK_IMAGE_TILING_OPTIMAL ? " optimal " : " linear ") + item.name;
                    ++checked;
                    if (reported && !supported) {
                        problems.add(what + ": the format feature is reported but vkGetPhysicalDeviceImageFormatProperties returns " + Kit::name(code));
                    } else if (!reported && supported) {
                        problems.add(what + ": the format feature is not reported but vkGetPhysicalDeviceImageFormatProperties succeeds");
                    } else if (!reported && code != VK_ERROR_FORMAT_NOT_SUPPORTED) {
                        problems.add(what + ": unsupported combination returned " + Kit::name(code) + ", expected VK_ERROR_FORMAT_NOT_SUPPORTED");
                    }
                    if (reported && supported && tiling == VK_IMAGE_TILING_OPTIMAL) {
                        VulkanImage image;
                        const VkResult made = image.create_2d(rig, info.format, 8, 8, item.usage);
                        Kit::succeeds(problems, what + " image creation", made);
                    }
                }
            }
            const VkFormatFeatureFlags wanted = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
            if (info.kind != VulkanFormatKind::Depth && info.kind != VulkanFormatKind::Stencil && (props.optimalTilingFeatures & wanted) == wanted) {
                check_attachment(context, rig, info, seed++, problems);
            }
            const VkFormatFeatureFlags depth_wanted = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
            if ((info.kind == VulkanFormatKind::Depth || info.kind == VulkanFormatKind::Stencil) && (props.optimalTilingFeatures & depth_wanted) == depth_wanted) {
                check_attachment(context, rig, info, seed++, problems);
            }
        }
        check_sampling(context, rig, problems);
        check_storage(context, rig, problems);
        context.measure("vk.format_usage_pairs_checked", checked);
        return {};
    });
}

Outcome ResourceChecks::types_layers_mips(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        struct Case {
            const char* name;
            VkImageType type;
            VkImageCreateFlags flags;
            VkExtent3D extent;
            std::uint32_t mips;
            std::uint32_t layers;
            bool needs_cube_array;
        };
        const Case cases[] = {
            {"1D", VK_IMAGE_TYPE_1D, 0, {64, 1, 1}, 7, 1, false},
            {"1D array", VK_IMAGE_TYPE_1D, 0, {32, 1, 1}, 6, 4, false},
            {"2D", VK_IMAGE_TYPE_2D, 0, {64, 32, 1}, 7, 1, false},
            {"2D array", VK_IMAGE_TYPE_2D, 0, {32, 16, 1}, 6, 3, false},
            {"cube", VK_IMAGE_TYPE_2D, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, {16, 16, 1}, 5, 6, false},
            {"cube array", VK_IMAGE_TYPE_2D, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, {16, 16, 1}, 5, 12, true},
            {"3D", VK_IMAGE_TYPE_3D, 0, {16, 16, 8}, 5, 1, false},
        };
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        for (const Case& item : cases) {
            if (item.needs_cube_array && rig.features.imageCubeArray == VK_FALSE) {
                context.note("cube array skipped: imageCubeArray is not reported");
                continue;
            }
            VkImageFormatProperties limits{};
            VkResult code = VK_SUCCESS;
            if (!Kit::image_supported(rig, VK_FORMAT_R8G8B8A8_UNORM, item.type, VK_IMAGE_TILING_OPTIMAL, usage, item.flags, limits, &code)) {
                problems.add(std::string(item.name) + " R8G8B8A8_UNORM image format query returned " + Kit::name(code));
                continue;
            }
            if (limits.maxMipLevels < item.mips || limits.maxArrayLayers < item.layers) {
                problems.add(std::string(item.name) + " limits report " + std::to_string(limits.maxMipLevels) + " mips and " + std::to_string(limits.maxArrayLayers) +
                             " layers, below what the extent needs");
                continue;
            }
            VkImageCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            info.flags = item.flags;
            info.imageType = item.type;
            info.format = VK_FORMAT_R8G8B8A8_UNORM;
            info.extent = item.extent;
            info.mipLevels = item.mips;
            info.arrayLayers = item.layers;
            info.samples = VK_SAMPLE_COUNT_1_BIT;
            info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.usage = usage;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VulkanImage image;
            if (!Kit::succeeds(problems, std::string(item.name) + " image", image.create(rig, info)) ||
                !Kit::succeeds(problems, std::string(item.name) + " layout", VulkanImageIo::to_general(rig, image))) {
                continue;
            }
            std::vector<ImageRegion> regions;
            std::vector<std::vector<std::uint8_t>> patterns;
            bool wrote = true;
            for (std::uint32_t mip = 0; mip < item.mips && wrote; ++mip) {
                for (std::uint32_t layer = 0; layer < item.layers && wrote; ++layer) {
                    ImageRegion region;
                    region.mip = mip;
                    region.layer = layer;
                    region.extent = {std::max(1u, item.extent.width >> mip), std::max(1u, item.extent.height >> mip), std::max(1u, item.extent.depth >> mip)};
                    std::vector<std::uint8_t> data(VulkanImageIo::region_bytes(region, 4));
                    Pattern::fill(0x1000 + mip * 64 + layer, 0, data.data(), data.size());
                    wrote = Kit::succeeds(problems, std::string(item.name) + " write of mip " + std::to_string(mip) + " layer " + std::to_string(layer),
                                          VulkanImageIo::write(rig, image, region, data));
                    regions.push_back(region);
                    patterns.push_back(std::move(data));
                }
            }
            for (std::size_t i = 0; wrote && i < regions.size(); ++i) {
                std::vector<std::uint8_t> back;
                const std::string where = std::string(item.name) + " mip " + std::to_string(regions[i].mip) + " layer " + std::to_string(regions[i].layer);
                if (Kit::succeeds(problems, where + " read", VulkanImageIo::read(rig, image, regions[i], back, 4))) {
                    Kit::same_bytes(context, problems, where, patterns[i].data(), back.data(), patterns[i].size());
                }
            }
        }
        const VkSampleCountFlags counts = rig.props.limits.sampledImageColorSampleCounts & rig.props.limits.framebufferColorSampleCounts;
        for (const VkSampleCountFlagBits samples : {VK_SAMPLE_COUNT_2_BIT, VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_8_BIT}) {
            VkImageFormatProperties limits{};
            VkResult code = VK_SUCCESS;
            const bool supported = Kit::image_supported(rig, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0, limits, &code);
            const VkSampleCountFlags bit = static_cast<VkSampleCountFlags>(samples);
            const bool listed = (counts & bit) != 0;
            const std::string name = std::to_string(static_cast<int>(samples)) + " samples";
            if (listed && (!supported || (limits.sampleCounts & bit) == 0)) {
                problems.add(name + " are listed in the device limits but the format query does not offer them");
                continue;
            }
            if (!listed) {
                continue;
            }
            VulkanImage image;
            Kit::succeeds(problems, name + " multisample image", image.create_2d(rig, VK_FORMAT_R8G8B8A8_UNORM, 32, 32,
                                                                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, samples));
        }
        return {};
    });
}

Outcome ResourceChecks::sparse_caps(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        const VkPhysicalDeviceFeatures& f = rig.features;
        if (f.sparseResidencyBuffer != VK_FALSE && f.sparseBinding == VK_FALSE) {
            problems.add("sparseResidencyBuffer is reported without sparseBinding");
        }
        if ((f.sparseResidencyImage2D != VK_FALSE || f.sparseResidencyImage3D != VK_FALSE) && f.sparseBinding == VK_FALSE) {
            problems.add("sparse image residency is reported without sparseBinding");
        }
        if ((f.sparseResidency2Samples != VK_FALSE || f.sparseResidency4Samples != VK_FALSE || f.sparseResidency8Samples != VK_FALSE ||
             f.sparseResidency16Samples != VK_FALSE) && f.sparseResidencyImage2D == VK_FALSE) {
            problems.add("multisample sparse residency is reported without sparseResidencyImage2D");
        }
        if (f.sparseResidencyAliased != VK_FALSE && f.sparseBinding == VK_FALSE) {
            problems.add("sparseResidencyAliased is reported without sparseBinding");
        }
        bool sparse_queue = false;
        for (const VulkanQueue& queue : rig.queues) {
            sparse_queue = sparse_queue || (queue.properties.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) != 0;
        }
        if (f.sparseBinding != VK_FALSE && !sparse_queue) {
            problems.add("sparseBinding is reported but no queue family has VK_QUEUE_SPARSE_BINDING_BIT");
        }
        VkImageFormatProperties limits{};
        VkResult code = VK_SUCCESS;
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        const bool binding_image = Kit::image_supported(rig, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage,
                                                       VK_IMAGE_CREATE_SPARSE_BINDING_BIT, limits, &code);
        if (f.sparseBinding == VK_FALSE) {
            Kit::expect(problems, "sparse binding image format query without sparseBinding", code, {VK_ERROR_FORMAT_NOT_SUPPORTED});
        } else if (!binding_image) {
            problems.add("sparseBinding is reported but the SPARSE_BINDING image format query returns " + Kit::name(code));
        }
        const bool residency_image = Kit::image_supported(rig, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage,
                                                         VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT, limits, &code);
        if (f.sparseResidencyImage2D == VK_FALSE) {
            if (residency_image) {
                problems.add("a SPARSE_RESIDENCY 2D image format query succeeds although sparseResidencyImage2D is not reported");
            } else {
                Kit::expect(problems, "sparse residency image format query without sparseResidencyImage2D", code, {VK_ERROR_FORMAT_NOT_SUPPORTED});
            }
        }
        std::uint32_t property_count = 0;
        rig.fn.vkGetPhysicalDeviceSparseImageFormatProperties(rig.physical, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_SAMPLE_COUNT_1_BIT, usage,
                                                              VK_IMAGE_TILING_OPTIMAL, &property_count, nullptr);
        if (f.sparseResidencyImage2D == VK_FALSE && property_count != 0) {
            problems.add("sparse image format properties are listed for R8G8B8A8_UNORM although sparseResidencyImage2D is not reported");
        }
        if (f.sparseResidencyImage2D != VK_FALSE && property_count == 0) {
            problems.add("sparseResidencyImage2D is reported but R8G8B8A8_UNORM lists no sparse image format properties");
        }
        context.measure("vk.sparseBinding", f.sparseBinding != VK_FALSE);
        if (f.sparseBinding == VK_FALSE) {
            return {};
        }
        VulkanQueue* queue = nullptr;
        for (VulkanQueue& item : rig.queues) {
            if ((item.properties.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) != 0) {
                queue = &item;
                break;
            }
        }
        if (queue == nullptr) {
            return {};
        }
        const VkBufferCreateFlags flags = VK_BUFFER_CREATE_SPARSE_BINDING_BIT;
        const VkDeviceSize pieces = 4;
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.flags = flags;
        info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkMemoryRequirements probe{};
        info.size = 4096;
        {
            VulkanOwned<VkBuffer> sample;
            if (!Kit::succeeds(problems, "sparse buffer probe", rig.make(sample, rig.fn.vkCreateBuffer, rig.fn.vkDestroyBuffer, info))) {
                return {};
            }
            rig.fn.vkGetBufferMemoryRequirements(rig.device, sample.get(), &probe);
        }
        const VkDeviceSize piece = probe.alignment;
        info.size = piece * pieces;
        VulkanOwned<VkBuffer> sparse;
        if (!Kit::succeeds(problems, "sparse buffer", rig.make(sparse, rig.fn.vkCreateBuffer, rig.fn.vkDestroyBuffer, info))) {
            return {};
        }
        VkMemoryRequirements need{};
        rig.fn.vkGetBufferMemoryRequirements(rig.device, sparse.get(), &need);
        const int type = rig.find_type(need.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
        if (type < 0) {
            context.note("no host visible memory type for the sparse buffer; binding probe skipped");
            return {};
        }
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = piece * pieces;
        allocate.memoryTypeIndex = static_cast<std::uint32_t>(type);
        VkDeviceMemory memory = VK_NULL_HANDLE;
        if (!Kit::succeeds(problems, "memory for the sparse buffer", rig.fn.vkAllocateMemory(rig.device, &allocate, nullptr, &memory))) {
            return {};
        }
        std::vector<VkSparseMemoryBind> binds;
        for (VkDeviceSize i = 0; i < pieces; ++i) {
            VkSparseMemoryBind bind{};
            bind.resourceOffset = i * piece;
            bind.size = piece;
            bind.memory = memory;
            bind.memoryOffset = (pieces - 1 - i) * piece;
            binds.push_back(bind);
        }
        VkSparseBufferMemoryBindInfo buffer_bind{sparse.get(), static_cast<std::uint32_t>(binds.size()), binds.data()};
        VkBindSparseInfo bind_info{};
        bind_info.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
        bind_info.bufferBindCount = 1;
        bind_info.pBufferBinds = &buffer_bind;
        VulkanOwned<VkFence> fence;
        if (Kit::succeeds(problems, "fence", rig.make_fence(fence))) {
            rig.mark_pending();
            if (Kit::succeeds(problems, "vkQueueBindSparse", rig.fn.vkQueueBindSparse(queue->handle, 1, &bind_info, fence.get())) &&
                Kit::succeeds(problems, "sparse bind completion", rig.wait_fence(fence.get(), 20ull * 1000000000ull))) {
                VulkanBuffer host;
                void* mapped = nullptr;
                if (Kit::succeeds(problems, "host buffer", Kit::host_buffer(rig, host, piece * pieces)) &&
                    Kit::succeeds(problems, "map of the sparse memory", rig.fn.vkMapMemory(rig.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped))) {
                    std::vector<std::uint8_t> pattern(static_cast<std::size_t>(piece * pieces));
                    Pattern::fill(0x5BA5, 0, pattern.data(), pattern.size());
                    std::memcpy(host.mapped, pattern.data(), pattern.size());
                    host.publish();
                    if (Kit::succeeds(problems, "copy into the sparse buffer", Kit::copy(rig, host.buffer, sparse.get(), piece * pieces))) {
                        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, memory, 0, VK_WHOLE_SIZE};
                        if ((rig.memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
                            (void)rig.fn.vkInvalidateMappedMemoryRanges(rig.device, 1, &range);
                        }
                        for (VkDeviceSize i = 0; i < pieces; ++i) {
                            const std::uint8_t* seen = static_cast<const std::uint8_t*>(mapped) + (pieces - 1 - i) * piece;
                            Kit::same_bytes(context, problems, "sparse piece " + std::to_string(i) + " in its bound memory range", pattern.data() + i * piece, seen,
                                            static_cast<std::size_t>(piece));
                        }
                    }
                    rig.fn.vkUnmapMemory(rig.device, memory);
                }
            }
        }
        rig.wait_idle();
        sparse.reset();
        rig.fn.vkFreeMemory(rig.device, memory, nullptr);
        return {};
    });
}

}  // namespace lsw::suites::sanity::vk
