#include "render/common/mat4.hpp"
#include "render/common/pattern.hpp"
#include "render/vk/rig.hpp"

#include <cmath>
#include <cstring>

namespace lsw::render::vk {

namespace {

struct PlaneVertex {
    float pos[3];
    float uv[2];
};

struct PlanePush {
    float m[16];
    float uv[4];
    float params[4];
};

std::vector<std::vector<std::uint8_t>> tinted_chain(unsigned size, unsigned levels) {
    static const std::uint32_t light[8] = {Pattern::pack(250, 250, 250), Pattern::pack(250, 220, 120), Pattern::pack(120, 250, 160),
                                           Pattern::pack(120, 200, 250), Pattern::pack(250, 140, 220), Pattern::pack(200, 160, 250),
                                           Pattern::pack(250, 170, 110), Pattern::pack(170, 250, 250)};
    static const std::uint32_t dark[8] = {Pattern::pack(30, 30, 40), Pattern::pack(120, 40, 30), Pattern::pack(30, 110, 50),
                                          Pattern::pack(30, 50, 130), Pattern::pack(110, 30, 90), Pattern::pack(60, 30, 120),
                                          Pattern::pack(120, 70, 30), Pattern::pack(30, 100, 100)};
    std::vector<std::vector<std::uint8_t>> chain;
    unsigned dim = size;
    for (unsigned l = 0; l < levels; ++l) {
        chain.push_back(Pattern::checker(dim, dim, std::max(1u, dim / 8), light[l % 8], dark[l % 8]));
        dim = std::max(1u, dim / 2);
    }
    return chain;
}

struct SampSetup {
    std::function<void(VkSamplerCreateInfo&)> sampler;
    std::function<void(VkImageViewCreateInfo&)> view;
    unsigned size = 64;
    unsigned mips = 7;
    float uv_scale = 4.0f;
    float uv_offset = 0.0f;
    float mode = 0.0f;
    float p1 = 0.0f;
    float p2 = 0.0f;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    const void* sampler_next = nullptr;
    float tilt = 1.0f;
    const char* shader = "samp.frag";
};

void run_plane(Rig& rig, CaptureSet& out, const SampSetup& setup) {
    Kit& kit = rig.kit;
    Target& target = rig.make_target(TargetDesc{});
    target.clear_color[0] = 0.12f;
    target.clear_color[1] = 0.14f;
    target.clear_color[2] = 0.2f;
    TextureDesc desc;
    desc.width = desc.height = setup.size;
    desc.mips = setup.mips;
    desc.format = setup.format;
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = setup.format;
    view.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, setup.mips, 0, 1};
    if (setup.view) {
        setup.view(view);
        desc.view_override = &view;
    }
    Texture texture;
    std::string error;
    rig.must(kit.make_texture(texture, desc, error), error);
    const auto chain = tinted_chain(setup.size, setup.mips);
    std::vector<std::uint8_t> bytes;
    std::vector<VkBufferImageCopy> regions;
    unsigned dim = setup.size;
    for (unsigned l = 0; l < setup.mips; ++l) {
        VkBufferImageCopy region{};
        region.bufferOffset = bytes.size();
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1};
        region.imageExtent = {dim, dim, 1};
        regions.push_back(region);
        bytes.insert(bytes.end(), chain[l].begin(), chain[l].end());
        dim = std::max(1u, dim / 2);
    }
    rig.must(kit.upload_texture(texture, regions, bytes, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, error), error);
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.pNext = setup.sampler_next;
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.maxLod = static_cast<float>(setup.mips);
    info.maxAnisotropy = 1.0f;
    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    if (setup.sampler) {
        setup.sampler(info);
    }
    const VkSampler sampler = rig.sampler(info);
    const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
    DescriptorWrite write;
    write.binding = 0;
    write.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.image = {sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorSet set = rig.dset(set_layout, {write});
    const VkPipelineLayout layout = rig.playout({set_layout}, sizeof(PlanePush), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    PipelineDesc pipe;
    pipe.vertex = rig.sh("plane.vert");
    pipe.fragment = rig.sh(setup.shader);
    pipe.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    pipe.bindings = {{0, sizeof(PlaneVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
    pipe.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 12}};
    pipe.layout = layout;
    pipe.pass = target.clear_pass;
    const VkPipeline pipeline = rig.pipe(pipe);
    const std::vector<PlaneVertex> plane = {{{-1.5f, -1.5f, 0}, {0, 0}}, {{1.5f, -1.5f, 0}, {1, 0}}, {{-1.5f, 1.5f, 0}, {0, 1}}, {{1.5f, 1.5f, 0}, {1, 1}}};
    const Buffer vb = rig.buffer(plane, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    const Mat4 m = Mat4::perspective(1.0f, 1.0f, 0.1f, 40.0f, true) * Mat4::translate(0, 0, -2.6f) * Mat4::rotate_x(-setup.tilt);
    PlanePush push{};
    std::memcpy(push.m, m.m, sizeof push.m);
    push.uv[0] = push.uv[1] = setup.uv_scale;
    push.uv[2] = push.uv[3] = setup.uv_offset;
    push.params[0] = setup.mode;
    push.params[1] = setup.p1;
    push.params[2] = setup.p2;
    out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
        rig.push(cb, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &push, sizeof push);
        rig.kit.api.vkCmdDraw(cb, 4, 1, 0, 0);
    }, layout, {set}));
}

void add_sampler(const std::string& name, SampSetup setup, std::function<std::string(Kit&)> caps = {}, const char* feature = nullptr,
                 const char* extension = nullptr, unsigned minor = 0, Tolerance tolerance = Tolerance::exact()) {
    CaseSpec spec("tex", name);
    spec.tolerance(std::move(tolerance));
    if (feature != nullptr) {
        spec.feature(feature);
    }
    if (extension != nullptr) {
        spec.extension(extension);
    }
    if (minor != 0) {
        spec.min_version(1, minor);
    }
    Cases::add(std::move(spec), [setup](Rig& rig, CaptureSet& out) { run_plane(rig, out, setup); }, std::move(caps));
}

void register_filters() {
    add_sampler("samp_nearest", {[](VkSamplerCreateInfo& s) { s.magFilter = s.minFilter = VK_FILTER_NEAREST; s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; }});
    add_sampler("samp_bilinear_mip_nearest", {[](VkSamplerCreateInfo& s) { s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; }});
    add_sampler("samp_trilinear", {[](VkSamplerCreateInfo&) {}});
    add_sampler("samp_mag_linear_min_nearest", {[](VkSamplerCreateInfo& s) { s.minFilter = VK_FILTER_NEAREST; }});
    SampSetup mag_nearest{[](VkSamplerCreateInfo& s) { s.magFilter = VK_FILTER_NEAREST; }};
    mag_nearest.uv_scale = 0.5f;
    add_sampler("samp_mag_nearest_min_linear", mag_nearest);
    for (float aniso : {2.0f, 4.0f, 16.0f}) {
        SampSetup setup{[aniso](VkSamplerCreateInfo& s) { s.anisotropyEnable = VK_TRUE; s.maxAnisotropy = aniso; }};
        setup.uv_scale = 10.0f;
        setup.tilt = 1.25f;
        add_sampler("samp_aniso_" + std::to_string(static_cast<int>(aniso)), setup,
                    [aniso](Kit& kit) -> std::string { return kit.properties.limits.maxSamplerAnisotropy < aniso ? "maxSamplerAnisotropy" : std::string(); },
                    "samplerAnisotropy", nullptr, 0, Tolerance::exact());
    }
    SampSetup bias_up{[](VkSamplerCreateInfo& s) { s.mipLodBias = 1.0f; }};
    bias_up.uv_scale = 8.0f;
    add_sampler("samp_lod_bias_positive", bias_up);
    SampSetup bias_down{[](VkSamplerCreateInfo& s) { s.mipLodBias = -1.5f; }};
    bias_down.uv_scale = 8.0f;
    add_sampler("samp_lod_bias_negative", bias_down);
    SampSetup min_lod{[](VkSamplerCreateInfo& s) { s.minLod = 2.0f; }};
    min_lod.uv_scale = 3.0f;
    add_sampler("samp_min_lod", min_lod);
    SampSetup max_lod{[](VkSamplerCreateInfo& s) { s.maxLod = 1.0f; }};
    max_lod.uv_scale = 12.0f;
    add_sampler("samp_max_lod", max_lod);
    SampSetup shader_bias{[](VkSamplerCreateInfo&) {}};
    shader_bias.mode = 0.0f;
    shader_bias.p1 = 2.0f;
    shader_bias.uv_scale = 6.0f;
    add_sampler("samp_shader_bias", shader_bias);
}

void register_addressing() {
    struct Mode {
        const char* name;
        VkSamplerAddressMode mode;
        VkBorderColor border;
    };
    const std::vector<Mode> modes = {
        {"repeat", VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK},
        {"mirrored_repeat", VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK},
        {"clamp_to_edge", VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK},
        {"border_transparent", VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK},
        {"border_opaque_black", VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK},
        {"border_opaque_white", VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE},
    };
    for (const Mode& entry : modes) {
        SampSetup setup{[entry](VkSamplerCreateInfo& s) {
            s.addressModeU = s.addressModeV = s.addressModeW = entry.mode;
            s.borderColor = entry.border;
        }};
        setup.uv_scale = 2.4f;
        setup.tilt = 0.6f;
        add_sampler(std::string("samp_addr_") + entry.name, setup);
    }
    SampSetup mirror_clamp{[](VkSamplerCreateInfo& s) { s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE; }};
    mirror_clamp.uv_scale = 2.4f;
    mirror_clamp.uv_offset = -1.2f;
    mirror_clamp.tilt = 0.6f;
    add_sampler("samp_addr_mirror_clamp_to_edge", mirror_clamp,
                [](Kit& kit) -> std::string {
                    const auto f = kit.query_features<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
                    return f.samplerMirrorClampToEdge != VK_FALSE ? std::string() : std::string("samplerMirrorClampToEdge");
                },
                nullptr, nullptr, 2);
    SampSetup mixed{[](VkSamplerCreateInfo& s) {
        s.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        s.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    }};
    mixed.uv_scale = 2.4f;
    mixed.tilt = 0.6f;
    add_sampler("samp_addr_mixed_uv", mixed);
    SampSetup unnormalized{[](VkSamplerCreateInfo& s) {
        s.unnormalizedCoordinates = VK_TRUE;
        s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        s.minFilter = s.magFilter = VK_FILTER_LINEAR;
        s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        s.minLod = s.maxLod = 0.0f;
    }};
    unnormalized.shader = "samp_lod.frag";
    unnormalized.mode = 1.0f;
    unnormalized.uv_scale = 80.0f;
    unnormalized.mips = 1;
    unnormalized.tilt = 0.5f;
    add_sampler("samp_unnormalized_coordinates", unnormalized);
    VkSamplerCustomBorderColorCreateInfoEXT custom{};
    custom.sType = VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT;
    custom.customBorderColor.float32[0] = 0.9f;
    custom.customBorderColor.float32[1] = 0.3f;
    custom.customBorderColor.float32[2] = 0.6f;
    custom.customBorderColor.float32[3] = 1.0f;
    custom.format = VK_FORMAT_R8G8B8A8_UNORM;
    static VkSamplerCustomBorderColorCreateInfoEXT custom_persistent = custom;
    SampSetup custom_setup{[](VkSamplerCreateInfo& s) {
        s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        s.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
    }};
    custom_setup.sampler_next = &custom_persistent;
    custom_setup.uv_scale = 2.4f;
    custom_setup.tilt = 0.6f;
    add_sampler("samp_custom_border_color", custom_setup,
                [](Kit& kit) -> std::string {
                    const auto f = kit.query_features<VkPhysicalDeviceCustomBorderColorFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT);
                    return f.customBorderColors != VK_FALSE ? std::string() : std::string("customBorderColors");
                },
                nullptr, "VK_EXT_custom_border_color");
}

