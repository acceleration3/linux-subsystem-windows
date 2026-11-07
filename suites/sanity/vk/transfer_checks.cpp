#include "sanity/vk/transfer_checks.hpp"

#include "sanity/vk/kit.hpp"
#include "support/vulkan_formats.hpp"
#include "support/vulkan_image_io.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::vk {

namespace {

constexpr VkImageUsageFlags kCopyUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
constexpr VkFormatFeatureFlags kCopyFeatures = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;

std::vector<std::uint8_t> pattern_for(const VulkanFormatInfo& info, std::uint64_t seed, std::size_t texels) {
    std::vector<std::uint8_t> data(texels * info.texel);
    Pattern::fill(seed, 0, data.data(), data.size());
    if (info.kind == VulkanFormatKind::Depth && info.texel == 4) {
        for (std::size_t i = 0; i < texels; ++i) {
            std::uint32_t word = 0;
            std::memcpy(&word, data.data() + i * 4, 4);
            const float value = static_cast<float>(word & 0xFFFFu) / 65535.0f;
            std::memcpy(data.data() + i * 4, &value, 4);
        }
    }
    return data;
}

void copy_block(std::vector<std::uint8_t>& destination, std::size_t destination_stride, std::uint32_t dx, std::uint32_t dy,
                const std::vector<std::uint8_t>& source, std::size_t source_offset, std::size_t source_stride, std::uint32_t sx,
                std::uint32_t sy, std::uint32_t width, std::uint32_t height, std::uint32_t texel) {
    for (std::uint32_t y = 0; y < height; ++y) {
        std::memcpy(destination.data() + (static_cast<std::size_t>(dy) + y) * destination_stride + static_cast<std::size_t>(dx) * texel,
                    source.data() + source_offset + (static_cast<std::size_t>(sy) + y) * source_stride + static_cast<std::size_t>(sx) * texel,
                    static_cast<std::size_t>(width) * texel);
    }
}

VkResult image_copy(VulkanRig& rig, const VulkanImage& source, const VulkanImage& destination, const VkImageCopy& region) {
    return rig.run([&](VkCommandBuffer commands) {
        rig.fn.vkCmdCopyImage(commands, source.image, VK_IMAGE_LAYOUT_GENERAL, destination.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0,
                                    nullptr, 0, nullptr);
    });
}

void check_buffer_copies(Context& context, VulkanRig& rig, Problems& problems) {
    const std::size_t size = 4096;
    VulkanBuffer source;
    VulkanBuffer destination;
    VulkanBuffer device;
    if (!Kit::succeeds(problems, "source buffer", Kit::host_buffer(rig, source, size)) ||
        !Kit::succeeds(problems, "destination buffer", Kit::host_buffer(rig, destination, size)) ||
        !Kit::succeeds(problems, "device buffer", Kit::device_buffer(rig, device, size))) {
        return;
    }
    source.fill_pattern(0xC0DE);
    source.publish();
    destination.fill_value(0xCD);
    destination.publish();
    const VkBufferCopy regions[3] = {{5, 100, 37}, {1000, 3000, 1000}, {2000, 1, 70}};
    std::vector<std::uint8_t> expected(size, 0xCD);
    for (const VkBufferCopy& region : regions) {
        std::memcpy(expected.data() + region.dstOffset, source.bytes() + region.srcOffset, static_cast<std::size_t>(region.size));
    }
    const VkResult result = rig.run([&](VkCommandBuffer commands) {
        rig.fn.vkCmdCopyBuffer(commands, source.buffer, destination.buffer, 3, regions);
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    });
    if (Kit::succeeds(problems, "three-region buffer copy", result)) {
        destination.acquire();
        Kit::same_bytes(context, problems, "buffer to buffer copy with odd offsets", expected.data(), destination.bytes(), size);
    }
}

void check_format(Context& context, VulkanRig& rig, const VulkanFormatInfo& info, Problems& problems) {
    const std::string name = VulkanTools::format_name(info.format);
    const std::uint32_t width = 24;
    const std::uint32_t height = 20;
    const std::uint32_t texel = info.texel;
    const VkImageAspectFlags aspect = VulkanFormats::aspect(info);
    VulkanImage image;
    if (!Kit::succeeds(problems, name + " image", image.create_2d(rig, info.format, width, height, kCopyUsage, VK_SAMPLE_COUNT_1_BIT, 1, 2)) ||
        !Kit::succeeds(problems, name + " layout", VulkanImageIo::to_general(rig, image))) {
        return;
    }
    const std::size_t layer_bytes = static_cast<std::size_t>(width) * height * texel;
    std::vector<std::vector<std::uint8_t>> model(2);
    for (std::uint32_t layer = 0; layer < 2; ++layer) {
        model[layer] = pattern_for(info, 0x2000 + layer, static_cast<std::size_t>(width) * height);
        ImageRegion region;
        region.aspect = aspect;
        region.layer = layer;
        region.extent = {width, height, 1};
        if (!Kit::succeeds(problems, name + " fill of layer " + std::to_string(layer), VulkanImageIo::write(rig, image, region, model[layer]))) {
            return;
        }
    }
    ImageRegion sub;
    sub.aspect = aspect;
    sub.layer = 1;
    sub.offset = {3, 5, 0};
    sub.extent = {7, 9, 1};
    sub.buffer_offset = 4 * texel;
    sub.row_length = 16;
    sub.image_height = 12;
    const std::vector<std::uint8_t> staged = pattern_for(info, 0x3000, VulkanImageIo::span_bytes(sub, texel) / texel + 1);
    std::vector<std::uint8_t> bytes(VulkanImageIo::span_bytes(sub, texel));
    std::memcpy(bytes.data(), staged.data(), bytes.size());
    if (!Kit::succeeds(problems, name + " strided buffer to image copy", VulkanImageIo::write(rig, image, sub, bytes))) {
        return;
    }
    copy_block(model[1], static_cast<std::size_t>(width) * texel, 3, 5, bytes, sub.buffer_offset, static_cast<std::size_t>(16) * texel, 0, 0, 7, 9, texel);
    VkImageCopy copy{};
    copy.srcSubresource = {aspect, 0, 1, 1};
    copy.srcOffset = {1, 2, 0};
    copy.dstSubresource = {aspect, 0, 0, 1};
    copy.dstOffset = {10, 3, 0};
    copy.extent = {11, 6, 1};
    if (!Kit::succeeds(problems, name + " image to image copy", image_copy(rig, image, image, copy))) {
        return;
    }
    copy_block(model[0], static_cast<std::size_t>(width) * texel, 10, 3, model[1], 0, static_cast<std::size_t>(width) * texel, 1, 2, 11, 6, texel);
    for (std::uint32_t layer = 0; layer < 2; ++layer) {
        ImageRegion region;
        region.aspect = aspect;
        region.layer = layer;
        region.extent = {width, height, 1};
        std::vector<std::uint8_t> back;
        if (Kit::succeeds(problems, name + " readback of layer " + std::to_string(layer), VulkanImageIo::read(rig, image, region, back, texel))) {
            Kit::same_bytes(context, problems, name + " layer " + std::to_string(layer) + " after the copy sequence", model[layer].data(), back.data(), layer_bytes);
        }
    }
    ImageRegion out;
    out.aspect = aspect;
    out.layer = 0;
    out.offset = {2, 1, 0};
    out.extent = {15, 13, 1};
    out.buffer_offset = 4 * texel;
    out.row_length = 40;
    out.image_height = 26;
    std::vector<std::uint8_t> strided;
    if (Kit::succeeds(problems, name + " strided image to buffer copy", VulkanImageIo::read(rig, image, out, strided, texel))) {
        std::vector<std::uint8_t> expected(strided.size(), 0xCD);
        copy_block(expected, static_cast<std::size_t>(40) * texel, 0, 0, model[0], 0, static_cast<std::size_t>(width) * texel, 2, 1, 15, 13, texel);
        std::vector<std::uint8_t> shifted(strided.size(), 0xCD);
        for (std::uint32_t y = 0; y < 13; ++y) {
            std::memcpy(shifted.data() + out.buffer_offset + static_cast<std::size_t>(y) * 40 * texel, expected.data() + static_cast<std::size_t>(y) * 40 * texel,
                        static_cast<std::size_t>(15) * texel);
        }
        Kit::same_bytes(context, problems, name + " strided readback including the untouched padding", shifted.data(), strided.data(), strided.size());
    }
}

void check_compatible_pairs(Context& context, VulkanRig& rig, Problems& problems) {
    const struct {
        VkFormat first;
        VkFormat second;
    } pairs[] = {{VK_FORMAT_R32_UINT, VK_FORMAT_R8G8B8A8_UNORM}, {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16_UINT},
                 {VK_FORMAT_R32G32_UINT, VK_FORMAT_R16G16B16A16_UINT}, {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM}};
    for (const auto& pair : pairs) {
        const VulkanFormatInfo* a = VulkanFormats::find(pair.first);
        const VulkanFormatInfo* b = VulkanFormats::find(pair.second);
        if (a == nullptr || b == nullptr || (Kit::format_properties(rig, pair.first).optimalTilingFeatures & kCopyFeatures) != kCopyFeatures ||
            (Kit::format_properties(rig, pair.second).optimalTilingFeatures & kCopyFeatures) != kCopyFeatures) {
            continue;
        }
        const std::string name = VulkanTools::format_name(pair.first) + " to " + VulkanTools::format_name(pair.second);
        VulkanImage from;
        VulkanImage to;
        if (!Kit::succeeds(problems, name + " source image", from.create_2d(rig, pair.first, 16, 16, kCopyUsage)) ||
            !Kit::succeeds(problems, name + " destination image", to.create_2d(rig, pair.second, 16, 16, kCopyUsage)) ||
            !Kit::succeeds(problems, name + " layouts", VulkanImageIo::to_general(rig, from)) || !Kit::succeeds(problems, name + " layouts", VulkanImageIo::to_general(rig, to))) {
            continue;
        }
        ImageRegion region;
        region.extent = {16, 16, 1};
        const std::vector<std::uint8_t> data = pattern_for(*a, 0x4000, 256);
        VkImageCopy copy{};
        copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.extent = {16, 16, 1};
        std::vector<std::uint8_t> back;
        if (Kit::succeeds(problems, name + " upload", VulkanImageIo::write(rig, from, region, data)) &&
            Kit::succeeds(problems, name + " copy", image_copy(rig, from, to, copy)) &&
            Kit::succeeds(problems, name + " readback", VulkanImageIo::read(rig, to, region, back, b->texel))) {
            std::vector<std::uint8_t> expected = data;
            if (pair.second == VK_FORMAT_B8G8R8A8_UNORM) {
                expected = data;
            }
            Kit::same_bytes(context, problems, name + " size-compatible copy keeps the bytes", expected.data(), back.data(), expected.size());
        }
    }
}

}  // namespace

