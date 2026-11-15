#include "render/common/pattern.hpp"
#include "render/vk/quadkit.hpp"
#include "render/vk/shader_registry.hpp"

#include <cstring>

namespace lsw::render::vk {

namespace {

const float kClear[4] = {0.05f, 0.06f, 0.1f, 1.0f};

CaseSpec spec_of(const std::string& group, const std::string& name) {
    CaseSpec spec(group, name);
    spec.tolerance(Tolerance::exact());
    return spec;
}

std::vector<Vertex2> three_triangles() {
    return {vertex(-0.9f, -0.8f, 0), vertex(-0.1f, -0.7f, 1), vertex(-0.5f, 0.0f, 2), vertex(0.1f, -0.1f, 3), vertex(0.9f, 0.0f, 4),
            vertex(0.5f, 0.8f, 5),   vertex(-0.8f, 0.2f, 6),  vertex(-0.2f, 0.3f, 7), vertex(-0.5f, 0.9f, 0)};
}

void register_library() {
    CaseSpec spec = spec_of("pipe", "graphics_pipeline_library_link");
    spec.extension("VK_EXT_graphics_pipeline_library").extension("VK_KHR_pipeline_library");
    Cases::add(
        std::move(spec),
        [](Rig& rig, CaptureSet& out) {
            Kit& kit = rig.kit;
            Target& target = rig.make_target(TargetDesc{});
            std::copy(kClear, kClear + 4, target.clear_color);
            const auto tris = three_triangles();
            const Buffer vb = rig.buffer(tris, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            const VkPipelineLayout layout = rig.empty_layout();
            const VkVertexInputBindingDescription binding{0, sizeof(Vertex2), VK_VERTEX_INPUT_RATE_VERTEX};
            const VkVertexInputAttributeDescription attrs[2] = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 8}};
            VkPipelineVertexInputStateCreateInfo vertex_input{};
            vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertex_input.vertexBindingDescriptionCount = 1;
            vertex_input.pVertexBindingDescriptions = &binding;
            vertex_input.vertexAttributeDescriptionCount = 2;
            vertex_input.pVertexAttributeDescriptions = attrs;
            VkPipelineInputAssemblyStateCreateInfo assembly{};
            assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{};
            viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport.viewportCount = viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{};
            raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster.lineWidth = 1.0f;
            raster.cullMode = VK_CULL_MODE_NONE;
            const VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{};
            dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount = 2;
            dynamic.pDynamicStates = dyn;
            VkPipelineMultisampleStateCreateInfo ms{};
            ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineDepthStencilStateCreateInfo ds{};
            ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            VkPipelineColorBlendAttachmentState att{};
            att.colorWriteMask = 0xf;
            VkPipelineColorBlendStateCreateInfo blend{};
            blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend.attachmentCount = 1;
            blend.pAttachments = &att;
            const auto make_stage = [](VkShaderStageFlagBits stage, VkShaderModule module) {
                VkPipelineShaderStageCreateInfo s{};
                s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                s.stage = stage;
                s.module = module;
                s.pName = "main";
                return s;
            };
            const auto make_library = [&](VkGraphicsPipelineLibraryFlagsEXT flags, const std::function<void(VkGraphicsPipelineCreateInfo&)>& fill) {
                VkGraphicsPipelineLibraryCreateInfoEXT lib{};
                lib.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT;
                lib.flags = flags;
                VkGraphicsPipelineCreateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                info.pNext = &lib;
                info.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
                info.layout = layout;
                info.renderPass = target.clear_pass;
                info.basePipelineIndex = -1;
                fill(info);
                VkPipeline pipeline = VK_NULL_HANDLE;
                rig.vk_ok(kit.api.vkCreateGraphicsPipelines(kit.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline), "vkCreateGraphicsPipelines library");
                kit.defer([&kit, pipeline] { kit.api.vkDestroyPipeline(kit.device, pipeline, nullptr); });
                return pipeline;
            };
            const VkPipelineShaderStageCreateInfo vs = make_stage(VK_SHADER_STAGE_VERTEX_BIT, rig.sh("pos2c.vert"));
            const VkPipelineShaderStageCreateInfo fs_a = make_stage(VK_SHADER_STAGE_FRAGMENT_BIT, rig.sh("col4.frag"));
            const VkPipelineShaderStageCreateInfo fs_b = make_stage(VK_SHADER_STAGE_FRAGMENT_BIT, rig.sh("addtint.frag"));
            const VkPipeline lib_input = make_library(VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT, [&](VkGraphicsPipelineCreateInfo& i) {
                i.pVertexInputState = &vertex_input;
                i.pInputAssemblyState = &assembly;
            });
            const VkPipeline lib_raster = make_library(VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT, [&](VkGraphicsPipelineCreateInfo& i) {
                i.stageCount = 1;
                i.pStages = &vs;
                i.pViewportState = &viewport;
                i.pRasterizationState = &raster;
                i.pDynamicState = &dynamic;
            });
            const VkPipeline lib_frag_a = make_library(VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT, [&](VkGraphicsPipelineCreateInfo& i) {
                i.stageCount = 1;
                i.pStages = &fs_a;
                i.pMultisampleState = &ms;
                i.pDepthStencilState = &ds;
            });
            const VkPipeline lib_frag_b = make_library(VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT, [&](VkGraphicsPipelineCreateInfo& i) {
                i.stageCount = 1;
                i.pStages = &fs_b;
                i.pMultisampleState = &ms;
                i.pDepthStencilState = &ds;
            });
            const VkPipeline lib_output = make_library(VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT, [&](VkGraphicsPipelineCreateInfo& i) {
                i.pColorBlendState = &blend;
                i.pMultisampleState = &ms;
            });
            VkPipeline linked[2] = {};
            const VkPipeline fragment_libs[2] = {lib_frag_a, lib_frag_b};
            for (int k = 0; k < 2; ++k) {
                const VkPipeline libs[4] = {lib_input, lib_raster, fragment_libs[k], lib_output};
                VkPipelineLibraryCreateInfoKHR lib_info{};
                lib_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR;
                lib_info.libraryCount = 4;
                lib_info.pLibraries = libs;
                VkGraphicsPipelineCreateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                info.pNext = &lib_info;
                info.layout = layout;
                info.basePipelineIndex = -1;
                rig.vk_ok(kit.api.vkCreateGraphicsPipelines(kit.device, VK_NULL_HANDLE, 1, &info, nullptr, &linked[k]), "vkCreateGraphicsPipelines link");
                const VkPipeline pipeline = linked[k];
                kit.defer([&kit, pipeline] { kit.api.vkDestroyPipeline(kit.device, pipeline, nullptr); });
            }
            out.add("", rig.draw(target, linked[0], &vb, [&](VkCommandBuffer cb) {
                rig.viewport(cb, 0, 0, 128, 64);
                kit.api.vkCmdDraw(cb, 9, 1, 0, 0);
                rig.bind_graphics(cb, linked[1]);
                rig.viewport(cb, 0, 64, 128, 64);
                kit.api.vkCmdDraw(cb, 9, 1, 0, 0);
            }));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT);
            return f.graphicsPipelineLibrary != VK_FALSE ? std::string() : std::string("graphicsPipelineLibrary");
        });
}