void register_reduction() {
    for (int minmax = 0; minmax < 2; ++minmax) {
        static VkSamplerReductionModeCreateInfo reduce_min{VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO, nullptr, VK_SAMPLER_REDUCTION_MODE_MIN};
        static VkSamplerReductionModeCreateInfo reduce_max{VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO, nullptr, VK_SAMPLER_REDUCTION_MODE_MAX};
        SampSetup setup{[](VkSamplerCreateInfo& s) { s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; }};
        setup.sampler_next = minmax == 0 ? &reduce_min : &reduce_max;
        setup.uv_scale = 6.0f;
        add_sampler(minmax == 0 ? "samp_reduction_min" : "samp_reduction_max", setup,
                    [](Kit& kit) -> std::string {
                        return Cases::need_format(kit, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_MINMAX_BIT, "min max sampling of RGBA8");
                    },
                    nullptr, nullptr, 2);
    }
}

void register_swizzles() {
    struct Swizzle {
        const char* name;
        VkComponentMapping map;
    };
    const std::vector<Swizzle> swizzles = {
        {"bgra", {VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_A}},
        {"rrr_one", {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ONE}},
        {"zero_one_a_g", {VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ONE, VK_COMPONENT_SWIZZLE_A, VK_COMPONENT_SWIZZLE_G}},
        {"gbr_r", {VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R}},
    };
    for (const Swizzle& entry : swizzles) {
        SampSetup setup{[](VkSamplerCreateInfo& s) { s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; },
                        [entry](VkImageViewCreateInfo& v) { v.components = entry.map; }};
        setup.uv_scale = 3.0f;
        add_sampler(std::string("view_swizzle_") + entry.name, setup);
    }
    SampSetup min_lod_view{[](VkSamplerCreateInfo&) {}, nullptr};
    static VkImageViewMinLodCreateInfoEXT min_lod_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_MIN_LOD_CREATE_INFO_EXT, nullptr, 3.0f};
    min_lod_view.view = [](VkImageViewCreateInfo& v) { v.pNext = &min_lod_info; };
    min_lod_view.uv_scale = 3.0f;
    add_sampler("view_min_lod", min_lod_view,
                [](Kit& kit) -> std::string {
                    const auto f = kit.query_features<VkPhysicalDeviceImageViewMinLodFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT);
                    return f.minLod != VK_FALSE ? std::string() : std::string("minLod");
                },
                nullptr, "VK_EXT_image_view_min_lod");
    SampSetup subrange{[](VkSamplerCreateInfo& s) { s.maxLod = 3.0f; },
                       [](VkImageViewCreateInfo& v) { v.subresourceRange.baseMipLevel = 2; v.subresourceRange.levelCount = 3; }};
    subrange.uv_scale = 4.0f;
    add_sampler("view_mip_subrange", subrange);
}

