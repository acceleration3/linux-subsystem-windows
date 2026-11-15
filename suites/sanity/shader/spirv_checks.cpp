#include "sanity/shader/spirv_checks.hpp"

#include "sanity/vk/kit.hpp"
#include "support/pattern.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::shader {

namespace {

struct Specialization {
    std::uint32_t local;
    std::uint32_t mul;
    std::uint32_t add;
    std::int32_t mode;
};

std::uint32_t reverse_bits(std::uint32_t value) {
    std::uint32_t out = 0;
    for (int i = 0; i < 32; ++i) {
        out |= ((value >> i) & 1u) << (31 - i);
    }
    return out;
}

std::uint32_t expected_word(const Specialization& s, std::uint32_t input) {
    std::uint32_t v = input * s.mul + s.add;
    if (s.mode == 1) {
        v ^= v >> 7;
    } else if (s.mode == 2) {
        v = reverse_bits(v);
    }
    return v + s.local;
}

void compute_cases(VulkanRig& rig, Problems& problems) {
    const std::uint32_t count = 200;
    const struct {
        const char* name;
        Specialization spec;
        bool specialize;
        std::uint32_t groups;
    } cases[] = {{"local size 64, multiply 3, add 7", {64, 3, 7, 0}, true, (count + 63) / 64},
                 {"local size 32, xor shift", {32, 0x9E3779B1u, 5, 1}, true, (count + 31) / 32},
                 {"local size 16, bit reversal", {16, 1, 0, 2}, true, (count + 15) / 16},
                 {"defaults without specialization", {1, 1, 0, 0}, false, count}};
    const std::vector<std::uint32_t> input = vk::Kit::words(0x5EC, count);
    for (const auto& item : cases) {
        const std::string name = std::string("compute module with ") + item.name;
        const VkSpecializationMapEntry entries[4] = {{0, 0, 4}, {1, 4, 4}, {2, 8, 4}, {3, 12, 4}};
        VkSpecializationInfo info{};
        info.mapEntryCount = 4;
        info.pMapEntries = entries;
        info.dataSize = sizeof(Specialization);
        info.pData = &item.spec;
        VulkanCompute compute;
        VulkanBuffer in;
        VulkanBuffer out;
        if (!vk::Kit::succeeds(problems, name + " pipeline",
                               compute.create(rig, VulkanShaders::words(VulkanShader::SpecKernel), {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}}, 4,
                                              item.specialize ? &info : nullptr)) ||
            !vk::Kit::succeeds(problems, name + " input", vk::Kit::host_buffer(rig, in, 4096)) ||
            !vk::Kit::succeeds(problems, name + " output", vk::Kit::host_buffer(rig, out, 4096))) {
            continue;
        }
        std::memset(in.mapped, 0, 4096);
        std::memcpy(in.mapped, input.data(), input.size() * 4);
        in.publish();
        std::memset(out.mapped, 0xCD, 4096);
        out.publish();
        compute.write_buffer(0, in.buffer);
        compute.write_buffer(1, out.buffer);
        if (!vk::Kit::succeeds(problems, name + " dispatch", compute.run(&count, 4, item.groups))) {
            rig.settle();
            continue;
        }
        rig.settle();
        out.acquire();
        for (std::uint32_t i = 0; i < 1024; ++i) {
            const std::uint32_t want = i < count ? expected_word(item.spec, input[i]) : 0xCDCDCDCDu;
            if (out.words()[i] != want) {
                problems.add(name + ": word " + std::to_string(i) + " is 0x" + vk::Kit::hex(out.words()[i]) + ", expected 0x" + vk::Kit::hex(want));
                break;
            }
        }
    }
}

void graphics_cases(Context& context, VulkanRig& rig, Problems& problems) {
    static_cast<void>(context);
    const struct {
        float color[3];
    } cases[] = {{{0.25f, 0.5f, 0.75f}}, {{1.0f, 0.0f, 0.5f}}};
    for (const auto& item : cases) {
        const VkSpecializationMapEntry entries[3] = {{0, 0, 4}, {1, 4, 4}, {2, 8, 4}};
        VkSpecializationInfo info{};
        info.mapEntryCount = 3;
        info.pMapEntries = entries;
        info.dataSize = sizeof(item.color);
        info.pData = item.color;
        SceneOptions options;
        options.fragment_words = VulkanShaders::words(VulkanShader::SpecFrag);
        options.fragment_specialization = &info;
        VulkanScene scene;
        const std::string name = "graphics module with specialized colour " + std::to_string(item.color[0]) + "," + std::to_string(item.color[1]) + "," +
                                 std::to_string(item.color[2]);
        if (!vk::Kit::succeeds(problems, name + " pipeline", scene.create(rig, options))) {
            continue;
        }
        SceneRect rect;
        rect.x0 = 0;
        rect.y0 = 0;
        rect.x1 = 32;
        rect.y1 = 32;
        const float clear[4] = {0, 0, 0, 1};
        SceneResult result;
        if (!vk::Kit::succeeds(problems, name + " draw", scene.render({rect}, clear, 1.0f, result))) {
            continue;
        }
        const std::uint8_t want[4] = {VulkanScene::unorm8(item.color[0]), VulkanScene::unorm8(item.color[1]), VulkanScene::unorm8(item.color[2]), 255};
        for (std::size_t pixel = 0; pixel < 32 * 32; ++pixel) {
            bool bad = false;
            for (int c = 0; c < 4; ++c) {
                if (std::abs(static_cast<int>(result.color[pixel * 4 + static_cast<std::size_t>(c)]) - static_cast<int>(want[c])) > 1) {
                    bad = true;
                }
            }
            if (bad) {
                problems.add(name + ": pixel " + std::to_string(pixel) + " is (" + std::to_string(result.color[pixel * 4]) + "," + std::to_string(result.color[pixel * 4 + 1]) + "," +
                             std::to_string(result.color[pixel * 4 + 2]) + "," + std::to_string(result.color[pixel * 4 + 3]) + "), expected (" + std::to_string(want[0]) + "," +
                             std::to_string(want[1]) + "," + std::to_string(want[2]) + ",255)");
                break;
            }
        }
    }
}