void register_shader_object() {
    CaseSpec spec = spec_of("pipe", "shader_objects_dynamic_rendering");
    spec.extension("VK_EXT_shader_object").min_version(1, 3);
    Cases::add(
        std::move(spec),
        [](Rig& rig, CaptureSet& out) {
            Kit& kit = rig.kit;
            TextureDesc desc;
            desc.width = desc.height = 128;
            desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            const Texture color = rig.texture(desc);
            const auto tris = three_triangles();
            const Buffer vb = rig.buffer(tris, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            std::vector<std::uint32_t> vs_code;
            std::vector<std::uint32_t> fs_code;
            rig.must(ShaderRegistry::find("pos2c.vert", vs_code) && ShaderRegistry::find("col4.frag", fs_code), "shader code missing");
            VkShaderCreateInfoEXT infos[2]{};
            infos[0].sType = infos[1].sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT;
            infos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            infos[0].nextStage = VK_SHADER_STAGE_FRAGMENT_BIT;
            infos[0].codeType = infos[1].codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
            infos[0].codeSize = vs_code.size() * 4;
            infos[0].pCode = vs_code.data();
            infos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            infos[1].codeSize = fs_code.size() * 4;
            infos[1].pCode = fs_code.data();
            infos[0].pName = infos[1].pName = "main";
            VkShaderEXT shaders[2] = {};
            rig.vk_ok(kit.api.vkCreateShadersEXT(kit.device, 2, infos, nullptr, shaders), "vkCreateShadersEXT");
            kit.defer([&kit, shaders] {
                for (VkShaderEXT s : shaders) {
                    kit.api.vkDestroyShaderEXT(kit.device, s, nullptr);
                }
            });
            rig.run([&](VkCommandBuffer cb) {
                kit.barrier(cb, color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                VkRenderingAttachmentInfo ca{};
                ca.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                ca.imageView = color.view;
                ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                std::copy(kClear, kClear + 4, ca.clearValue.color.float32);
                VkRenderingInfo ri{};
                ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                ri.renderArea = {{0, 0}, {128, 128}};
                ri.layerCount = 1;
                ri.colorAttachmentCount = 1;
                ri.pColorAttachments = &ca;
                kit.api.vkCmdBeginRendering(cb, &ri);
                const VkShaderStageFlagBits stages[5] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                                                         VK_SHADER_STAGE_GEOMETRY_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
                const VkShaderEXT bound[5] = {shaders[0], VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, shaders[1]};
                kit.api.vkCmdBindShadersEXT(cb, 5, stages, bound);
                const VkViewport vp{0, 0, 128, 128, 0, 1};
                const VkRect2D sc{{0, 0}, {128, 128}};
                kit.api.vkCmdSetViewportWithCount(cb, 1, &vp);
                kit.api.vkCmdSetScissorWithCount(cb, 1, &sc);
                kit.api.vkCmdSetRasterizerDiscardEnable(cb, VK_FALSE);
                kit.api.vkCmdSetCullMode(cb, VK_CULL_MODE_NONE);
                kit.api.vkCmdSetFrontFace(cb, VK_FRONT_FACE_COUNTER_CLOCKWISE);
                kit.api.vkCmdSetDepthTestEnable(cb, VK_FALSE);
                kit.api.vkCmdSetDepthWriteEnable(cb, VK_FALSE);
                kit.api.vkCmdSetDepthCompareOp(cb, VK_COMPARE_OP_ALWAYS);
                kit.api.vkCmdSetDepthBiasEnable(cb, VK_FALSE);
                kit.api.vkCmdSetDepthBoundsTestEnable(cb, VK_FALSE);
                kit.api.vkCmdSetStencilTestEnable(cb, VK_FALSE);
                kit.api.vkCmdSetPrimitiveTopology(cb, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                kit.api.vkCmdSetPrimitiveRestartEnable(cb, VK_FALSE);
                kit.api.vkCmdSetPolygonModeEXT(cb, VK_POLYGON_MODE_FILL);
                kit.api.vkCmdSetRasterizationSamplesEXT(cb, VK_SAMPLE_COUNT_1_BIT);
                const VkSampleMask mask = 0xffffffffu;
                kit.api.vkCmdSetSampleMaskEXT(cb, VK_SAMPLE_COUNT_1_BIT, &mask);
                kit.api.vkCmdSetAlphaToCoverageEnableEXT(cb, VK_FALSE);
                kit.api.vkCmdSetAlphaToOneEnableEXT(cb, VK_FALSE);
                kit.api.vkCmdSetDepthClampEnableEXT(cb, VK_FALSE);
                kit.api.vkCmdSetLogicOpEnableEXT(cb, VK_FALSE);
                const VkBool32 blend_enable = VK_FALSE;
                kit.api.vkCmdSetColorBlendEnableEXT(cb, 0, 1, &blend_enable);
                const VkColorBlendEquationEXT equation{VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD};
                kit.api.vkCmdSetColorBlendEquationEXT(cb, 0, 1, &equation);
                const VkColorComponentFlags write = 0xf;
                kit.api.vkCmdSetColorWriteMaskEXT(cb, 0, 1, &write);
                VkVertexInputBindingDescription2EXT bind{};
                bind.sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT;
                bind.stride = sizeof(Vertex2);
                bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
                bind.divisor = 1;
                VkVertexInputAttributeDescription2EXT attrs[2]{};
                attrs[0] = {VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT, nullptr, 0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
                attrs[1] = {VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT, nullptr, 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 8};
                kit.api.vkCmdSetVertexInputEXT(cb, 1, &bind, 2, attrs);
                rig.bind_vertex(cb, vb);
                kit.api.vkCmdDraw(cb, 9, 1, 0, 0);
                kit.api.vkCmdEndRendering(cb);
            });
            out.add("", rig.read(color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceShaderObjectFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT);
            return f.shaderObject != VK_FALSE ? std::string() : std::string("shaderObject");
        });
}

struct DescriptorBufferData {
    float tint[4];
    float params[4];
};

void register_descriptor_buffer() {
    CaseSpec spec = spec_of("desc", "descriptor_buffer_ubo_and_sampler");
    spec.extension("VK_EXT_descriptor_buffer").min_version(1, 3);
    Cases::add(
        std::move(spec),
        [](Rig& rig, CaptureSet& out) {
            Kit& kit = rig.kit;
            Target& target = rig.make_target(TargetDesc{});
            const DescriptorBufferData data{{0.9f, 0.8f, 1.0f, 1.0f}, {3.0f, 3.0f, 0, 0}};
            Buffer ubo = rig.blank(sizeof data, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, true);
            std::memcpy(ubo.mapped, &data, sizeof data);
            const Texture texture = rig.sampled(16, 16, VK_FORMAT_R8G8B8A8_UNORM, Pattern::checker(16, 16, 2, Pattern::pack(240, 200, 60), Pattern::pack(40, 80, 200)));
            const VkSampler sampler = rig.sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);
            const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                                                  {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}},
                                                                 VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);
            const auto props = kit.query_properties<VkPhysicalDeviceDescriptorBufferPropertiesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT);
            VkDeviceSize layout_size = 0;
            kit.api.vkGetDescriptorSetLayoutSizeEXT(kit.device, set_layout, &layout_size);
            VkDeviceSize offsets[2] = {};
            kit.api.vkGetDescriptorSetLayoutBindingOffsetEXT(kit.device, set_layout, 0, &offsets[0]);
            kit.api.vkGetDescriptorSetLayoutBindingOffsetEXT(kit.device, set_layout, 1, &offsets[1]);
            const VkDeviceSize alignment = std::max<VkDeviceSize>(props.descriptorBufferOffsetAlignment, 1);
            const VkDeviceSize buffer_size = (layout_size + alignment - 1) / alignment * alignment + alignment;
            Buffer descriptors = rig.blank(buffer_size, VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT, true, true);
            auto* base = static_cast<std::uint8_t*>(descriptors.mapped);
            VkDescriptorAddressInfoEXT address{};
            address.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
            address.address = ubo.address;
            address.range = sizeof data;
            VkDescriptorGetInfoEXT get{};
            get.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
            get.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            get.data.pUniformBuffer = &address;
            kit.api.vkGetDescriptorEXT(kit.device, &get, props.uniformBufferDescriptorSize, base + offsets[0]);
            const VkDescriptorImageInfo image{sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            get.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            get.data.pCombinedImageSampler = &image;
            kit.api.vkGetDescriptorEXT(kit.device, &get, props.combinedImageSamplerDescriptorSize, base + offsets[1]);
            const VkPipelineLayout layout = rig.playout({set_layout});
            PipelineDesc pipe;
            pipe.vertex = rig.sh("full.vert");
            pipe.fragment = rig.sh("desc_basic.frag");
            pipe.layout = layout;
            pipe.pass = target.clear_pass;
            pipe.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
            const VkPipeline pipeline = rig.pipe(pipe);
            out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
                VkDescriptorBufferBindingInfoEXT binding{};
                binding.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
                binding.address = descriptors.address;
                binding.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;
                kit.api.vkCmdBindDescriptorBuffersEXT(cb, 1, &binding);
                const std::uint32_t index = 0;
                const VkDeviceSize offset = 0;
                kit.api.vkCmdSetDescriptorBufferOffsetsEXT(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &index, &offset);
                kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            }, layout));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceDescriptorBufferFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT);
            if (f.descriptorBuffer == VK_FALSE) {
                return "descriptorBuffer";
            }
            const auto v12 = kit.query_features<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
            return v12.bufferDeviceAddress != VK_FALSE ? std::string() : std::string("bufferDeviceAddress");
        });
}

