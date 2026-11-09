#include "render/common/pattern.hpp"
#include "render/vk/rig.hpp"

namespace lsw::render::vk {

namespace {

std::vector<std::uint8_t> layer_pattern(unsigned w, unsigned h, unsigned layer) {
    const std::uint32_t a = Pattern::pack(40 + 50 * (layer % 5), 220 - 30 * (layer % 4), 60 + 45 * (layer % 3));
    const std::uint32_t b = Pattern::pack(230 - 35 * (layer % 6), 70 + 25 * (layer % 7), 200 - 20 * (layer % 5));
    std::vector<std::uint8_t> bytes = Pattern::checker(w, h, std::max(1u, w / 4), a, b);
    for (unsigned x = 0; x < w; ++x) {
        for (unsigned y = 0; y < std::min(h, 2u); ++y) {
            const std::size_t at = (static_cast<std::size_t>(y) * w + x) * 4;
            bytes[at] = static_cast<std::uint8_t>(255 * x / std::max(1u, w - 1));
        }
    }
    return bytes;
}

struct TypeSetup {
    VkImageType type = VK_IMAGE_TYPE_2D;
    VkImageViewType view = VK_IMAGE_VIEW_TYPE_2D;
    unsigned w = 16;
    unsigned h = 16;
    unsigned d = 1;
    unsigned layers = 1;
    VkImageCreateFlags flags = 0;
    const char* shader = "t2da.frag";
    VkFilter filter = VK_FILTER_NEAREST;
};

void run_type(Rig& rig, CaptureSet& out, const TypeSetup& setup) {
    Target& target = rig.make_target(TargetDesc{});
    TextureDesc desc;
    desc.type = setup.type;
    desc.view_type = setup.view;
    desc.width = setup.w;
    desc.height = setup.h;
    desc.depth = setup.d;
    desc.layers = setup.layers;
    desc.flags = setup.flags;
    const Texture texture = rig.texture(desc);
    std::vector<std::uint8_t> bytes;
    std::vector<VkBufferImageCopy> regions;
    const unsigned count = setup.type == VK_IMAGE_TYPE_3D ? setup.d : setup.layers;
    if (setup.type == VK_IMAGE_TYPE_3D) {
        for (unsigned z = 0; z < setup.d; ++z) {
            const auto slice = layer_pattern(setup.w, setup.h, z);
            bytes.insert(bytes.end(), slice.begin(), slice.end());
        }
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {setup.w, setup.h, setup.d};
        regions.push_back(region);
    } else {
        for (unsigned l = 0; l < count; ++l) {
            VkBufferImageCopy region{};
            region.bufferOffset = bytes.size();
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, l, 1};
            region.imageExtent = {setup.w, setup.h, 1};
            regions.push_back(region);
            const auto data = layer_pattern(setup.w, setup.h, l);
            bytes.insert(bytes.end(), data.begin(), data.end());
        }
    }
    std::string error;
    rig.must(rig.kit.upload_texture(texture, regions, bytes, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, error), error);
    const VkSampler sampler = rig.sampler(setup.filter, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
    DescriptorWrite write;
    write.binding = 0;
    write.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.image = {sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorSet set = rig.dset(set_layout, {write});
    const VkPipelineLayout layout = rig.playout({set_layout});
    PipelineDesc pipe;
    pipe.vertex = rig.sh("full.vert");
    pipe.fragment = rig.sh(setup.shader);
    pipe.layout = layout;
    pipe.pass = target.clear_pass;
    const VkPipeline pipeline = rig.pipe(pipe);
    out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0); }, layout, {set}));
}

void add_type(const char* name, TypeSetup setup, std::function<std::string(Kit&)> caps = {}, const char* feature = nullptr,
              const char* extension = nullptr) {
    CaseSpec spec("tex", name);
    spec.tolerance(Tolerance::exact());
    if (feature != nullptr) {
        spec.feature(feature);
    }
    if (extension != nullptr) {
        spec.extension(extension);
    }
    Cases::add(std::move(spec), [setup](Rig& rig, CaptureSet& out) { run_type(rig, out, setup); }, std::move(caps));
}