Outcome TransferChecks::copy_all_forms(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        check_buffer_copies(context, rig, problems);
        std::size_t formats = 0;
        for (const VulkanFormatInfo& info : VulkanFormats::all()) {
            if ((Kit::format_properties(rig, info.format).optimalTilingFeatures & kCopyFeatures) != kCopyFeatures) {
                context.note(VulkanTools::format_name(info.format) + " does not report both transfer features; skipped");
                continue;
            }
            check_format(context, rig, info, problems);
            ++formats;
        }
        check_compatible_pairs(context, rig, problems);
        context.measure("vk.copy_formats", formats);
        return {};
    });
}

namespace {

struct Texels {
    std::uint32_t width;
    std::uint32_t height;
    std::vector<std::uint8_t> data;

    [[nodiscard]] const std::uint8_t* at(std::int64_t x, std::int64_t y) const {
        x = std::min<std::int64_t>(std::max<std::int64_t>(x, 0), width - 1);
        y = std::min<std::int64_t>(std::max<std::int64_t>(y, 0), height - 1);
        return data.data() + (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4;
    }
};

std::vector<std::uint8_t> linear_resample(const Texels& source, std::uint32_t width, std::uint32_t height) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * height * 4);
    const double sx = static_cast<double>(source.width) / width;
    const double sy = static_cast<double>(source.height) / height;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const double fu = (x + 0.5) * sx - 0.5;
            const double fv = (y + 0.5) * sy - 0.5;
            const std::int64_t x0 = static_cast<std::int64_t>(std::floor(fu));
            const std::int64_t y0 = static_cast<std::int64_t>(std::floor(fv));
            const double ax = fu - static_cast<double>(x0);
            const double ay = fv - static_cast<double>(y0);
            for (int c = 0; c < 4; ++c) {
                const double top = source.at(x0, y0)[c] * (1 - ax) + source.at(x0 + 1, y0)[c] * ax;
                const double bottom = source.at(x0, y0 + 1)[c] * (1 - ax) + source.at(x0 + 1, y0 + 1)[c] * ax;
                out[(static_cast<std::size_t>(y) * width + x) * 4 + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(std::lround(top * (1 - ay) + bottom * ay));
            }
        }
    }
    return out;
}

}  // namespace