std::string eds3_caps(Kit& kit, int which) {
    const auto f = kit.query_features<VkPhysicalDeviceExtendedDynamicState3FeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT);
    switch (which) {
        case 0: return f.extendedDynamicState3ColorBlendEnable && f.extendedDynamicState3ColorBlendEquation && f.extendedDynamicState3ColorWriteMask ? std::string() : std::string("eds3 blend state");
        case 1: return f.extendedDynamicState3DepthClampEnable != VK_FALSE ? std::string() : std::string("extendedDynamicState3DepthClampEnable");
        default: return f.extendedDynamicState3LogicOpEnable != VK_FALSE ? std::string() : std::string("extendedDynamicState3LogicOpEnable");
    }
}

void register_eds3() {
    CaseSpec blend = spec_of("pipe", "dyn_eds3_blend_equation_write_mask");
    blend.extension("VK_EXT_extended_dynamic_state3");
    Cases::add(std::move(blend), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        Target& target = rig.make_target(TargetDesc{});
        QuadKit quads(rig);
        PipelineDesc bg = quads.desc(target);
        bg.vertex = rig.sh("full.vert");
        bg.fragment = rig.sh("grad.frag");
        bg.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        PipelineDesc fg = quads.desc(target);
        fg.dynamic = {VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT, VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT, VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT};
        const VkPipeline p_bg = rig.pipe(bg);
        const VkPipeline p_fg = rig.pipe(fg);
        out.add("", rig.draw(target, p_bg, nullptr, [&](VkCommandBuffer cb) {
            kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            rig.bind_graphics(cb, p_fg);
            const VkColorBlendEquationEXT eqs[4] = {
                {VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD},
                {VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_MAX},
                {VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD},
                {VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD}};
            const VkColorComponentFlags masks[4] = {0xf, 0xf, 0x7, 0x3};
            for (unsigned i = 0; i < 4; ++i) {
                const VkBool32 enable = i == 1 ? VK_FALSE : VK_TRUE;
                kit.api.vkCmdSetColorBlendEnableEXT(cb, 0, 1, &enable);
                kit.api.vkCmdSetColorBlendEquationEXT(cb, 0, 1, &eqs[i]);
                kit.api.vkCmdSetColorWriteMaskEXT(cb, 0, 1, &masks[i]);
                const float color[4] = {0.9f - 0.1f * static_cast<float>(i), 0.5f, 0.3f + 0.2f * static_cast<float>(i), 0.6f};
                quads.quad(cb, -0.95f + 0.5f * static_cast<float>(i), -0.8f, 0.44f, 1.6f, color);
            }
        }, quads.layout));
    }, [](Kit& kit) { return eds3_caps(kit, 0); });
    CaseSpec clamp = spec_of("pipe", "dyn_eds3_depth_clamp_enable");
    clamp.extension("VK_EXT_extended_dynamic_state3").feature("depthClamp");
    Cases::add(std::move(clamp), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        TargetDesc td;
        td.depth = VK_FORMAT_D32_SFLOAT;
        Target& target = rig.make_target(td);
        std::copy(kClear, kClear + 4, target.clear_color);
        QuadKit quads(rig);
        PipelineDesc pipe = quads.desc(target);
        pipe.depth_test = true;
        pipe.dynamic = {VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT};
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            for (unsigned i = 0; i < 2; ++i) {
                kit.api.vkCmdSetDepthClampEnableEXT(cb, i == 0 ? VK_FALSE : VK_TRUE);
                rig.viewport(cb, static_cast<float>(i) * 64.0f, 0, 64, 128);
                quads.quad(cb, -0.9f, -0.9f, 1.8f, 1.8f, palette(i + 1), -0.3f, 1.6f);
            }
        }, quads.layout));
    }, [](Kit& kit) { return eds3_caps(kit, 1); });
    CaseSpec logic = spec_of("pipe", "dyn_eds3_logic_op_enable");
    logic.extension("VK_EXT_extended_dynamic_state3").extension("VK_EXT_extended_dynamic_state2").feature("logicOp");
    Cases::add(std::move(logic), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        Target& target = rig.make_target(TargetDesc{});
        QuadKit quads(rig);
        PipelineDesc bg = quads.desc(target);
        bg.vertex = rig.sh("full.vert");
        bg.fragment = rig.sh("grad.frag");
        bg.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        PipelineDesc fg = quads.desc(target);
        fg.logic = VK_LOGIC_OP_XOR;
        fg.dynamic = {VK_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT};
        const VkPipeline p_bg = rig.pipe(bg);
        const VkPipeline p_fg = rig.pipe(fg);
        out.add("", rig.draw(target, p_bg, nullptr, [&](VkCommandBuffer cb) {
            kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            rig.bind_graphics(cb, p_fg);
            for (unsigned i = 0; i < 4; ++i) {
                kit.api.vkCmdSetLogicOpEnableEXT(cb, i % 2 == 0 ? VK_TRUE : VK_FALSE);
                quads.quad(cb, -0.95f + 0.5f * static_cast<float>(i), -0.8f, 0.44f, 1.6f, palette(i));
            }
        }, quads.layout));
    }, [](Kit& kit) { return eds3_caps(kit, 2); });
    CaseSpec write_enable = spec_of("pipe", "dyn_color_write_enable_mrt");
    write_enable.extension("VK_EXT_color_write_enable").feature("independentBlend");
    Cases::add(
        std::move(write_enable),
        [](Rig& rig, CaptureSet& out) {
            Kit& kit = rig.kit;
            TargetDesc td;
            td.colors = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
            Target& target = rig.make_target(td);
            const auto tris = three_triangles();
            const Buffer vb = rig.buffer(tris, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            PipelineDesc pipe = rig.pos2c(target);
            pipe.fragment = rig.sh("mrt2.frag");
            pipe.dynamic = {VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT};
            const VkPipeline pipeline = rig.pipe(pipe);
            rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
                const VkBool32 both[2] = {VK_TRUE, VK_TRUE};
                kit.api.vkCmdSetColorWriteEnableEXT(cb, 2, both);
                rig.viewport(cb, 0, 0, 128, 64);
                kit.api.vkCmdDraw(cb, 9, 1, 0, 0);
                const VkBool32 second_only[2] = {VK_FALSE, VK_TRUE};
                kit.api.vkCmdSetColorWriteEnableEXT(cb, 2, second_only);
                rig.viewport(cb, 0, 64, 128, 64);
                kit.api.vkCmdDraw(cb, 9, 1, 0, 0);
            });
            out.add("c0", rig.color(target, 0));
            out.add("c1", rig.color(target, 1));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceColorWriteEnableFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT);
            return f.colorWriteEnable != VK_FALSE ? std::string() : std::string("colorWriteEnable");
        });
}

const int registered = (register_library(), register_shader_object(), register_descriptor_buffer(), register_eds3(), 0);

}  // namespace

}  // namespace lsw::render::vk
