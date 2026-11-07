#include "sanity/vk/compute_checks.hpp"

#include "sanity/vk/kit.hpp"
#include "support/vulkan_image_io.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <set>

namespace lsw::suites::sanity::vk {

namespace {

struct OpsRig {
    VulkanCompute program;
    VulkanBuffer input;
    VulkanBuffer output;
    VulkanBuffer params;
};

bool open_ops(VulkanRig& rig, OpsRig& ops, std::size_t words, Problems& problems, const std::string& what) {
    const std::size_t padded = (words + 63) / 64 * 64;
    return Kit::succeeds(problems, what + " pipeline",
                         ops.program.create(rig, VulkanShaders::words(VulkanShader::Ops),
                                            {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}}, 8)) &&
           Kit::succeeds(problems, what + " input buffer", Kit::host_buffer(rig, ops.input, std::max<std::size_t>(padded, 64) * 4)) &&
           Kit::succeeds(problems, what + " output buffer", Kit::host_buffer(rig, ops.output, std::max<std::size_t>(padded, 64) * 4)) &&
           Kit::succeeds(problems, what + " parameter buffer", Kit::host_buffer(rig, ops.params, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT));
}

void bind_all(OpsRig& ops) {
    ops.program.write_buffer(0, ops.input.buffer);
    ops.program.write_buffer(1, ops.output.buffer);
    ops.program.write_buffer(2, ops.params.buffer);
}

void set_params(OpsRig& ops, std::uint32_t mul, std::uint32_t add, std::uint32_t count) {
    ops.params.words()[0] = mul;
    ops.params.words()[1] = add;
    ops.params.words()[2] = count;
    ops.params.words()[3] = 0;
    ops.params.publish();
}

std::vector<std::uint32_t> read_words(OpsRig& ops, std::size_t count) {
    ops.output.acquire();
    return std::vector<std::uint32_t>(ops.output.words(), ops.output.words() + count);
}

}  // namespace