std::vector<std::uint32_t> mutate(const std::vector<std::uint32_t>& words, int kind, std::string& name) {
    std::vector<std::uint32_t> out = words;
    std::vector<std::size_t> starts;
    for (std::size_t at = 5; at < words.size();) {
        starts.push_back(at);
        const std::uint32_t length = words[at] >> 16;
        if (length == 0) {
            break;
        }
        at += length;
    }
    switch (kind) {
    case 0:
        name = "a wrong magic number";
        out[0] ^= 0xFFFFu;
        break;
    case 1: {
        name = "a module cut inside an instruction";
        std::size_t index = starts.size() / 2;
        while (index + 1 < starts.size() && (words[starts[index]] >> 16) < 2) {
            ++index;
        }
        out.resize(starts[index] + 1);
        break;
    }
    case 2:
        name = "an instruction with a word count of zero";
        out[starts[starts.size() / 2]] = 0;
        break;
    case 3:
        name = "an id bound of one";
        out[3] = 1;
        break;
    case 4:
        name = "a header without a body";
        out.resize(5);
        break;
    default: {
        name = "random words behind a valid header";
        std::vector<std::uint8_t> noise(words.size() * 4);
        Pattern::fill(0xBAD, 0, noise.data(), noise.size());
        std::memcpy(out.data() + 5, noise.data(), (out.size() - 5) * 4);
        break;
    }
    }
    return out;
}

}  // namespace

Outcome SpirvChecks::valid_runs(Context& context) {
    return vk::Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        compute_cases(rig, problems);
        graphics_cases(context, rig, problems);
        return {};
    });
}

Outcome SpirvChecks::invalid_fails(Context& context) {
    VulkanRigOptions options;
    options.debug_messages = true;
    return vk::Kit::each_device(context, options, [&](VulkanRig& rig, Problems& problems) -> std::string {
        const std::vector<std::uint32_t> valid = VulkanShaders::words(VulkanShader::Ops);
        for (int kind = 0; kind < 6; ++kind) {
            std::string name;
            const std::vector<std::uint32_t> bad = mutate(valid, kind, name);
            std::string first;
            int errors = 0;
            int warnings = 0;
            rig.drain_debug(first, errors, warnings);
            VulkanCompute compute;
            const VkResult result =
                compute.create(rig, bad, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}}, 8);
            rig.settle();
            rig.drain_debug(first, errors, warnings);
            std::string key = "spirv.invalid." + name;
            std::replace(key.begin(), key.end(), ' ', '_');
            context.measure(key + ".creation_failed", result != VK_SUCCESS);
            context.measure(key + ".messages", errors + warnings);
            rig.settle();
        }
        const std::vector<std::uint32_t> input = vk::Kit::words(0x77, 64);
        VulkanCompute compute;
        VulkanBuffer in;
        VulkanBuffer out;
        VulkanBuffer params;
        if (!vk::Kit::succeeds(problems, "the valid pipeline after the malformed modules",
                               compute.create(rig, valid, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}}, 8)) ||
            !vk::Kit::succeeds(problems, "input", vk::Kit::host_buffer(rig, in, 256)) || !vk::Kit::succeeds(problems, "output", vk::Kit::host_buffer(rig, out, 256)) ||
            !vk::Kit::succeeds(problems, "parameters", vk::Kit::host_buffer(rig, params, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT))) {
            return {};
        }
        std::memcpy(in.mapped, input.data(), 256);
        in.publish();
        params.words()[0] = 3;
        params.words()[1] = 5;
        params.words()[2] = 64;
        params.words()[3] = 0;
        params.publish();
        compute.write_buffer(0, in.buffer);
        compute.write_buffer(1, out.buffer);
        compute.write_buffer(2, params.buffer);
        const std::uint32_t push[2] = {0, 0};
        if (vk::Kit::succeeds(problems, "dispatch after the malformed modules", compute.run(push, 8, 1))) {
            rig.settle();
            out.acquire();
            for (std::uint32_t i = 0; i < 64; ++i) {
                if (out.words()[i] != input[i] * 3 + 5) {
                    problems.add("the device computes wrongly after the malformed modules: word " + std::to_string(i));
                    break;
                }
            }
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::shader