Outcome TransferChecks::blit_resolve(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        const VkFormatProperties props = Kit::format_properties(rig, VK_FORMAT_R8G8B8A8_UNORM);
        const VkFormatFeatureFlags blit = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
        if ((props.optimalTilingFeatures & blit) != blit) {
            problems.add("R8G8B8A8_UNORM reports no blit features, which the specification requires");
            return {};
        }
        const bool linear = (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
        const auto source_of = [&](std::uint32_t w, std::uint32_t h, std::uint64_t seed) {
            Texels t{w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h * 4)};
            Pattern::fill(seed, 0, t.data.data(), t.data.size());
            return t;
        };
        auto blit_case = [&](const std::string& name, const Texels& source, std::uint32_t dst_w, std::uint32_t dst_h, const VkImageBlit& region, VkFilter filter,
                             const std::vector<std::uint8_t>& expected, int tolerance, VkFormat dst_format) {
            VulkanImage from;
            VulkanImage to;
            if (!Kit::succeeds(problems, name + " source image", from.create_2d(rig, VK_FORMAT_R8G8B8A8_UNORM, source.width, source.height, kCopyUsage)) ||
                !Kit::succeeds(problems, name + " destination image", to.create_2d(rig, dst_format, dst_w, dst_h, kCopyUsage)) ||
                !Kit::succeeds(problems, name + " source layout", VulkanImageIo::to_general(rig, from)) ||
                !Kit::succeeds(problems, name + " destination layout", VulkanImageIo::to_general(rig, to))) {
                return;
            }
            ImageRegion whole;
            whole.extent = {source.width, source.height, 1};
            ImageRegion target;
            target.extent = {dst_w, dst_h, 1};
            const std::vector<std::uint8_t> fill(static_cast<std::size_t>(dst_w) * dst_h * 4, 0xCD);
            if (!Kit::succeeds(problems, name + " source upload", VulkanImageIo::write(rig, from, whole, source.data)) ||
                !Kit::succeeds(problems, name + " destination fill", VulkanImageIo::write(rig, to, target, fill))) {
                return;
            }
            const VkResult blitted = rig.run([&](VkCommandBuffer commands) {
                rig.fn.vkCmdBlitImage(commands, from.image, VK_IMAGE_LAYOUT_GENERAL, to.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region, filter);
                VkMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
            });
            std::vector<std::uint8_t> back;
            if (!Kit::succeeds(problems, name + " blit", blitted) || !Kit::succeeds(problems, name + " readback", VulkanImageIo::read(rig, to, target, back, 4))) {
                return;
            }
            if (tolerance == 0) {
                Kit::same_bytes(context, problems, name, expected.data(), back.data(), expected.size());
                return;
            }
            for (std::size_t i = 0; i < expected.size(); ++i) {
                if (std::abs(static_cast<int>(expected[i]) - static_cast<int>(back[i])) > tolerance) {
                    problems.add(name + ": byte " + std::to_string(i) + " is " + std::to_string(back[i]) + ", expected " + std::to_string(expected[i]) + " within " +
                                 std::to_string(tolerance));
                    ArtifactWriter writer(context);
                    writer.bytes(Kit::sanitize(name) + ".expected", expected.data(), expected.size());
                    writer.bytes(Kit::sanitize(name) + ".actual", back.data(), back.size());
                    return;
                }
            }
        };
        const Texels sixteen = source_of(16, 16, 0xB117);
        const auto nearest = [&](const Texels& source, std::uint32_t w, std::uint32_t h, auto mapper) {
            std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4);
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    const std::uint8_t* pixel = mapper(source, x, y);
                    std::memcpy(out.data() + (static_cast<std::size_t>(y) * w + x) * 4, pixel, 4);
                }
            }
            return out;
        };
        const auto region = [](VkOffset3D s0, VkOffset3D s1, VkOffset3D d0, VkOffset3D d1) {
            VkImageBlit r{};
            r.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            r.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            r.srcOffsets[0] = s0;
            r.srcOffsets[1] = s1;
            r.dstOffsets[0] = d0;
            r.dstOffsets[1] = d1;
            return r;
        };
        blit_case("nearest 1:1 blit", sixteen, 16, 16, region({0, 0, 0}, {16, 16, 1}, {0, 0, 0}, {16, 16, 1}), VK_FILTER_NEAREST,
                  nearest(sixteen, 16, 16, [](const Texels& t, std::uint32_t x, std::uint32_t y) { return t.at(x, y); }), 0, VK_FORMAT_R8G8B8A8_UNORM);
        const Texels eight = source_of(8, 8, 0xB118);
        blit_case("nearest 2x upscale", eight, 16, 16, region({0, 0, 0}, {8, 8, 1}, {0, 0, 0}, {16, 16, 1}), VK_FILTER_NEAREST,
                  nearest(eight, 16, 16, [](const Texels& t, std::uint32_t x, std::uint32_t y) { return t.at(x / 2, y / 2); }), 0, VK_FORMAT_R8G8B8A8_UNORM);
        const Texels big = source_of(24, 24, 0xB119);
        blit_case("nearest 3:1 downscale", big, 8, 8, region({0, 0, 0}, {24, 24, 1}, {0, 0, 0}, {8, 8, 1}), VK_FILTER_NEAREST,
                  nearest(big, 8, 8, [](const Texels& t, std::uint32_t x, std::uint32_t y) { return t.at(3 * x + 1, 3 * y + 1); }), 0, VK_FORMAT_R8G8B8A8_UNORM);
        blit_case("horizontal flip", sixteen, 16, 16, region({0, 0, 0}, {16, 16, 1}, {16, 0, 0}, {0, 16, 1}), VK_FILTER_NEAREST,
                  nearest(sixteen, 16, 16, [](const Texels& t, std::uint32_t x, std::uint32_t y) { return t.at(15 - x, y); }), 0, VK_FORMAT_R8G8B8A8_UNORM);
        blit_case("vertical flip", sixteen, 16, 16, region({0, 16, 0}, {16, 0, 1}, {0, 0, 0}, {16, 16, 1}), VK_FILTER_NEAREST,
                  nearest(sixteen, 16, 16, [](const Texels& t, std::uint32_t x, std::uint32_t y) { return t.at(x, 15 - y); }), 0, VK_FORMAT_R8G8B8A8_UNORM);
        blit_case("flip in both directions", sixteen, 16, 16, region({0, 0, 0}, {16, 16, 1}, {16, 16, 0}, {0, 0, 1}), VK_FILTER_NEAREST,
                  nearest(sixteen, 16, 16, [](const Texels& t, std::uint32_t x, std::uint32_t y) { return t.at(15 - x, 15 - y); }), 0, VK_FORMAT_R8G8B8A8_UNORM);
        blit_case("cropped region blit", sixteen, 8, 8, region({4, 4, 0}, {12, 12, 1}, {0, 0, 0}, {8, 8, 1}), VK_FILTER_NEAREST,
                  nearest(sixteen, 8, 8, [](const Texels& t, std::uint32_t x, std::uint32_t y) { return t.at(4 + x, 4 + y); }), 0, VK_FORMAT_R8G8B8A8_UNORM);
        const VkFormatProperties bgra = Kit::format_properties(rig, VK_FORMAT_B8G8R8A8_UNORM);
        if ((bgra.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0) {
            std::vector<std::uint8_t> expected = sixteen.data;
            for (std::size_t i = 0; i < expected.size(); i += 4) {
                std::swap(expected[i], expected[i + 2]);
            }
            blit_case("RGBA to BGRA blit swizzles", sixteen, 16, 16, region({0, 0, 0}, {16, 16, 1}, {0, 0, 0}, {16, 16, 1}), VK_FILTER_NEAREST, expected, 0,
                      VK_FORMAT_B8G8R8A8_UNORM);
        }
        if (linear) {
            blit_case("linear 2:1 downscale", sixteen, 8, 8, region({0, 0, 0}, {16, 16, 1}, {0, 0, 0}, {8, 8, 1}), VK_FILTER_LINEAR, linear_resample(sixteen, 8, 8), 1,
                      VK_FORMAT_R8G8B8A8_UNORM);
            blit_case("linear 1:2 upscale", eight, 16, 16, region({0, 0, 0}, {8, 8, 1}, {0, 0, 0}, {16, 16, 1}), VK_FILTER_LINEAR, linear_resample(eight, 16, 16), 1,
                      VK_FORMAT_R8G8B8A8_UNORM);
        } else {
            context.note("R8G8B8A8_UNORM does not report linear filtering; linear blits skipped");
        }
        const VkSampleCountFlags samples = rig.props.limits.framebufferColorSampleCounts;
        VkImageFormatProperties limits{};
        if ((samples & VK_SAMPLE_COUNT_4_BIT) == 0 ||
            !Kit::image_supported(rig, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0, limits) ||
            (limits.sampleCounts & VK_SAMPLE_COUNT_4_BIT) == 0) {
            context.note("4 samples are not available for R8G8B8A8_UNORM; resolve skipped");
            return {};
        }
        const std::uint32_t side = 16;
        VulkanImage multisampled;
        VulkanImage resolved;
        VulkanOwned<VkImageView> view;
        if (!Kit::succeeds(problems, "multisample image", multisampled.create_2d(rig, VK_FORMAT_R8G8B8A8_UNORM, side, side,
                                                                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_SAMPLE_COUNT_4_BIT)) ||
            !Kit::succeeds(problems, "resolve target", resolved.create_2d(rig, VK_FORMAT_R8G8B8A8_UNORM, side, side, kCopyUsage)) ||
            !Kit::succeeds(problems, "multisample view", multisampled.view(view, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT))) {
            return {};
        }
        VkAttachmentDescription attachment{};
        attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
        attachment.samples = VK_SAMPLE_COUNT_4_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        const VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &reference;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = 0;
        dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
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
        VkImageView view_handle = view.get();
        VulkanOwned<VkFramebuffer> framebuffer;
        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = &view_handle;
        framebuffer_info.width = side;
        framebuffer_info.height = side;
        framebuffer_info.layers = 1;
        if (!Kit::succeeds(problems, "resolve render pass", rig.make(pass, rig.fn.vkCreateRenderPass, rig.fn.vkDestroyRenderPass, pass_info))) {
            return {};
        }
        framebuffer_info.renderPass = pass.get();
        if (!Kit::succeeds(problems, "resolve framebuffer", rig.make(framebuffer, rig.fn.vkCreateFramebuffer, rig.fn.vkDestroyFramebuffer, framebuffer_info)) ||
            !Kit::succeeds(problems, "resolve target layout", VulkanImageIo::to_general(rig, resolved))) {
            return {};
        }
        VkClearValue clear{};
        clear.color.float32[0] = 0.2f;
        clear.color.float32[1] = 0.4f;
        clear.color.float32[2] = 0.6f;
        clear.color.float32[3] = 0.8f;
        const VkImageResolve resolve{{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {side, side, 1}};
        const VkResult ran = rig.run([&](VkCommandBuffer commands) {
            VkRenderPassBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            begin.renderPass = pass.get();
            begin.framebuffer = framebuffer.get();
            begin.renderArea = {{0, 0}, {side, side}};
            begin.clearValueCount = 1;
            begin.pClearValues = &clear;
            rig.fn.vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
            rig.fn.vkCmdEndRenderPass(commands);
            rig.fn.vkCmdResolveImage(commands, multisampled.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, resolved.image, VK_IMAGE_LAYOUT_GENERAL, 1, &resolve);
            VkMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        });
        ImageRegion whole;
        whole.extent = {side, side, 1};
        std::vector<std::uint8_t> back;
        if (Kit::succeeds(problems, "resolve of a cleared 4-sample image", ran) && Kit::succeeds(problems, "resolve readback", VulkanImageIo::read(rig, resolved, whole, back, 4))) {
            std::vector<std::uint8_t> expected;
            for (std::uint32_t i = 0; i < side * side; ++i) {
                expected.insert(expected.end(), {51, 102, 153, 204});
            }
            Kit::same_bytes(context, problems, "ResolveImage of a uniform multisample image", expected.data(), back.data(), expected.size());
        }
        return {};
    });
}