Outcome ComputeChecks::vs_cpu(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        for (const std::uint32_t groups : {1u, 3u, 17u, 256u}) {
            const std::size_t words = static_cast<std::size_t>(groups) * 64;
            const std::size_t count = words - 5;
            OpsRig ops;
            if (!open_ops(rig, ops, words, problems, "arithmetic " + std::to_string(groups) + " groups")) {
                continue;
            }
            bind_all(ops);
            const std::vector<std::uint32_t> input = Kit::words(0x9000 + groups, words);
            std::memcpy(ops.input.mapped, input.data(), words * 4);
            ops.input.publish();
            ops.output.fill_value(0xCD);
            ops.output.publish();
            set_params(ops, 0x01000193u, 0x811C9DC5u, static_cast<std::uint32_t>(count));
            const std::uint32_t push[2] = {0, groups};
            if (Kit::succeeds(problems, "dispatch of " + std::to_string(groups) + " groups", ops.program.run(push, sizeof(push), groups))) {
                std::vector<std::uint32_t> expected(words, 0xCDCDCDCDu);
                for (std::size_t i = 0; i < count; ++i) {
                    expected[i] = input[i] * 0x01000193u + 0x811C9DC5u + groups;
                }
                Kit::same_words(context, problems, "SSBO/UBO/push constant result for " + std::to_string(groups) + " groups", expected, read_words(ops, words));
            }
        }
        {
            const std::uint32_t groups = 33;
            const std::size_t words = static_cast<std::size_t>(groups) * 64;
            OpsRig ops;
            if (open_ops(rig, ops, words, problems, "shared memory")) {
                bind_all(ops);
                const std::vector<std::uint32_t> input = Kit::words(0x9100, words);
                std::memcpy(ops.input.mapped, input.data(), words * 4);
                ops.input.publish();
                ops.output.fill_value(0xCD);
                ops.output.publish();
                set_params(ops, 1, 0, static_cast<std::uint32_t>(words));
                const std::uint32_t push[2] = {1, 0};
                if (Kit::succeeds(problems, "shared memory dispatch", ops.program.run(push, sizeof(push), groups))) {
                    std::vector<std::uint32_t> expected(words);
                    for (std::size_t i = 0; i < words; ++i) {
                        expected[i] = input[i / 64 * 64 + 63 - i % 64];
                    }
                    Kit::same_words(context, problems, "workgroup memory reversal", expected, read_words(ops, words));
                }
            }
        }
        {
            const std::uint32_t groups = 50;
            const std::size_t words = static_cast<std::size_t>(groups) * 64;
            OpsRig ops;
            if (open_ops(rig, ops, words, problems, "atomics")) {
                bind_all(ops);
                const std::vector<std::uint32_t> input = Kit::words(0x9200, words);
                std::memcpy(ops.input.mapped, input.data(), words * 4);
                ops.input.publish();
                std::memset(ops.output.mapped, 0, ops.output.size);
                ops.output.publish();
                set_params(ops, 1, 0, static_cast<std::uint32_t>(words));
                const std::uint32_t push[2] = {2, 0};
                if (Kit::succeeds(problems, "atomic add dispatch", ops.program.run(push, sizeof(push), groups))) {
                    std::vector<std::uint32_t> expected(words, 0);
                    for (std::size_t i = 0; i < words; ++i) {
                        ++expected[input[i] & 15u];
                    }
                    std::vector<std::uint32_t> actual = read_words(ops, 16);
                    expected.resize(16);
                    Kit::same_words(context, problems, "atomicAdd histogram", expected, actual);
                }
                ops.output.words()[0] = 0;
                ops.output.words()[1] = 0xFFFFFFFFu;
                ops.output.words()[2] = 0;
                ops.output.words()[3] = 0;
                ops.output.words()[4] = 0xFFFFFFFFu;
                ops.output.publish();
                const std::uint32_t other[2] = {3, 0};
                if (Kit::succeeds(problems, "atomic min/max/xor/or/and dispatch", ops.program.run(other, sizeof(other), groups))) {
                    std::vector<std::uint32_t> expected = {0, 0xFFFFFFFFu, 0, 0, 0xFFFFFFFFu};
                    for (const std::uint32_t value : input) {
                        expected[0] = std::max(expected[0], value);
                        expected[1] = std::min(expected[1], value);
                        expected[2] ^= value;
                        expected[3] |= value;
                        expected[4] &= value;
                    }
                    Kit::same_words(context, problems, "atomicMax/Min/Xor/Or/And", expected, read_words(ops, 5));
                }
            }
        }
        {
            OpsRig ops;
            const std::size_t words = 64 * 9;
            if (open_ops(rig, ops, words, problems, "zero-size and indirect")) {
                bind_all(ops);
                const std::vector<std::uint32_t> input = Kit::words(0x9300, words);
                std::memcpy(ops.input.mapped, input.data(), words * 4);
                ops.input.publish();
                set_params(ops, 5, 1, static_cast<std::uint32_t>(words));
                const std::uint32_t push[2] = {0, 0};
                ops.output.fill_value(0xCD);
                ops.output.publish();
                if (Kit::succeeds(problems, "zero-size dispatch", ops.program.run(push, sizeof(push), 0))) {
                    std::vector<std::uint32_t> expected(words, 0xCDCDCDCDu);
                    Kit::same_words(context, problems, "output after a zero-size dispatch", expected, read_words(ops, words));
                }
                for (const std::uint32_t groups : {0u, 5u, 9u}) {
                    VulkanBuffer arguments;
                    VulkanBuffer staging;
                    if (!Kit::succeeds(problems, "indirect arguments", arguments.create(rig, 64, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0,
                                                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false)) ||
                        !Kit::succeeds(problems, "indirect staging", Kit::host_buffer(rig, staging, 64))) {
                        continue;
                    }
                    const VkDispatchIndirectCommand command{groups, 1, 1};
                    std::memcpy(staging.mapped, &command, sizeof(command));
                    staging.publish();
                    if (!Kit::succeeds(problems, "indirect argument upload", Kit::copy(rig, staging.buffer, arguments.buffer, sizeof(command)))) {
                        continue;
                    }
                    ops.output.fill_value(0xCD);
                    ops.output.publish();
                    const VkResult result = rig.run([&](VkCommandBuffer commands) {
                        ops.program.bind(commands);
                        ops.program.push(commands, push, sizeof(push));
                        rig.fn.vkCmdDispatchIndirect(commands, arguments.buffer, 0);
                        VkMemoryBarrier barrier{};
                        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
                    });
                    if (Kit::succeeds(problems, "indirect dispatch of " + std::to_string(groups) + " groups", result)) {
                        std::vector<std::uint32_t> expected(words, 0xCDCDCDCDu);
                        for (std::size_t i = 0; i < static_cast<std::size_t>(groups) * 64 && i < words; ++i) {
                            expected[i] = input[i] * 5 + 1;
                        }
                        Kit::same_words(context, problems, "indirect dispatch of " + std::to_string(groups) + " groups", expected, read_words(ops, words));
                    }
                }
            }
        }
        const bool subgroup_arithmetic = rig.at_least(VK_API_VERSION_1_1) && (rig.subgroup.supportedOperations & VK_SUBGROUP_FEATURE_ARITHMETIC_BIT) != 0 &&
                                         (rig.subgroup.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0;
        context.measure("vk.subgroupSize", rig.subgroup.subgroupSize);
        if (subgroup_arithmetic) {
            VulkanCompute program;
            VulkanBuffer output;
            const std::uint32_t groups = 4;
            if (Kit::succeeds(problems, "subgroup pipeline", program.create(rig, VulkanShaders::words(VulkanShader::Subgroup), {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}}, 0)) &&
                Kit::succeeds(problems, "subgroup output", Kit::host_buffer(rig, output, groups * 64 * 8))) {
                output.fill_value(0xCD);
                output.publish();
                program.write_buffer(0, output.buffer);
                if (Kit::succeeds(problems, "subgroup dispatch", program.run(nullptr, 0, groups))) {
                    output.acquire();
                    const std::uint32_t size = rig.subgroup.subgroupSize;
                    for (std::uint32_t i = 0; i < groups * 64; ++i) {
                        const std::uint32_t sum = output.words()[i * 2];
                        const std::uint32_t reported = output.words()[i * 2 + 1];
                        if (reported != size) {
                            problems.add("invocation " + std::to_string(i) + " sees gl_SubgroupSize " + std::to_string(reported) + ", the device reports " + std::to_string(size));
                            break;
                        }
                        if (sum != std::min<std::uint32_t>(size, 64)) {
                            problems.add("invocation " + std::to_string(i) + " computed subgroupAdd(1) = " + std::to_string(sum) + ", expected " +
                                         std::to_string(std::min<std::uint32_t>(size, 64)));
                            break;
                        }
                    }
                }
            }
        } else {
            context.note("subgroup arithmetic is not reported for compute; subgroup case skipped");
        }
        if (rig.enabled_has("VK_KHR_push_descriptor") && rig.fn.vkCmdPushDescriptorSetKHR != nullptr) {
            const std::size_t words = 64 * 4;
            VulkanCompute program;
            VulkanBuffer input;
            VulkanBuffer output;
            VulkanBuffer params;
            if (Kit::succeeds(problems, "push descriptor pipeline",
                              program.create(rig, VulkanShaders::words(VulkanShader::Ops),
                                             {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}}, 8, nullptr, true)) &&
                Kit::succeeds(problems, "push descriptor input", Kit::host_buffer(rig, input, words * 4)) &&
                Kit::succeeds(problems, "push descriptor output", Kit::host_buffer(rig, output, words * 4)) &&
                Kit::succeeds(problems, "push descriptor parameters", Kit::host_buffer(rig, params, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT))) {
                const std::vector<std::uint32_t> data = Kit::words(0x9400, words);
                std::memcpy(input.mapped, data.data(), words * 4);
                input.publish();
                params.words()[0] = 7;
                params.words()[1] = 9;
                params.words()[2] = static_cast<std::uint32_t>(words);
                params.publish();
                output.fill_value(0xCD);
                output.publish();
                const VkDescriptorBufferInfo infos[3] = {{input.buffer, 0, VK_WHOLE_SIZE}, {output.buffer, 0, VK_WHOLE_SIZE}, {params.buffer, 0, 16}};
                VkWriteDescriptorSet writes[3]{};
                for (std::uint32_t i = 0; i < 3; ++i) {
                    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[i].dstBinding = i;
                    writes[i].descriptorCount = 1;
                    writes[i].descriptorType = i == 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    writes[i].pBufferInfo = &infos[i];
                }
                const std::uint32_t push[2] = {0, 3};
                const VkResult result = rig.run([&](VkCommandBuffer commands) {
                    program.bind(commands);
                    program.push(commands, push, sizeof(push));
                    rig.fn.vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, program.layout.get(), 0, 3, writes);
                    program.dispatch(commands, 4);
                    VkMemoryBarrier barrier{};
                    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                    rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
                });
                if (Kit::succeeds(problems, "push descriptor dispatch", result)) {
                    output.acquire();
                    std::vector<std::uint32_t> expected(words);
                    for (std::size_t i = 0; i < words; ++i) {
                        expected[i] = data[i] * 7 + 9 + 3;
                    }
                    Kit::same_words(context, problems, "push descriptor result", expected, std::vector<std::uint32_t>(output.words(), output.words() + words));
                }
            }
        }
        return {};
    });
}

