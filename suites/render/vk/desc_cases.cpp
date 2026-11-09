#include "render/common/pattern.hpp"
#include "render/vk/quadkit.hpp"

#include <cstring>

namespace lsw::render::vk {

namespace {

struct UniformData {
    float tint[4];
    float params[4];
};

CaseSpec spec_of(const std::string& name) {
    CaseSpec spec("desc", name);
    spec.tolerance(Tolerance::tight(1));
    return spec;
}

struct Basic {
    Target* target = nullptr;
    Texture texture;
    Buffer uniform;
    VkSampler sampler = VK_NULL_HANDLE;
};

Basic make_basic(Rig& rig, const UniformData& data) {
    Basic b;
    b.target = &rig.make_target(TargetDesc{});
    b.texture = rig.sampled(16, 16, VK_FORMAT_R8G8B8A8_UNORM, Pattern::checker(16, 16, 2, Pattern::pack(240, 220, 90), Pattern::pack(40, 70, 200)));
    b.uniform = rig.buffer(&data, sizeof data, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    b.sampler = rig.sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    return b;
}

PipelineDesc basic_pipe(Rig& rig, Basic& b, VkPipelineLayout layout, const char* fragment = "desc_basic.frag") {
    PipelineDesc p;
    p.vertex = rig.sh("full.vert");
    p.fragment = rig.sh(fragment);
    p.layout = layout;
    p.pass = b.target->clear_pass;
    return p;
}

VkDescriptorPool make_pool(Rig& rig, const std::vector<VkDescriptorPoolSize>& sizes, VkDescriptorPoolCreateFlags flags = 0, const void* next = nullptr,
                           std::uint32_t sets = 4) {
    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.pNext = next;
    info.flags = flags;
    info.maxSets = sets;
    info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    info.pPoolSizes = sizes.data();
    VkDescriptorPool pool = VK_NULL_HANDLE;
    rig.vk_ok(rig.kit.api.vkCreateDescriptorPool(rig.kit.device, &info, nullptr, &pool), "vkCreateDescriptorPool");
    Kit* kit = &rig.kit;
    rig.kit.defer([kit, pool] { kit->api.vkDestroyDescriptorPool(kit->device, pool, nullptr); });
    return pool;
}

VkDescriptorSet alloc_set(Rig& rig, VkDescriptorPool pool, VkDescriptorSetLayout layout, const void* next = nullptr) {
    VkDescriptorSetAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.pNext = next;
    info.descriptorPool = pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    rig.vk_ok(rig.kit.api.vkAllocateDescriptorSets(rig.kit.device, &info, &set), "vkAllocateDescriptorSets");
    return set;
}

std::vector<VkDescriptorSetLayoutBinding> basic_bindings() {
    return {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
}

void write_basic(Rig& rig, VkDescriptorSet set, const Buffer& uniform, const Basic& b) {
    VkDescriptorBufferInfo bi{uniform.buffer, 0, sizeof(UniformData)};
    VkDescriptorImageInfo ii{b.sampler, b.texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = writes[1].dstSet = set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &bi;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &ii;
    rig.kit.api.vkUpdateDescriptorSets(rig.kit.device, 2, writes, 0, nullptr);
}

void register_types_and_sets() {
    Cases::add(spec_of("all_descriptor_types"), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        Target& target = rig.make_target(TargetDesc{});
        const float tint[4] = {0.9f, 0.5f, 0.3f, 1.0f};
        const Buffer ubo = rig.buffer(tint, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        std::vector<float> values;
        for (int i = 0; i < 8; ++i) {
            values.push_back(0.1f + 0.11f * static_cast<float>(i));
            values.push_back(0.9f - 0.1f * static_cast<float>(i));
            values.push_back(0.3f + 0.05f * static_cast<float>(i));
            values.push_back(1.0f);
        }
        const Buffer ssbo = rig.buffer(values.data(), values.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        const Buffer texel = rig.buffer(values.data(), values.size() * 4, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT);
        const Texture combined = rig.sampled(16, 16, VK_FORMAT_R8G8B8A8_UNORM, Pattern::checker(16, 16, 4, Pattern::pack(250, 90, 90), Pattern::pack(40, 40, 70)));
        const Texture image = rig.sampled(8, 8, VK_FORMAT_R8G8B8A8_UNORM, Pattern::gradient(8, 8));
        TextureDesc storage_desc;
        storage_desc.width = storage_desc.height = 8;
        storage_desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        const Texture storage = rig.texture(storage_desc);
        std::string error;
        std::vector<std::uint8_t> pixels = Pattern::noise(8, 8, 77);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {8, 8, 1};
        rig.must(kit.upload_texture(storage, {region}, pixels, VK_IMAGE_LAYOUT_GENERAL, error), error);
        VkBufferViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
        view_info.buffer = texel.buffer;
        view_info.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        view_info.range = VK_WHOLE_SIZE;
        VkBufferView texel_view = VK_NULL_HANDLE;
        rig.vk_ok(kit.api.vkCreateBufferView(kit.device, &view_info, nullptr, &texel_view), "vkCreateBufferView");
        kit.defer([&kit, texel_view] { kit.api.vkDestroyBufferView(kit.device, texel_view, nullptr); });
        const VkSampler sampler = rig.sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);
        const VkShaderStageFlags frag = VK_SHADER_STAGE_FRAGMENT_BIT;
        const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, frag, nullptr},
                                                              {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, frag, nullptr},
                                                              {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, frag, nullptr},
                                                              {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, frag, nullptr},
                                                              {4, VK_DESCRIPTOR_TYPE_SAMPLER, 1, frag, nullptr},
                                                              {5, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1, frag, nullptr},
                                                              {6, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1, frag, nullptr},
                                                              {7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, frag, nullptr}});
        const VkDescriptorPool pool = make_pool(rig, {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
                                                      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}, {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
                                                      {VK_DESCRIPTOR_TYPE_SAMPLER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1},
                                                      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}});
        const VkDescriptorSet set = alloc_set(rig, pool, set_layout);
        const VkDescriptorBufferInfo ubo_info{ubo.buffer, 0, 16};
        const VkDescriptorBufferInfo ssbo_info{ssbo.buffer, 0, VK_WHOLE_SIZE};
        const VkDescriptorImageInfo combined_info{sampler, combined.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo image_info{VK_NULL_HANDLE, image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo sampler_info{sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        const VkDescriptorImageInfo storage_info{VK_NULL_HANDLE, storage.view, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet w[8]{};
        for (std::uint32_t i = 0; i < 8; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = set;
            w[i].dstBinding = i;
            w[i].descriptorCount = 1;
        }
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[0].pBufferInfo = &ubo_info;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[1].pBufferInfo = &ssbo_info;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[2].pImageInfo = &combined_info;
        w[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        w[3].pImageInfo = &image_info;
        w[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        w[4].pImageInfo = &sampler_info;
        w[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        w[5].pTexelBufferView = &texel_view;
        w[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        w[6].pTexelBufferView = &texel_view;
        w[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[7].pImageInfo = &storage_info;
        kit.api.vkUpdateDescriptorSets(kit.device, 8, w, 0, nullptr);
        const VkPipelineLayout layout = rig.playout({set_layout});
        PipelineDesc pipe;
        pipe.vertex = rig.sh("full.vert");
        pipe.fragment = rig.sh("desc_all.frag");
        pipe.layout = layout;
        pipe.pass = target.clear_pass;
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) { kit.api.vkCmdDraw(cb, 3, 1, 0, 0); }, layout, {set}));
    }, [](Kit& kit) -> std::string {
        return Cases::first({Cases::need_buffer_format(kit, VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT, "uniform texel buffer RGBA32F"),
                             Cases::need_buffer_format(kit, VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT, "storage texel buffer RGBA32F"),
                             Cases::need_format(kit, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT, "storage image RGBA8")});
    });
    Cases::add(spec_of("push_descriptors").extension("VK_KHR_push_descriptor"), [](Rig& rig, CaptureSet& out) {
        const UniformData first{{1.0f, 0.8f, 0.6f, 1.0f}, {2.0f, 2.0f, 0, 0}};
        const UniformData second{{0.6f, 1.0f, 0.9f, 1.0f}, {4.0f, 1.0f, 0, 0}};
        Basic b = make_basic(rig, first);
        const Buffer second_buffer = rig.buffer(&second, sizeof second, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        const VkDescriptorSetLayout set_layout = rig.dlayout(basic_bindings(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);
        const VkPipelineLayout layout = rig.playout({set_layout});
        const VkPipeline pipeline = rig.pipe(basic_pipe(rig, b, layout));
        out.add("", rig.draw(*b.target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            for (unsigned i = 0; i < 2; ++i) {
                const Buffer& ubo = i == 0 ? b.uniform : second_buffer;
                VkDescriptorBufferInfo bi{ubo.buffer, 0, sizeof(UniformData)};
                VkDescriptorImageInfo ii{b.sampler, b.texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                VkWriteDescriptorSet writes[2]{};
                writes[0].sType = writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[0].dstBinding = 0;
                writes[0].descriptorCount = 1;
                writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writes[0].pBufferInfo = &bi;
                writes[1].dstBinding = 1;
                writes[1].descriptorCount = 1;
                writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[1].pImageInfo = &ii;
                rig.kit.api.vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 2, writes);
                rig.viewport(cb, static_cast<float>(i) * 64.0f, 0, 64, 128);
                rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            }
        }, layout));
    });
    CaseSpec tmpl = spec_of("update_template");
    tmpl.min_version(1, 1);
    Cases::add(std::move(tmpl), [](Rig& rig, CaptureSet& out) {
        const UniformData data{{0.7f, 1.0f, 0.8f, 1.0f}, {3.0f, 3.0f, 0, 0}};
        Basic b = make_basic(rig, data);
        const VkDescriptorSetLayout set_layout = rig.dlayout(basic_bindings());
        const VkDescriptorPool pool = make_pool(rig, {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}});
        const VkDescriptorSet set = alloc_set(rig, pool, set_layout);
        struct Packed {
            VkDescriptorBufferInfo buffer;
            VkDescriptorImageInfo image;
        } packed{{b.uniform.buffer, 0, sizeof(UniformData)}, {b.sampler, b.texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkDescriptorUpdateTemplateEntry entries[2]{};
        entries[0] = {0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, offsetof(Packed, buffer), sizeof(VkDescriptorBufferInfo)};
        entries[1] = {1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, offsetof(Packed, image), sizeof(VkDescriptorImageInfo)};
        VkDescriptorUpdateTemplateCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO;
        info.descriptorUpdateEntryCount = 2;
        info.pDescriptorUpdateEntries = entries;
        info.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
        info.descriptorSetLayout = set_layout;
        VkDescriptorUpdateTemplate tmpl_handle = VK_NULL_HANDLE;
        rig.vk_ok(rig.kit.api.vkCreateDescriptorUpdateTemplate(rig.kit.device, &info, nullptr, &tmpl_handle), "vkCreateDescriptorUpdateTemplate");
        rig.kit.defer([&kit = rig.kit, tmpl_handle] { kit.api.vkDestroyDescriptorUpdateTemplate(kit.device, tmpl_handle, nullptr); });
        rig.kit.api.vkUpdateDescriptorSetWithTemplate(rig.kit.device, set, tmpl_handle, &packed);
        const VkPipelineLayout layout = rig.playout({set_layout});
        const VkPipeline pipeline = rig.pipe(basic_pipe(rig, b, layout));
        out.add("", rig.draw(*b.target, pipeline, nullptr, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0); }, layout, {set}));
    });
    CaseSpec after_bind = spec_of("update_after_bind");
    after_bind.min_version(1, 2);
    Cases::add(
        std::move(after_bind),
        [](Rig& rig, CaptureSet& out) {
            const UniformData first{{0.3f, 0.3f, 0.3f, 1.0f}, {2.0f, 2.0f, 0, 0}};
            const UniformData second{{1.0f, 0.7f, 0.4f, 1.0f}, {3.0f, 3.0f, 0, 0}};
            Basic b = make_basic(rig, first);
            const Buffer late = rig.buffer(&second, sizeof second, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
            const VkDescriptorBindingFlags flags[2] = {VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT, VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT};
            VkDescriptorSetLayoutBindingFlagsCreateInfo flag_info{};
            flag_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            flag_info.bindingCount = 2;
            flag_info.pBindingFlags = flags;
            const VkDescriptorSetLayout set_layout = rig.dlayout(basic_bindings(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT, &flag_info);
            const VkDescriptorPool pool = make_pool(rig, {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}},
                                                    VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT);
            const VkDescriptorSet set = alloc_set(rig, pool, set_layout);
            write_basic(rig, set, b.uniform, b);
            const VkPipelineLayout layout = rig.playout({set_layout});
            const VkPipeline pipeline = rig.pipe(basic_pipe(rig, b, layout));
            out.add("", rig.draw(*b.target, pipeline, nullptr, [&](VkCommandBuffer cb) {
                rig.kit.api.vkCmdDraw(cb, 0, 0, 0, 0);
                write_basic(rig, set, late, b);
                rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            }, layout, {set}));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
            return f.descriptorBindingUniformBufferUpdateAfterBind != VK_FALSE && f.descriptorBindingSampledImageUpdateAfterBind != VK_FALSE
                       ? std::string()
                       : std::string("descriptorBinding update after bind");
        });
    Cases::add(spec_of("immutable_samplers"), [](Rig& rig, CaptureSet& out) {
        const UniformData data{{1.0f, 1.0f, 1.0f, 1.0f}, {3.0f, 3.0f, 0, 0}};
        Basic b = make_basic(rig, data);
        std::vector<VkDescriptorSetLayoutBinding> bindings = basic_bindings();
        bindings[1].pImmutableSamplers = &b.sampler;
        const VkDescriptorSetLayout set_layout = rig.dlayout(bindings);
        const VkDescriptorPool pool = make_pool(rig, {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}});
        const VkDescriptorSet set = alloc_set(rig, pool, set_layout);
        VkDescriptorBufferInfo bi{b.uniform.buffer, 0, sizeof(UniformData)};
        VkDescriptorImageInfo ii{VK_NULL_HANDLE, b.texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = writes[1].dstSet = set;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &bi;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &ii;
        rig.kit.api.vkUpdateDescriptorSets(rig.kit.device, 2, writes, 0, nullptr);
        const VkPipelineLayout layout = rig.playout({set_layout});
        const VkPipeline pipeline = rig.pipe(basic_pipe(rig, b, layout));
        out.add("", rig.draw(*b.target, pipeline, nullptr, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0); }, layout, {set}));
    });
    Cases::add(spec_of("rebind_compatible_sets"), [](Rig& rig, CaptureSet& out) {
        const UniformData data{{1.0f, 1.0f, 1.0f, 1.0f}, {2.0f, 2.0f, 0, 0}};
        Basic b = make_basic(rig, data);
        const VkDescriptorSetLayout set_layout = rig.dlayout(basic_bindings());
        const VkDescriptorPool pool = make_pool(rig, {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3}}, 0, nullptr, 3);
        std::vector<Buffer> buffers;
        std::vector<VkDescriptorSet> sets;
        for (int i = 0; i < 3; ++i) {
            const UniformData d{{0.4f + 0.3f * static_cast<float>(i), 1.0f - 0.3f * static_cast<float>(i), 0.7f, 1.0f}, {1.0f + static_cast<float>(i), 2.0f + static_cast<float>(i), 0, 0}};
            buffers.push_back(rig.buffer(&d, sizeof d, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT));
            sets.push_back(alloc_set(rig, pool, set_layout));
            write_basic(rig, sets.back(), buffers.back(), b);
        }
        const VkPipelineLayout layout = rig.playout({set_layout});
        const VkPipeline pipeline = rig.pipe(basic_pipe(rig, b, layout));
        out.add("", rig.draw(*b.target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            for (int i = 0; i < 6; ++i) {
                rig.kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &sets[static_cast<std::size_t>(i % 3)], 0, nullptr);
                rig.viewport(cb, static_cast<float>(i % 3) * 42.0f, static_cast<float>(i / 3) * 64.0f, 42, 64);
                rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            }
        }, layout));
    });
}

void register_advanced() {
    CaseSpec dynamic = spec_of("dynamic_offsets");
    Cases::add(std::move(dynamic), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        Target& target = rig.make_target(TargetDesc{});
        const VkDeviceSize ubo_min = std::max<VkDeviceSize>(kit.properties.limits.minUniformBufferOffsetAlignment, 1);
        const VkDeviceSize ubo_align = (sizeof(UniformData) + ubo_min - 1) / ubo_min * ubo_min;
        const VkDeviceSize ssbo_align = std::max<VkDeviceSize>(kit.properties.limits.minStorageBufferOffsetAlignment, 16);
        std::vector<std::uint8_t> ubo_data(static_cast<std::size_t>(ubo_align) * 4, 0);
        std::vector<std::uint8_t> ssbo_data(static_cast<std::size_t>(ssbo_align) * 4, 0);
        for (unsigned i = 0; i < 4; ++i) {
            const UniformData d{{0.3f + 0.2f * static_cast<float>(i), 0.9f - 0.2f * static_cast<float>(i), 0.5f, 1.0f}, {0, 0, 0, 0}};
            std::memcpy(ubo_data.data() + ubo_align * i, &d, sizeof d);
            const float s[4] = {0.8f - 0.2f * static_cast<float>(i), 0.3f + 0.2f * static_cast<float>(i), 0.9f, 1.0f};
            std::memcpy(ssbo_data.data() + ssbo_align * i, s, sizeof s);
        }
        const Buffer ubo = rig.buffer(ubo_data.data(), ubo_data.size(), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        const Buffer ssbo = rig.buffer(ssbo_data.data(), ssbo_data.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                                              {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
        const VkDescriptorPool pool = make_pool(rig, {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1}}, 0, nullptr, 1);
        const VkDescriptorSet set = alloc_set(rig, pool, set_layout);
        const VkDescriptorBufferInfo bi{ubo.buffer, 0, sizeof(UniformData)};
        const VkDescriptorBufferInfo si{ssbo.buffer, 0, 16};
        VkWriteDescriptorSet w[2]{};
        w[0].sType = w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = w[1].dstSet = set;
        w[0].dstBinding = 0;
        w[0].descriptorCount = 1;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        w[0].pBufferInfo = &bi;
        w[1].dstBinding = 1;
        w[1].descriptorCount = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        w[1].pBufferInfo = &si;
        kit.api.vkUpdateDescriptorSets(kit.device, 2, w, 0, nullptr);
        const VkPipelineLayout layout = rig.playout({set_layout});
        PipelineDesc pipe;
        pipe.vertex = rig.sh("full.vert");
        pipe.fragment = rig.sh("desc_dyn.frag");
        pipe.layout = layout;
        pipe.pass = target.clear_pass;
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            for (unsigned i = 0; i < 4; ++i) {
                const std::uint32_t offsets[2] = {static_cast<std::uint32_t>(ubo_align * i), static_cast<std::uint32_t>(ssbo_align * (3 - i))};
                kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 2, offsets);
                rig.viewport(cb, static_cast<float>(i % 2) * 64.0f, static_cast<float>(i / 2) * 64.0f, 64, 64);
                kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            }
        }, layout));
    });
    CaseSpec inline_block = spec_of("inline_uniform_block");
    inline_block.min_version(1, 3);
    Cases::add(
        std::move(inline_block),
        [](Rig& rig, CaptureSet& out) {
            Kit& kit = rig.kit;
            const UniformData data{{1.0f, 0.9f, 0.5f, 1.0f}, {3.0f, 3.0f, 0, 0}};
            Basic b = make_basic(rig, data);
            const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK, sizeof(UniformData), VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                                                  {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
            VkDescriptorPoolInlineUniformBlockCreateInfo inline_info{};
            inline_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_INLINE_UNIFORM_BLOCK_CREATE_INFO;
            inline_info.maxInlineUniformBlockBindings = 1;
            const VkDescriptorPool pool = make_pool(rig, {{VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK, sizeof(UniformData)}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}}, 0, &inline_info, 1);
            const VkDescriptorSet set = alloc_set(rig, pool, set_layout);
            VkWriteDescriptorSetInlineUniformBlock block{};
            block.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK;
            block.dataSize = sizeof data;
            block.pData = &data;
            VkDescriptorImageInfo ii{b.sampler, b.texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet writes[2]{};
            writes[0].sType = writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].pNext = &block;
            writes[0].dstSet = writes[1].dstSet = set;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = sizeof data;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].pImageInfo = &ii;
            kit.api.vkUpdateDescriptorSets(kit.device, 2, writes, 0, nullptr);
            const VkPipelineLayout layout = rig.playout({set_layout});
            const VkPipeline pipeline = rig.pipe(basic_pipe(rig, b, layout, "desc_inline.v13.frag"));
            out.add("", rig.draw(*b.target, pipeline, nullptr, [&](VkCommandBuffer cb) { kit.api.vkCmdDraw(cb, 3, 1, 0, 0); }, layout, {set}));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
            return f.inlineUniformBlock != VK_FALSE ? std::string() : std::string("inlineUniformBlock");
        });
    CaseSpec indexing = spec_of("descriptor_indexing_runtime_array");
    indexing.min_version(1, 2);
    Cases::add(
        std::move(indexing),
        [](Rig& rig, CaptureSet& out) {
            Kit& kit = rig.kit;
            Target& target = rig.make_target(TargetDesc{});
            std::vector<Texture> textures;
            for (unsigned i = 0; i < 6; ++i) {
                textures.push_back(rig.sampled(8, 8, VK_FORMAT_R8G8B8A8_UNORM, Pattern::checker(8, 8, 2, Pattern::pack(60 + 30 * i, 230 - 30 * i, 120), Pattern::pack(250, 40 * i, 60 + 20 * i))));
            }
            const VkSampler sampler = rig.sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);
            const VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
            VkDescriptorSetLayoutBindingFlagsCreateInfo flag_info{};
            flag_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            flag_info.bindingCount = 1;
            flag_info.pBindingFlags = &flags;
            const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}}, 0, &flag_info);
            const VkDescriptorPool pool = make_pool(rig, {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8}}, 0, nullptr, 1);
            const std::uint32_t variable_count = 8;
            VkDescriptorSetVariableDescriptorCountAllocateInfo variable{};
            variable.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
            variable.descriptorSetCount = 1;
            variable.pDescriptorCounts = &variable_count;
            const VkDescriptorSet set = alloc_set(rig, pool, set_layout, &variable);
            std::vector<VkDescriptorImageInfo> infos;
            for (const Texture& t : textures) {
                infos.push_back({sampler, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            }
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = set;
            write.dstBinding = 0;
            write.descriptorCount = 6;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = infos.data();
            kit.api.vkUpdateDescriptorSets(kit.device, 1, &write, 0, nullptr);
            const VkPipelineLayout layout = rig.playout({set_layout}, 16, VK_SHADER_STAGE_FRAGMENT_BIT);
            PipelineDesc pipe;
            pipe.vertex = rig.sh("full.vert");
            pipe.fragment = rig.sh("desc_array.v12.frag");
            pipe.layout = layout;
            pipe.pass = target.clear_pass;
            const VkPipeline pipeline = rig.pipe(pipe);
            out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
                const float count[4] = {6, 0, 0, 0};
                rig.push(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, count, 16);
                kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            }, layout, {set}));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
            if (f.runtimeDescriptorArray == VK_FALSE || f.descriptorBindingPartiallyBound == VK_FALSE || f.descriptorBindingVariableDescriptorCount == VK_FALSE) {
                return "descriptor indexing runtime arrays";
            }
            return f.shaderSampledImageArrayNonUniformIndexing != VK_FALSE ? std::string() : std::string("shaderSampledImageArrayNonUniformIndexing");
        });
    CaseSpec address = spec_of("buffer_device_address_vertices");
    address.min_version(1, 2);
    Cases::add(
        std::move(address),
        [](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            const float data[24] = {-0.8f, -0.8f, 0, 0, 1, 0.2f, 0.2f, 1, 0.8f, -0.7f, 0, 0, 0.2f, 1, 0.3f, 1, 0.0f, 0.8f, 0, 0, 0.3f, 0.3f, 1, 1};
            Buffer verts = rig.blank(sizeof data, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, true);
            std::memcpy(verts.mapped, data, sizeof data);
            const VkPipelineLayout layout = rig.playout({}, 8, VK_SHADER_STAGE_VERTEX_BIT);
            PipelineDesc pipe;
            pipe.vertex = rig.sh("devaddr.v12.vert");
            pipe.fragment = rig.sh("col4.frag");
            pipe.layout = layout;
            pipe.pass = target.clear_pass;
            const VkPipeline pipeline = rig.pipe(pipe);
            out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
                rig.push(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, &verts.address, 8);
                rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            }, layout));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
            return f.bufferDeviceAddress != VK_FALSE ? std::string() : std::string("bufferDeviceAddress");
        });
}