Outcome TransferChecks::clear_fill_update(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        {
            const std::uint32_t side = 16;
            const std::uint32_t mips = 3;
            const std::uint32_t layers = 3;
            VulkanImage image;
            if (Kit::succeeds(problems, "subresource clear image", image.create_2d(rig, VK_FORMAT_R8G8B8A8_UNORM, side, side, kCopyUsage, VK_SAMPLE_COUNT_1_BIT, mips, layers)) &&
                Kit::succeeds(problems, "subresource clear layout", VulkanImageIo::to_general(rig, image))) {
                std::vector<std::vector<std::uint8_t>> model;
                std::vector<ImageRegion> regions;
                bool ready = true;
                for (std::uint32_t mip = 0; mip < mips && ready; ++mip) {
                    for (std::uint32_t layer = 0; layer < layers && ready; ++layer) {
                        ImageRegion region;
                        region.mip = mip;
                        region.layer = layer;
                        region.extent = {side >> mip, side >> mip, 1};
                        std::vector<std::uint8_t> data(VulkanImageIo::region_bytes(region, 4));
                        Pattern::fill(0x6000 + mip * 8 + layer, 0, data.data(), data.size());
                        ready = Kit::succeeds(problems, "initial write", VulkanImageIo::write(rig, image, region, data));
                        regions.push_back(region);
                        model.push_back(std::move(data));
                    }
                }
                VkClearColorValue value{};
                value.float32[0] = 0.2f;
                value.float32[1] = 0.4f;
                value.float32[2] = 0.6f;
                value.float32[3] = 0.8f;
                const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 1, 2};
                const VkResult cleared = rig.run([&](VkCommandBuffer commands) {
                    rig.fn.vkCmdClearColorImage(commands, image.image, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
                    VkMemoryBarrier barrier{};
                    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
                });
                if (ready && Kit::succeeds(problems, "ClearColorImage of one mip and two layers", cleared)) {
                    for (std::size_t i = 0; i < regions.size(); ++i) {
                        std::vector<std::uint8_t> expected = model[i];
                        if (regions[i].mip == 1 && regions[i].layer >= 1) {
                            for (std::size_t p = 0; p < expected.size(); p += 4) {
                                expected[p] = 51;
                                expected[p + 1] = 102;
                                expected[p + 2] = 153;
                                expected[p + 3] = 204;
                            }
                        }
                        std::vector<std::uint8_t> back;
                        const std::string where = "mip " + std::to_string(regions[i].mip) + " layer " + std::to_string(regions[i].layer);
                        if (Kit::succeeds(problems, "read of " + where, VulkanImageIo::read(rig, image, regions[i], back, 4))) {
                            Kit::same_bytes(context, problems, "range clear leaves " + where + " exactly as specified", expected.data(), back.data(), expected.size());
                        }
                    }
                }
            }
        }
        std::uint32_t seed = 40;
        for (const VulkanFormatInfo& info : VulkanFormats::all()) {
            const bool depth = info.kind == VulkanFormatKind::Depth || info.kind == VulkanFormatKind::Stencil;
            if ((Kit::format_properties(rig, info.format).optimalTilingFeatures & kCopyFeatures) != kCopyFeatures) {
                continue;
            }
            const std::string name = VulkanTools::format_name(info.format) + (depth ? " ClearDepthStencilImage" : " ClearColorImage");
            const std::uint32_t side = 8;
            VulkanImage image;
            if (!Kit::succeeds(problems, name + " image", image.create_2d(rig, info.format, side, side, kCopyUsage)) ||
                !Kit::succeeds(problems, name + " layout", VulkanImageIo::to_general(rig, image))) {
                continue;
            }
            ImageRegion region;
            region.aspect = VulkanFormats::aspect(info);
            region.extent = {side, side, 1};
            std::vector<std::uint8_t> pixel;
            VkResult cleared = VK_SUCCESS;
            const VkImageSubresourceRange range{region.aspect, 0, 1, 0, 1};
            if (!depth) {
                const VkClearColorValue value = VulkanFormats::clear_value(info, seed++);
                pixel = VulkanFormats::encode_clear(info, value);
                cleared = rig.run([&](VkCommandBuffer commands) { rig.fn.vkCmdClearColorImage(commands, image.image, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range); });
            } else {
                VkClearDepthStencilValue value{};
                if (info.kind == VulkanFormatKind::Stencil) {
                    value.stencil = 0x77;
                    pixel = {0x77};
                } else if (info.texel == 4) {
                    value.depth = 0.75f;
                    pixel.resize(4);
                    std::memcpy(pixel.data(), &value.depth, 4);
                } else {
                    value.depth = 0.25f;
                    pixel = {0x00, 0x40};
                }
                cleared = rig.run([&](VkCommandBuffer commands) { rig.fn.vkCmdClearDepthStencilImage(commands, image.image, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range); });
            }
            std::vector<std::uint8_t> back;
            if (Kit::succeeds(problems, name, cleared) && Kit::succeeds(problems, name + " readback", VulkanImageIo::read(rig, image, region, back, info.texel))) {
                std::vector<std::uint8_t> expected;
                for (std::uint32_t i = 0; i < side * side; ++i) {
                    expected.insert(expected.end(), pixel.begin(), pixel.end());
                }
                Kit::same_bytes(context, problems, name, expected.data(), back.data(), expected.size());
            }
        }
        const VkDeviceSize size = 70000;
        VulkanBuffer buffer;
        VulkanBuffer readback;
        if (!Kit::succeeds(problems, "fill buffer", Kit::host_buffer(rig, buffer, size)) || !Kit::succeeds(problems, "update buffer", Kit::host_buffer(rig, readback, size))) {
            return {};
        }
        const auto run_and_check = [&](const std::string& name, std::vector<std::uint8_t> expected, const std::function<void(VkCommandBuffer)>& record) {
            buffer.fill_pattern(0x7000);
            buffer.publish();
            std::vector<std::uint8_t> before(buffer.bytes(), buffer.bytes() + size);
            const VkResult result = rig.run([&](VkCommandBuffer commands) {
                record(commands);
                VkMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
            });
            if (Kit::succeeds(problems, name, result)) {
                buffer.acquire();
                Kit::same_bytes(context, problems, name, expected.data(), buffer.bytes(), expected.size());
            }
        };
        std::vector<std::uint8_t> base(size);
        Pattern::fill(0x7000, 0, base.data(), base.size());
        {
            std::vector<std::uint8_t> expected = base;
            for (std::size_t i = 256; i < 1280 && i < expected.size(); ++i) {
                expected[i] = 0xAB;
            }
            run_and_check("FillBuffer with an offset and size", expected,
                          [&](VkCommandBuffer commands) { rig.fn.vkCmdFillBuffer(commands, buffer.buffer, 256, 1024, 0xABABABABu); });
        }
        {
            std::vector<std::uint8_t> expected = base;
            for (std::size_t i = 60000; i < expected.size(); ++i) {
                expected[i] = 0x12;
            }
            run_and_check("FillBuffer to the end of the buffer", expected,
                          [&](VkCommandBuffer commands) { rig.fn.vkCmdFillBuffer(commands, buffer.buffer, 60000, VK_WHOLE_SIZE, 0x12121212u); });
        }
        const struct {
            VkDeviceSize offset;
            VkDeviceSize length;
        } updates[] = {{0, 4}, {12, 12}, {1000, 256}, {4096, 4096}, {2048, 65536}};
        for (const auto& update : updates) {
            std::vector<std::uint8_t> payload(static_cast<std::size_t>(update.length));
            Pattern::fill(0x7100 + update.length, 0, payload.data(), payload.size());
            std::vector<std::uint8_t> expected = base;
            std::memcpy(expected.data() + update.offset, payload.data(), payload.size());
            run_and_check("UpdateBuffer of " + std::to_string(update.length) + " bytes at " + std::to_string(update.offset), expected, [&](VkCommandBuffer commands) {
                rig.fn.vkCmdUpdateBuffer(commands, buffer.buffer, update.offset, update.length, payload.data());
            });
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::vk