Outcome ComputeChecks::pools_templates_samplers(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        const VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &binding;
        VulkanOwned<VkDescriptorSetLayout> layout;
        if (!Kit::succeeds(problems, "descriptor set layout", rig.make(layout, rig.fn.vkCreateDescriptorSetLayout, rig.fn.vkDestroyDescriptorSetLayout, layout_info))) {
            return {};
        }
        const std::uint32_t sets = 16;
        for (const bool freeable : {false, true}) {
            const std::string kind = freeable ? "pool with FREE_DESCRIPTOR_SET" : "pool without FREE_DESCRIPTOR_SET";
            const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, sets};
            VkDescriptorPoolCreateInfo pool_info{};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.flags = freeable ? VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT : 0;
            pool_info.maxSets = sets;
            pool_info.poolSizeCount = 1;
            pool_info.pPoolSizes = &size;
            VulkanOwned<VkDescriptorPool> pool;
            if (!Kit::succeeds(problems, kind, rig.make(pool, rig.fn.vkCreateDescriptorPool, rig.fn.vkDestroyDescriptorPool, pool_info))) {
                continue;
            }
            const VkDescriptorSetLayout layouts[sets] = {layout.get(), layout.get(), layout.get(), layout.get(), layout.get(), layout.get(), layout.get(), layout.get(),
                                                         layout.get(), layout.get(), layout.get(), layout.get(), layout.get(), layout.get(), layout.get(), layout.get()};
            VkDescriptorSet handles[sets] = {};
            VkDescriptorSetAllocateInfo allocate{};
            allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocate.descriptorPool = pool.get();
            allocate.descriptorSetCount = sets;
            allocate.pSetLayouts = layouts;
            if (!Kit::succeeds(problems, kind + " allocation of all " + std::to_string(sets) + " sets", rig.fn.vkAllocateDescriptorSets(rig.device, &allocate, handles))) {
                continue;
            }
            VkDescriptorSet extra = VK_NULL_HANDLE;
            allocate.descriptorSetCount = 1;
            Kit::expect(problems, kind + " allocation beyond maxSets", rig.fn.vkAllocateDescriptorSets(rig.device, &allocate, &extra),
                        {VK_ERROR_OUT_OF_POOL_MEMORY, VK_ERROR_FRAGMENTED_POOL});
            std::set<std::uint64_t> unique;
            for (VkDescriptorSet handle : handles) {
                unique.insert(reinterpret_cast<std::uint64_t>(handle));
            }
            if (unique.size() != sets) {
                problems.add(kind + " returned duplicate set handles");
            }
            if (freeable) {
                VkDescriptorSet to_free[sets / 2];
                for (std::uint32_t i = 0; i < sets / 2; ++i) {
                    to_free[i] = handles[i * 2];
                }
                Kit::succeeds(problems, "vkFreeDescriptorSets", rig.fn.vkFreeDescriptorSets(rig.device, pool.get(), sets / 2, to_free));
                allocate.descriptorSetCount = sets / 2;
                VkDescriptorSet again[sets / 2] = {};
                Kit::succeeds(problems, "allocation after freeing half of the sets", rig.fn.vkAllocateDescriptorSets(rig.device, &allocate, again));
            }
            Kit::succeeds(problems, kind + " reset", rig.fn.vkResetDescriptorPool(rig.device, pool.get(), 0));
            allocate.descriptorSetCount = sets;
            Kit::succeeds(problems, kind + " allocation of all sets after a reset", rig.fn.vkAllocateDescriptorSets(rig.device, &allocate, handles));
        }
        if (rig.fn.vkCreateDescriptorUpdateTemplate == nullptr || rig.fn.vkUpdateDescriptorSetWithTemplate == nullptr) {
            problems.add("descriptor update templates do not resolve on a Vulkan 1.1 device");
        } else {
            const std::size_t words = 256;
            VulkanCompute program;
            VulkanBuffer input;
            VulkanBuffer output;
            VulkanBuffer params;
            if (Kit::succeeds(problems, "template pipeline",
                              program.create(rig, VulkanShaders::words(VulkanShader::Ops),
                                             {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}}, 8)) &&
                Kit::succeeds(problems, "template input", Kit::host_buffer(rig, input, words * 4)) &&
                Kit::succeeds(problems, "template output", Kit::host_buffer(rig, output, words * 4)) &&
                Kit::succeeds(problems, "template parameters", Kit::host_buffer(rig, params, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT))) {
                struct Data {
                    VkDescriptorBufferInfo storage[2];
                    VkDescriptorBufferInfo uniform;
                } data{};
                data.storage[0] = {input.buffer, 0, VK_WHOLE_SIZE};
                data.storage[1] = {output.buffer, 0, VK_WHOLE_SIZE};
                data.uniform = {params.buffer, 0, 16};
                const VkDescriptorUpdateTemplateEntry entries[2] = {
                    {0, 0, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, offsetof(Data, storage), sizeof(VkDescriptorBufferInfo)},
                    {2, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, offsetof(Data, uniform), sizeof(VkDescriptorBufferInfo)}};
                VkDescriptorUpdateTemplateCreateInfo template_info{};
                template_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO;
                template_info.descriptorUpdateEntryCount = 2;
                template_info.pDescriptorUpdateEntries = entries;
                template_info.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
                template_info.descriptorSetLayout = program.set_layout.get();
                VkDescriptorUpdateTemplate update = VK_NULL_HANDLE;
                if (Kit::succeeds(problems, "descriptor update template", rig.fn.vkCreateDescriptorUpdateTemplate(rig.device, &template_info, nullptr, &update))) {
                    rig.fn.vkUpdateDescriptorSetWithTemplate(rig.device, program.set, update, &data);
                    const std::vector<std::uint32_t> values = Kit::words(0x9500, words);
                    std::memcpy(input.mapped, values.data(), words * 4);
                    input.publish();
                    params.words()[0] = 11;
                    params.words()[1] = 13;
                    params.words()[2] = static_cast<std::uint32_t>(words);
                    params.publish();
                    output.fill_value(0xCD);
                    output.publish();
                    const std::uint32_t push[2] = {0, 1};
                    if (Kit::succeeds(problems, "dispatch through a template-updated set", program.run(push, sizeof(push), 4))) {
                        output.acquire();
                        std::vector<std::uint32_t> expected(words);
                        for (std::size_t i = 0; i < words; ++i) {
                            expected[i] = values[i] * 11 + 13 + 1;
                        }
                        Kit::same_words(context, problems, "descriptor update template result", expected, std::vector<std::uint32_t>(output.words(), output.words() + words));
                    }
                    rig.fn.vkDestroyDescriptorUpdateTemplate(rig.device, update, nullptr);
                }
            }
        }
        {
            const std::size_t words = 256;
            const VkDeviceSize alignment = std::max<VkDeviceSize>(rig.props.limits.minUniformBufferOffsetAlignment, 16);
            VulkanCompute program;
            VulkanBuffer input;
            VulkanBuffer output;
            VulkanBuffer params;
            if (Kit::succeeds(problems, "dynamic uniform pipeline",
                              program.create(rig, VulkanShaders::words(VulkanShader::Ops),
                                             {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1}}, 8)) &&
                Kit::succeeds(problems, "dynamic input", Kit::host_buffer(rig, input, words * 4)) &&
                Kit::succeeds(problems, "dynamic output", Kit::host_buffer(rig, output, words * 4)) &&
                Kit::succeeds(problems, "dynamic parameters", Kit::host_buffer(rig, params, alignment * 2 + 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT))) {
                program.write_buffer(0, input.buffer);
                program.write_buffer(1, output.buffer);
                program.write_buffer(2, params.buffer, 0, 16);
                const std::vector<std::uint32_t> values = Kit::words(0x9600, words);
                std::memcpy(input.mapped, values.data(), words * 4);
                input.publish();
                std::memset(params.mapped, 0, params.size);
                params.words()[0] = 2;
                params.words()[1] = 0;
                params.words()[2] = static_cast<std::uint32_t>(words);
                std::uint32_t* second = reinterpret_cast<std::uint32_t*>(params.bytes() + alignment);
                second[0] = 9;
                second[1] = 100;
                second[2] = static_cast<std::uint32_t>(words);
                params.publish();
                for (const std::uint32_t pick : {1u, 0u}) {
                    program.dynamic_offsets = {static_cast<std::uint32_t>(pick * alignment)};
                    output.fill_value(0xCD);
                    output.publish();
                    const std::uint32_t push[2] = {0, 0};
                    if (Kit::succeeds(problems, "dispatch with a dynamic uniform offset", program.run(push, sizeof(push), 4))) {
                        output.acquire();
                        std::vector<std::uint32_t> expected(words);
                        for (std::size_t i = 0; i < words; ++i) {
                            expected[i] = pick == 1 ? values[i] * 9 + 100 : values[i] * 2;
                        }
                        Kit::same_words(context, problems, "dynamic uniform offset " + std::to_string(pick), expected,
                                        std::vector<std::uint32_t>(output.words(), output.words() + words));
                    }
                }
            }
        }
        const VkFormatProperties props = Kit::format_properties(rig, VK_FORMAT_R8G8B8A8_UNORM);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0) {
            VkSamplerCreateInfo sampler_info{};
            sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sampler_info.magFilter = VK_FILTER_NEAREST;
            sampler_info.minFilter = VK_FILTER_NEAREST;
            sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            VulkanOwned<VkSampler> sampler;
            VulkanImage image;
            VulkanOwned<VkImageView> view;
            VulkanCompute program;
            VulkanBuffer output;
            VkSampler immutable = VK_NULL_HANDLE;
            if (Kit::succeeds(problems, "immutable sampler", rig.make(sampler, rig.fn.vkCreateSampler, rig.fn.vkDestroySampler, sampler_info))) {
                immutable = sampler.get();
                VulkanBinding first;
                first.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                first.immutable = &immutable;
                if (Kit::succeeds(problems, "immutable sampler pipeline", program.create(rig, VulkanShaders::words(VulkanShader::Sample), {first, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}}, 20)) &&
                    Kit::succeeds(problems, "immutable sampler image", image.create_2d(rig, VK_FORMAT_R8G8B8A8_UNORM, 4, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) &&
                    Kit::succeeds(problems, "immutable sampler layout", VulkanImageIo::to_general(rig, image)) &&
                    Kit::succeeds(problems, "immutable sampler view", image.view(view, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT)) &&
                    Kit::succeeds(problems, "immutable sampler output", Kit::host_buffer(rig, output, 256))) {
                    const std::vector<std::uint8_t> texels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 17, 34, 51, 68};
                    ImageRegion region;
                    region.extent = {4, 1, 1};
                    if (Kit::succeeds(problems, "immutable sampler upload", VulkanImageIo::write(rig, image, region, texels))) {
                        program.write_image(0, view.get(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);
                        program.write_buffer(1, output.buffer);
                        output.fill_value(0xCD);
                        output.publish();
                        const float values[4] = {0.125f, 0.5f, 0.25f, 0.0f};
                        const std::uint32_t count = 4;
                        std::uint8_t push[20];
                        std::memcpy(push, values, 16);
                        std::memcpy(push + 16, &count, 4);
                        if (Kit::succeeds(problems, "immutable sampler dispatch", program.run(push, 20, 1))) {
                            output.acquire();
                            const float* got = reinterpret_cast<const float*>(output.bytes());
                            for (std::uint32_t i = 0; i < 16; ++i) {
                                const float expected = static_cast<float>(texels[i]) / 255.0f;
                                if (std::fabs(got[i] - expected) > 0.25f / 255.0f) {
                                    problems.add("immutable sampler: component " + std::to_string(i) + " is " + std::to_string(got[i]) + ", expected " + std::to_string(expected));
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::vk