class SetReuseScene : public RigScene {
protected:
    void init(Rig& rig) override {
        const UniformData data{{1, 1, 1, 1}, {2, 2, 0, 0}};
        basic_ = make_basic(rig, data);
        set_layout_ = rig.dlayout(basic_bindings());
        pool_ = make_pool(rig, {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}}, 0, nullptr, 1);
        set_ = alloc_set(rig, pool_, set_layout_);
        write_basic(rig, set_, basic_.uniform, basic_);
        layout_ = rig.playout({set_layout_});
        pipeline_ = rig.pipe(basic_pipe(rig, basic_, layout_));
    }

    void step(Rig& rig, unsigned frame) override {
        const UniformData data{{0.4f + 0.2f * static_cast<float>(frame), 1.0f - 0.2f * static_cast<float>(frame), 0.6f, 1.0f},
                               {1.0f + static_cast<float>(frame), 2.0f + 0.5f * static_cast<float>(frame), 0, 0}};
        std::string error;
        rig.must(rig.kit.write(basic_.uniform, 0, &data, sizeof data, error), error);
        rig.draw(*basic_.target, pipeline_, nullptr, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0); }, layout_, {set_});
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*basic_.target)); }

private:
    Basic basic_;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

const int registered = (register_types_and_sets(), register_advanced(),
                        Cases::add_class<SetReuseScene>(spec_of("set_reuse_across_frames").frames(4, {0, 1, 2, 3})), 0);

}  // namespace

}  // namespace lsw::render::vk