void register_shader_ops() {
    SampSetup lod{[](VkSamplerCreateInfo&) {}};
    lod.mode = 1.0f;
    lod.p1 = 2.5f;
    lod.uv_scale = 3.0f;
    add_sampler("samp_texture_lod", lod);
    SampSetup grad{[](VkSamplerCreateInfo&) {}};
    grad.mode = 2.0f;
    grad.p1 = 3.0f;
    grad.uv_scale = 5.0f;
    add_sampler("samp_texture_grad", grad);
    SampSetup query{[](VkSamplerCreateInfo& s) { s.minLod = 2.0f; s.maxLod = 3.0f; }};
    query.mode = 4.0f;
    query.uv_scale = 9.0f;
    add_sampler("samp_query_lod", query, {}, nullptr, nullptr, 0, Tolerance::exact());
    for (float comp : {0.0f, 2.0f}) {
        SampSetup gather{[](VkSamplerCreateInfo& s) { s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; s.maxLod = 0.0f; }};
        gather.mode = comp == 0.0f ? 3.0f : 7.0f;
        gather.uv_scale = 1.0f;
        add_sampler(std::string("samp_gather_comp") + std::to_string(static_cast<int>(comp)), gather);
    }
    SampSetup offset_gather{[](VkSamplerCreateInfo& s) { s.maxLod = 0.0f; }};
    offset_gather.mode = 5.0f;
        offset_gather.uv_scale = 1.0f;
    add_sampler("samp_gather_offset", offset_gather);
    SampSetup offset{[](VkSamplerCreateInfo&) {}};
    offset.mode = 6.0f;
    offset.uv_scale = 2.0f;
    add_sampler("samp_texture_offset", offset);
}

const int registered = (register_filters(), register_addressing(), register_reduction(), register_swizzles(), register_shader_ops(), 0);

}  // namespace

}  // namespace lsw::render::vk