void register_types() {
    TypeSetup one;
    one.type = VK_IMAGE_TYPE_1D;
    one.view = VK_IMAGE_VIEW_TYPE_1D;
    one.h = 1;
    one.w = 32;
    one.shader = "t1d.frag";
    one.filter = VK_FILTER_LINEAR;
    add_type("type_1d", one);
    TypeSetup one_array = one;
    one_array.view = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    one_array.layers = 4;
    one_array.shader = "t1da.frag";
    one_array.filter = VK_FILTER_NEAREST;
    add_type("type_1d_array", one_array);
    TypeSetup array;
    array.view = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    array.layers = 4;
    array.shader = "t2da.frag";
    add_type("type_2d_array", array);
    TypeSetup volume;
    volume.type = VK_IMAGE_TYPE_3D;
    volume.view = VK_IMAGE_VIEW_TYPE_3D;
    volume.d = 4;
    volume.shader = "t3d.frag";
    volume.filter = VK_FILTER_LINEAR;
    add_type("type_3d", volume);
    TypeSetup cube;
    cube.view = VK_IMAGE_VIEW_TYPE_CUBE;
    cube.layers = 6;
    cube.shader = "tcube.frag";
    add_type("type_cube", cube);
    TypeSetup cube_array = cube;
    cube_array.view = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    cube_array.layers = 12;
    cube_array.shader = "tcubea.frag";
    add_type("type_cube_array", cube_array, {}, "imageCubeArray");
    Cases::add(CaseSpec("tex", "type_3d_slice_2d_view").extension("VK_EXT_image_2d_view_of_3d").tolerance(Tolerance::exact()),
               [](Rig& rig, CaptureSet& out) {
                   Target& target = rig.make_target(TargetDesc{});
                   TextureDesc desc;
                   desc.type = VK_IMAGE_TYPE_3D;
                   desc.view_type = VK_IMAGE_VIEW_TYPE_3D;
                   desc.width = desc.height = 16;
                   desc.depth = 4;
                   desc.flags = VK_IMAGE_CREATE_2D_VIEW_COMPATIBLE_BIT_EXT;
                   desc.create_view = false;
                   const Texture texture = rig.texture(desc);
                   std::vector<std::uint8_t> bytes;
                   for (unsigned z = 0; z < 4; ++z) {
                       const auto slice = layer_pattern(16, 16, z);
                       bytes.insert(bytes.end(), slice.begin(), slice.end());
                   }
                   VkBufferImageCopy region{};
                   region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                   region.imageExtent = {16, 16, 4};
                   std::string error;
                   rig.must(rig.kit.upload_texture(texture, {region}, bytes, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, error), error);
                   VkImageViewCreateInfo view{};
                   view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                   view.image = texture.image;
                   view.viewType = VK_IMAGE_VIEW_TYPE_2D;
                   view.format = VK_FORMAT_R8G8B8A8_UNORM;
                   view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 2, 1};
                   VkImageView slice_view = VK_NULL_HANDLE;
                   rig.vk_ok(rig.kit.api.vkCreateImageView(rig.kit.device, &view, nullptr, &slice_view), "vkCreateImageView");
                   rig.kit.defer([&kit = rig.kit, slice_view] { kit.api.vkDestroyImageView(kit.device, slice_view, nullptr); });
                   const VkSampler sampler = rig.sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
                   const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
                   DescriptorWrite write;
                   write.binding = 0;
                   write.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                   write.image = {sampler, slice_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                   const VkDescriptorSet set = rig.dset(set_layout, {write});
                   const VkPipelineLayout layout = rig.playout({set_layout}, 16, VK_SHADER_STAGE_FRAGMENT_BIT);
                   PipelineDesc pipe;
                   pipe.vertex = rig.sh("full.vert");
                   pipe.fragment = rig.sh("tex_f.frag");
                   pipe.layout = layout;
                   pipe.pass = target.clear_pass;
                   const VkPipeline pipeline = rig.pipe(pipe);
                   struct Push {
                       float scale[4];
                       float bias[4];
                       std::int32_t info[4];
                   } push{{1, 1, 1, 1}, {0, 0, 0, 0}, {16, 16, 0, 0}};
                   const VkPipelineLayout full_layout = rig.playout({set_layout}, sizeof push, VK_SHADER_STAGE_FRAGMENT_BIT);
                   pipe.layout = full_layout;
                   const VkPipeline pipeline2 = rig.pipe(pipe);
                   (void)layout;
                   (void)pipeline;
                   out.add("", rig.draw(target, pipeline2, nullptr, [&](VkCommandBuffer cb) {
                       rig.push(cb, full_layout, VK_SHADER_STAGE_FRAGMENT_BIT, &push, sizeof push);
                       rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
                   }, full_layout, {set}));
               },
               [](Kit& kit) -> std::string {
                   const auto f = kit.query_features<VkPhysicalDeviceImage2DViewOf3DFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_2D_VIEW_OF_3D_FEATURES_EXT);
                   return f.sampler2DViewOf3D != VK_FALSE ? std::string() : std::string("sampler2DViewOf3D");
               });
    CaseSpec multisample("tex", "type_2d_ms_texel_fetch");
    multisample.tolerance(Tolerance::exact());
    Cases::add(std::move(multisample), [](Rig& rig, CaptureSet& out) {
        if (!rig.kit.format_supports(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
            rig.fail("RGBA8 sampling unsupported");
        }
        TargetDesc td;
        td.samples = VK_SAMPLE_COUNT_4_BIT;
        td.color_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        td.width = td.height = 64;
        Target& source = rig.make_target(td);
        source.clear_color[0] = 0.1f;
        source.clear_color[1] = 0.1f;
        source.clear_color[2] = 0.2f;
        const std::vector<Vertex2> tris = {vertex(-0.8f, -0.7f, 0), vertex(0.7f, -0.3f, 1), vertex(-0.1f, 0.8f, 2),
                                           vertex(0.5f, 0.6f, 3),   vertex(0.9f, 0.9f, 4),  vertex(0.2f, 0.95f, 5)};
        const Buffer vb = rig.buffer(tris, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        PipelineDesc scene = rig.pos2c(source);
        const VkPipeline p_scene = rig.pipe(scene);
        rig.draw(source, p_scene, &vb, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 6, 1, 0, 0); });
        const Texture& ms = source.color_[0];
        rig.run([&](VkCommandBuffer cb) {
            rig.kit.barrier(cb, ms.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });
        Target& target = rig.make_target(TargetDesc{});
        const VkSampler sampler = rig.sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
        DescriptorWrite write;
        write.binding = 0;
        write.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.image = {sampler, ms.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorSet set = rig.dset(set_layout, {write});
        const VkPipelineLayout layout = rig.playout({set_layout});
        PipelineDesc show;
        show.vertex = rig.sh("full.vert");
        show.fragment = rig.sh("t2dms.frag");
        show.layout = layout;
        show.pass = target.clear_pass;
        const VkPipeline p_show = rig.pipe(show);
        out.add("", rig.draw(target, p_show, nullptr, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0); }, layout, {set}));
    });
}

const int registered = (register_types(), 0);

}  // namespace

}  // namespace lsw::render::vk
