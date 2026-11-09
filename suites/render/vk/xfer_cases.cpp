#include "render/common/pattern.hpp"
#include "render/vk/quadkit.hpp"

#include <cstring>

namespace lsw::render::vk {

namespace {

CaseSpec spec_of(const std::string& name) {
    CaseSpec spec("xfer", name);
    spec.tolerance(Tolerance::tight(1));
    return spec;
}

Image bytes_image(const std::vector<std::uint8_t>& bytes, std::uint32_t width) {
    const std::uint32_t height = static_cast<std::uint32_t>((bytes.size() + width - 1) / width);
    Image image(width, height, PixelKind::R8Uint);
    std::memcpy(image.bytes().data(), bytes.data(), bytes.size());
    return image;
}

Image rgba_image(const std::vector<std::uint8_t>& bytes, std::uint32_t width, std::uint32_t height) {
    Image image(width, height, PixelKind::Rgba8Unorm);
    std::memcpy(image.bytes().data(), bytes.data(), std::min(bytes.size(), image.bytes().size()));
    return image;
}

std::vector<std::uint8_t> pattern_bytes(std::size_t count, std::uint32_t seed) {
    std::vector<std::uint8_t> bytes(count);
    std::uint32_t state = seed;
    for (std::uint8_t& b : bytes) {
        state = state * 1664525u + 1013904223u;
        b = static_cast<std::uint8_t>(state >> 24);
    }
    return bytes;
}

Texture xfer_image(Rig& rig, std::uint32_t w, std::uint32_t h, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM, std::uint32_t layers = 1, VkImageUsageFlags extra = 0) {
    TextureDesc desc;
    desc.width = w;
    desc.height = h;
    desc.layers = layers;
    desc.format = format;
    desc.view_type = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    desc.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | extra;
    return rig.texture(desc);
}

void upload_full(Rig& rig, const Texture& t, const std::vector<std::uint8_t>& bytes, std::uint32_t layers = 1) {
    std::vector<VkBufferImageCopy> regions;
    const std::size_t layer_bytes = bytes.size() / layers;
    for (std::uint32_t l = 0; l < layers; ++l) {
        VkBufferImageCopy region{};
        region.bufferOffset = layer_bytes * l;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, l, 1};
        region.imageExtent = {t.width, t.height, 1};
        regions.push_back(region);
    }
    std::string error;
    rig.must(rig.kit.upload_texture(t, regions, bytes, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, error), error);
}

constexpr VkImageLayout kSrc = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
constexpr VkImageLayout kDst = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

void register_buffers() {
    Cases::add(spec_of("copy_buffer_regions"), [](Rig& rig, CaptureSet& out) {
        const std::vector<std::uint8_t> data = pattern_bytes(256, 11);
        const Buffer src = rig.buffer(data.data(), data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        const Buffer dst = rig.blank(256, VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
        rig.run([&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdFillBuffer(cb, dst.buffer, 0, 256, 0xA5A5A5A5u);
            rig.barrier_all(cb);
            const VkBufferCopy regions[4] = {{0, 100, 40}, {200, 4, 36}, {61, 61, 17}, {128, 140, 64}};
            rig.kit.api.vkCmdCopyBuffer(cb, src.buffer, dst.buffer, 4, regions);
        });
        out.add("", bytes_image(rig.bytes(dst, 0, 256), 16));
    });
    Cases::add(spec_of("fill_and_update_buffer"), [](Rig& rig, CaptureSet& out) {
        const Buffer dst = rig.blank(512, VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
        rig.run([&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdFillBuffer(cb, dst.buffer, 0, 128, 0x01020304u);
            rig.kit.api.vkCmdFillBuffer(cb, dst.buffer, 128, 384, 0xFFu);
            rig.barrier_all(cb);
            const std::vector<std::uint8_t> patch = pattern_bytes(96, 5);
            rig.kit.api.vkCmdUpdateBuffer(cb, dst.buffer, 20, patch.size(), patch.data());
            rig.barrier_all(cb);
            const std::vector<std::uint8_t> tail = pattern_bytes(64, 9);
            rig.kit.api.vkCmdUpdateBuffer(cb, dst.buffer, 400, tail.size(), tail.data());
        });
        out.add("", bytes_image(rig.bytes(dst, 0, 512), 32));
    });
}

void register_images() {
    Cases::add(spec_of("buffer_to_image_row_length"), [](Rig& rig, CaptureSet& out) {
        Texture t = xfer_image(rig, 32, 32);
        upload_full(rig, t, std::vector<std::uint8_t>(32 * 32 * 4, 0x20));
        const std::vector<std::uint8_t> data = pattern_bytes(24 * 14 * 4, 3);
        const Buffer src = rig.buffer(data.data(), data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        rig.run([&](VkCommandBuffer cb) {
            rig.kit.barrier(cb, t.image, VK_IMAGE_ASPECT_COLOR_BIT, kSrc, kDst);
            VkBufferImageCopy regions[2]{};
            regions[0].bufferOffset = 4 * 24 * 2 + 8;
            regions[0].bufferRowLength = 24;
            regions[0].bufferImageHeight = 14;
            regions[0].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            regions[0].imageOffset = {5, 3, 0};
            regions[0].imageExtent = {13, 9, 1};
            regions[1] = regions[0];
            regions[1].bufferOffset = 4 * 5;
            regions[1].imageOffset = {18, 17, 0};
            regions[1].imageExtent = {11, 12, 1};
            rig.kit.api.vkCmdCopyBufferToImage(cb, src.buffer, t.image, kDst, 2, regions);
            rig.kit.barrier(cb, t.image, VK_IMAGE_ASPECT_COLOR_BIT, kDst, kSrc);
        });
        out.add("", rig.read(t, kSrc));
    });
    Cases::add(spec_of("image_to_buffer_row_length"), [](Rig& rig, CaptureSet& out) {
        Texture t = xfer_image(rig, 32, 32);
        upload_full(rig, t, pattern_bytes(32 * 32 * 4, 17));
        const Buffer dst = rig.blank(40 * 20 * 4 + 64, VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
        rig.run([&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdFillBuffer(cb, dst.buffer, 0, 40 * 20 * 4 + 64, 0x11223344u);
            rig.barrier_all(cb);
            VkBufferImageCopy region{};
            region.bufferOffset = 16;
            region.bufferRowLength = 40;
            region.bufferImageHeight = 20;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageOffset = {7, 5, 0};
            region.imageExtent = {19, 13, 1};
            rig.kit.api.vkCmdCopyImageToBuffer(cb, t.image, kSrc, dst.buffer, 1, &region);
        });
        out.add("", bytes_image(rig.bytes(dst, 0, 40 * 20 * 4 + 64), 40));
    });
    Cases::add(spec_of("copy_image_layers_and_offsets"), [](Rig& rig, CaptureSet& out) {
        Texture a = xfer_image(rig, 32, 32, VK_FORMAT_R8G8B8A8_UNORM, 3);
        Texture b = xfer_image(rig, 32, 32, VK_FORMAT_R8G8B8A8_UNORM, 3);
        upload_full(rig, a, pattern_bytes(32 * 32 * 4 * 3, 21), 3);
        upload_full(rig, b, pattern_bytes(32 * 32 * 4 * 3, 22), 3);
        rig.run([&](VkCommandBuffer cb) {
            rig.kit.barrier(cb, b.image, VK_IMAGE_ASPECT_COLOR_BIT, kSrc, kDst, 1, 3);
            VkImageCopy regions[3]{};
            regions[0].srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 1};
            regions[0].dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 2, 1};
            regions[0].extent = {32, 32, 1};
            regions[1].srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 2};
            regions[1].srcOffset = {3, 5, 0};
            regions[1].dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 2};
            regions[1].dstOffset = {11, 2, 0};
            regions[1].extent = {17, 13, 1};
            rig.kit.api.vkCmdCopyImage(cb, a.image, kSrc, b.image, kDst, 2, regions);
            rig.kit.barrier(cb, b.image, VK_IMAGE_ASPECT_COLOR_BIT, kDst, kSrc, 1, 3);
        });
        for (std::uint32_t l = 0; l < 3; ++l) {
            out.add("layer" + std::to_string(l), rig.read(b, kSrc, VK_IMAGE_ASPECT_COLOR_BIT, l));
        }
    });
    Cases::add(spec_of("copy_image_3d_box"), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        const std::uint32_t w = 16, h = 16, d = 8;
        TextureDesc desc;
        desc.type = VK_IMAGE_TYPE_3D;
        desc.view_type = VK_IMAGE_VIEW_TYPE_3D;
        desc.width = w;
        desc.height = h;
        desc.depth = d;
        desc.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        desc.create_view = false;
        Texture a = rig.texture(desc);
        Texture b = rig.texture(desc);
        const auto fill_a = pattern_bytes(w * h * d * 4, 31);
        const auto fill_b = pattern_bytes(w * h * d * 4, 32);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {w, h, d};
        std::string error;
        rig.must(kit.upload_texture(a, {region}, fill_a, kSrc, error), error);
        rig.must(kit.upload_texture(b, {region}, fill_b, kDst, error), error);
        const Buffer dst = rig.blank(w * h * d * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
        rig.run([&](VkCommandBuffer cb) {
            VkImageCopy copy{};
            copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.srcOffset = {4, 4, 2};
            copy.dstOffset = {2, 3, 1};
            copy.extent = {8, 6, 4};
            kit.api.vkCmdCopyImage(cb, a.image, kSrc, b.image, kDst, 1, &copy);
            kit.barrier(cb, b.image, VK_IMAGE_ASPECT_COLOR_BIT, kDst, kSrc);
            kit.api.vkCmdCopyImageToBuffer(cb, b.image, kSrc, dst.buffer, 1, &region);
        });
        out.add("", rgba_image(rig.bytes(dst, 0, w * h * d * 4), w, h * d));
    });
    CaseSpec bc = spec_of("bc1_block_regions");
    bc.feature("textureCompressionBC");
    Cases::add(std::move(bc), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        Texture a = xfer_image(rig, 16, 16, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
        Texture b = xfer_image(rig, 16, 16, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
        const auto blocks = pattern_bytes(16 * 8, 41);
        VkBufferImageCopy full{};
        full.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        full.imageExtent = {16, 16, 1};
        std::string error;
        rig.must(kit.upload_texture(a, {full}, blocks, kSrc, error), error);
        rig.must(kit.upload_texture(b, {full}, std::vector<std::uint8_t>(16 * 8, 0), kDst, error), error);
        const Buffer dst = rig.blank(16 * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
        rig.run([&](VkCommandBuffer cb) {
            VkImageCopy copy{};
            copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.srcOffset = {4, 8, 0};
            copy.dstOffset = {8, 0, 0};
            copy.extent = {8, 8, 1};
            kit.api.vkCmdCopyImage(cb, a.image, kSrc, b.image, kDst, 1, &copy);
            kit.barrier(cb, b.image, VK_IMAGE_ASPECT_COLOR_BIT, kDst, kSrc);
            kit.api.vkCmdCopyImageToBuffer(cb, b.image, kSrc, dst.buffer, 1, &full);
        });
        out.add("", bytes_image(rig.bytes(dst, 0, 16 * 8), 8));
    }, [](Kit& kit) { return Cases::need_format(kit, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT, "BC1 transfer"); });
}

struct BlitSpec {
    const char* name;
    VkFilter filter;
    std::uint32_t src;
    std::uint32_t dst;
    bool mirror;
    VkFormat dst_format;
};

void register_blits() {
    const std::vector<BlitSpec> blits = {
        {"blit_linear_upscale", VK_FILTER_LINEAR, 16, 64, false, VK_FORMAT_R8G8B8A8_UNORM},
        {"blit_nearest_downscale", VK_FILTER_NEAREST, 64, 20, false, VK_FORMAT_R8G8B8A8_UNORM},
        {"blit_mirrored_regions", VK_FILTER_LINEAR, 32, 48, true, VK_FORMAT_R8G8B8A8_UNORM},
        {"blit_format_conversion_bgra", VK_FILTER_LINEAR, 32, 32, false, VK_FORMAT_B8G8R8A8_UNORM},
    };
    for (const BlitSpec& entry : blits) {
        Cases::add(spec_of(entry.name), [entry](Rig& rig, CaptureSet& out) {
            Kit& kit = rig.kit;
            Texture a = xfer_image(rig, entry.src, entry.src);
            Texture b = xfer_image(rig, entry.dst, entry.dst, entry.dst_format);
            upload_full(rig, a, Pattern::checker(entry.src, entry.src, std::max(2u, entry.src / 8), Pattern::pack(240, 120, 30), Pattern::pack(20, 90, 200)));
            std::string error;
            VkBufferImageCopy full{};
            full.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            full.imageExtent = {entry.dst, entry.dst, 1};
            rig.must(kit.upload_texture(b, {full}, std::vector<std::uint8_t>(entry.dst * entry.dst * 4, 0x30), kDst, error), error);
            rig.run([&](VkCommandBuffer cb) {
                VkImageBlit blit{};
                blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                const std::int32_t s = static_cast<std::int32_t>(entry.src);
                const std::int32_t d = static_cast<std::int32_t>(entry.dst);
                blit.srcOffsets[0] = {entry.mirror ? s - 3 : 1, 2, 0};
                blit.srcOffsets[1] = {entry.mirror ? 3 : s - 2, s - 3, 1};
                blit.dstOffsets[0] = {2, entry.mirror ? d - 2 : 1, 0};
                blit.dstOffsets[1] = {d - 3, entry.mirror ? 2 : d - 2, 1};
                kit.api.vkCmdBlitImage(cb, a.image, kSrc, b.image, kDst, 1, &blit, entry.filter);
                kit.barrier(cb, b.image, VK_IMAGE_ASPECT_COLOR_BIT, kDst, kSrc);
            });
            out.add("", rig.read(b, kSrc));
        }, [entry](Kit& kit) {
            return Cases::first({Cases::need_format(kit, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT, "blit source"),
                                 Cases::need_format(kit, entry.dst_format, VK_FORMAT_FEATURE_BLIT_DST_BIT, "blit destination")});
        });
    }
}

void register_clears() {
    Cases::add(spec_of("clear_color_image_ranges").uniform_ok(), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        Texture t = xfer_image(rig, 16, 16, VK_FORMAT_R8G8B8A8_UNORM, 4);
        upload_full(rig, t, std::vector<std::uint8_t>(16 * 16 * 4 * 4, 0x40), 4);
        rig.run([&](VkCommandBuffer cb) {
            kit.barrier(cb, t.image, VK_IMAGE_ASPECT_COLOR_BIT, kSrc, kDst, 1, 4);
            const VkClearColorValue a = {{1.0f, 0.2f, 0.1f, 1.0f}};
            const VkClearColorValue b = {{0.1f, 0.9f, 0.5f, 0.5f}};
            const VkImageSubresourceRange r1{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 1, 1};
            const VkImageSubresourceRange r2{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 2, 2};
            kit.api.vkCmdClearColorImage(cb, t.image, kDst, &a, 1, &r1);
            kit.api.vkCmdClearColorImage(cb, t.image, kDst, &b, 1, &r2);
            kit.barrier(cb, t.image, VK_IMAGE_ASPECT_COLOR_BIT, kDst, kSrc, 1, 4);
        });
        for (std::uint32_t l = 0; l < 4; ++l) {
            out.add("layer" + std::to_string(l), rig.read(t, kSrc, VK_IMAGE_ASPECT_COLOR_BIT, l));
        }
    }, {});
    Cases::add(spec_of("clear_depth_stencil_image").uniform_ok(), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        TextureDesc desc;
        desc.width = desc.height = 16;
        desc.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
        desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        desc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        desc.create_view = false;
        const Texture t = rig.texture(desc);
        rig.run([&](VkCommandBuffer cb) {
            kit.barrier(cb, t.image, desc.aspect, VK_IMAGE_LAYOUT_UNDEFINED, kDst);
            const VkClearDepthStencilValue value{0.25f, 7};
            const VkImageSubresourceRange range{desc.aspect, 0, 1, 0, 1};
            kit.api.vkCmdClearDepthStencilImage(cb, t.image, kDst, &value, 1, &range);
            const VkClearDepthStencilValue depth_only{0.75f, 0};
            const VkImageSubresourceRange depth_range{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            kit.api.vkCmdClearDepthStencilImage(cb, t.image, kDst, &depth_only, 1, &depth_range);
            kit.barrier(cb, t.image, desc.aspect, kDst, kSrc);
        });
        out.add("depth", rig.read(t, kSrc, VK_IMAGE_ASPECT_DEPTH_BIT));
        out.add("stencil", rig.read(t, kSrc, VK_IMAGE_ASPECT_STENCIL_BIT));
    }, [](Kit& kit) { return Cases::need_format(kit, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_FEATURE_TRANSFER_DST_BIT, "D32S8 transfer"); });
    Cases::add(spec_of("clear_attachments_rects"), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        TargetDesc td;
        td.depth = VK_FORMAT_D32_SFLOAT_S8_UINT;
        Target& target = rig.make_target(td);
        target.clear_color[0] = 0.2f;
        target.clear_color[1] = 0.2f;
        target.clear_color[2] = 0.3f;
        QuadKit quads(rig);
        const VkPipeline noop = rig.pipe(quads.desc(target));
        out.add("color", rig.draw(target, noop, nullptr, [&](VkCommandBuffer cb) {
            VkClearAttachment color{};
            color.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            color.colorAttachment = 0;
            color.clearValue.color = {{0.9f, 0.3f, 0.2f, 1.0f}};
            VkClearAttachment stencil{};
            stencil.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT | VK_IMAGE_ASPECT_DEPTH_BIT;
            stencil.clearValue.depthStencil = {0.5f, 9};
            VkClearRect rects[3]{};
            rects[0] = {{{10, 12}, {50, 30}}, 0, 1};
            rects[1] = {{{70, 40}, {40, 60}}, 0, 1};
            rects[2] = {{{0, 100}, {128, 14}}, 0, 1};
            kit.api.vkCmdClearAttachments(cb, 1, &color, 3, rects);
            kit.api.vkCmdClearAttachments(cb, 1, &stencil, 2, rects);
        }));
        Image stencil_image;
        std::string error;
        rig.must(target.read_stencil(kit, stencil_image, error), error);
        out.add("stencil", std::move(stencil_image));
    }, {});
}

void register_copy2() {
    CaseSpec spec = spec_of("copy_commands2_chain");
    spec.min_version(1, 3);
    Cases::add(std::move(spec), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        const auto data = pattern_bytes(32 * 32 * 4, 55);
        const Buffer src = rig.buffer(data.data(), data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        const Buffer mid = rig.blank(data.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);
        Texture a = xfer_image(rig, 32, 32);
        Texture b = xfer_image(rig, 32, 32);
        Texture c = xfer_image(rig, 16, 16);
        std::string error;
        rig.must(kit.upload_texture(b, {VkBufferImageCopy{0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {32, 32, 1}}}, std::vector<std::uint8_t>(32 * 32 * 4, 0), kDst, error), error);
        rig.must(kit.upload_texture(a, {VkBufferImageCopy{0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {32, 32, 1}}}, std::vector<std::uint8_t>(32 * 32 * 4, 0), kDst, error), error);
        rig.must(kit.upload_texture(c, {VkBufferImageCopy{0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {16, 16, 1}}}, std::vector<std::uint8_t>(16 * 16 * 4, 0), kDst, error), error);
        rig.run([&](VkCommandBuffer cb) {
            VkBufferCopy2 bc{VK_STRUCTURE_TYPE_BUFFER_COPY_2, nullptr, 0, 0, data.size()};
            VkCopyBufferInfo2 cbi{VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2, nullptr, src.buffer, mid.buffer, 1, &bc};
            kit.api.vkCmdCopyBuffer2(cb, &cbi);
            rig.barrier_all(cb);
            VkBufferImageCopy2 bi{};
            bi.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
            bi.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            bi.imageExtent = {32, 32, 1};
            VkCopyBufferToImageInfo2 b2i{VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2, nullptr, mid.buffer, a.image, kDst, 1, &bi};
            kit.api.vkCmdCopyBufferToImage2(cb, &b2i);
            kit.barrier(cb, a.image, VK_IMAGE_ASPECT_COLOR_BIT, kDst, kSrc);
            VkImageCopy2 ic{};
            ic.sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
            ic.srcSubresource = ic.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            ic.srcOffset = {6, 4, 0};
            ic.dstOffset = {3, 9, 0};
            ic.extent = {20, 15, 1};
            VkCopyImageInfo2 cii{VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2, nullptr, a.image, kSrc, b.image, kDst, 1, &ic};
            kit.api.vkCmdCopyImage2(cb, &cii);
            kit.barrier(cb, b.image, VK_IMAGE_ASPECT_COLOR_BIT, kDst, kSrc);
            VkImageBlit2 blit{};
            blit.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
            blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.srcOffsets[1] = {32, 32, 1};
            blit.dstOffsets[1] = {16, 16, 1};
            VkBlitImageInfo2 bii{VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, nullptr, b.image, kSrc, c.image, kDst, 1, &blit, VK_FILTER_LINEAR};
            kit.api.vkCmdBlitImage2(cb, &bii);
            kit.barrier(cb, c.image, VK_IMAGE_ASPECT_COLOR_BIT, kDst, kSrc);
        });
        out.add("copied", rig.read(b, kSrc));
        out.add("blitted", rig.read(c, kSrc));
    });
    CaseSpec host = spec_of("host_image_copy_roundtrip");
    host.extension("VK_EXT_host_image_copy").min_version(1, 3);
    Cases::add(
        std::move(host),
        [](Rig& rig, CaptureSet& out) {
            Kit& kit = rig.kit;
            TextureDesc desc;
            desc.width = desc.height = 32;
            desc.usage = VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            desc.create_view = false;
            const Texture t = rig.texture(desc);
            const auto data = pattern_bytes(32 * 32 * 4, 61);
            VkHostImageLayoutTransitionInfoEXT transition{};
            transition.sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO_EXT;
            transition.image = t.image;
            transition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            transition.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            transition.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            rig.vk_ok(kit.api.vkTransitionImageLayoutEXT(kit.device, 1, &transition), "vkTransitionImageLayoutEXT");
            VkMemoryToImageCopyEXT region{};
            region.sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_EXT;
            region.pHostPointer = data.data();
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageOffset = {2, 3, 0};
            region.imageExtent = {28, 25, 1};
            region.memoryRowLength = 32;
            region.memoryImageHeight = 32;
            VkCopyMemoryToImageInfoEXT info{};
            info.sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_EXT;
            info.dstImage = t.image;
            info.dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
            info.regionCount = 1;
            info.pRegions = &region;
            rig.vk_ok(kit.api.vkCopyMemoryToImageEXT(kit.device, &info), "vkCopyMemoryToImageEXT");
            out.add("", rig.read(t, VK_IMAGE_LAYOUT_GENERAL));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceHostImageCopyFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT);
            return f.hostImageCopy != VK_FALSE ? std::string() : std::string("hostImageCopy");
        });
}

const int registered = (register_buffers(), register_images(), register_blits(), register_clears(), register_copy2(), 0);

}  // namespace

}  // namespace lsw::render::vk
