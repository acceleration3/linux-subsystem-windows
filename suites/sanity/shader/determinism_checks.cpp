#include "sanity/shader/determinism_checks.hpp"

#include "core/child_mode.hpp"
#include "core/text.hpp"
#include "sanity/cl/kit.hpp"
#include "sanity/gl/kit.hpp"
#include "sanity/shader/kit.hpp"
#include "sanity/shader/workload.hpp"
#include "sanity/vk/kit.hpp"
#include "support/child_runs.hpp"
#include "support/d3d11_tools.hpp"
#include "support/gl_program.hpp"

#include <map>
#include <thread>

namespace lsw::suites::sanity::shader {

namespace {

using Words = Workload::Words;

std::string hex_hash(const Words& words) { return Text::hex(Workload::hash(words), 16); }

std::string first_difference(const Words& a, const Words& b) {
    if (a.size() != b.size()) {
        return "sizes " + std::to_string(a.size()) + " and " + std::to_string(b.size());
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            return "word " + std::to_string(i) + " is 0x" + Text::hex(b[i], 8) + ", the first run gave 0x" + Text::hex(a[i], 8);
        }
    }
    return {};
}

void invalid_hlsl(Problems& problems, const std::string& who) {
    std::vector<std::uint8_t> code;
    std::string error;
    if (D3d11Tools::compile("RWStructuredBuffer<uint> o : register(u0);\n[numthreads(64,1,1)] void main(uint3 id : SV_DispatchThreadID) { o[id.x] = undeclared_name + ; }\n",
                            "main", "cs_5_0", code, error)) {
        problems.add(who + ": invalid HLSL compiled");
    }
}

void d3d11_twice(Context& context, Problems& problems) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!TargetAdapter::pick(adapter, error) || !device.create_ex(adapter.luid, nullptr, 0, 0, error)) {
        problems.add("D3D11: " + error);
        return;
    }
    Words first;
    Words second;
    Workload::Bytes code_a;
    Workload::Bytes code_b;
    if (!Workload::d3d11(device, first, &code_a, error) || !Workload::d3d11(device, second, &code_b, error)) {
        problems.add("D3D11: " + error);
        return;
    }
    if (code_a != code_b) {
        problems.add("D3D11: compiling the same HLSL twice gave different bytecode");
    }
    if (first != second) {
        problems.add("D3D11: the same shader created twice gave different output: " + first_difference(first, second));
    }
    context.measure("shader.twice.d3d11.hash", hex_hash(first));
}

void d3d12_twice(Context& context, Problems& problems) {
    D3d12Rig rig;
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error) || !rig.open(adapter, D3D_FEATURE_LEVEL_11_0, error)) {
        problems.add("D3D12: " + error);
        return;
    }
    Words first;
    Words second;
    if (!Workload::d3d12(rig, first, error) || !Workload::d3d12(rig, second, error)) {
        problems.add("D3D12: " + error);
        return;
    }
    if (first != second) {
        problems.add("D3D12: the same pipeline created twice gave different output: " + first_difference(first, second));
    }
    context.measure("shader.twice.d3d12.hash", hex_hash(first));
}

Outcome vulkan_twice(Context& context) {
    return vk::Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        Words first;
        Words second;
        VkResult result = Workload::vulkan(rig, first);
        if (result == VK_SUCCESS) {
            result = Workload::vulkan(rig, second);
        }
        if (result != VK_SUCCESS) {
            problems.add("the compute pipeline failed: " + vk::Kit::name(result));
        } else if (first != second) {
            problems.add("the same pipeline created twice gave different output: " + first_difference(first, second));
        }
        return {};
    });
}

Outcome gl_twice(Context& context) {
    GlRig rig;
    std::string error;
    if (!gl::Kit::open(rig, context, error, 64, 64)) {
        return Outcome::fail(error);
    }
    if (!Workload::gl_supported(rig.gl)) {
        return Context::skip("compute shaders are not available in this context");
    }
    Words first;
    Words second;
    if (!Workload::gl(rig.gl, first, error) || !Workload::gl(rig.gl, second, error)) {
        return Outcome::fail(error);
    }
    if (first != second) {
        return Outcome::fail("the same program linked twice gave different output: " + first_difference(first, second));
    }
    return Outcome::ok();
}

Outcome opencl_twice(Context& context) {
    return cl::Kit::each_device(context, [&](ClRig& rig, Problems& problems) {
        Words first;
        Words second;
        std::string error;
        if (!Workload::opencl_run(rig, first, error) || !Workload::opencl_run(rig, second, error)) {
            problems.add(error);
        } else if (first != second) {
            problems.add("the same program built twice gave different output: " + first_difference(first, second));
        }
    });
}

using Hashes = std::map<std::string, std::string>;

void run_all(Context& context, bool poison, Hashes& hashes, Problems& problems, std::vector<std::string>& skips) {
    std::string error;
    AvAdapter adapter;
    if (Kit::enabled("d3d11")) {
        D3d11Device device;
        Words words;
        if (!TargetAdapter::pick(adapter, error) || !device.create_ex(adapter.luid, nullptr, 0, 0, error)) {
            problems.add("D3D11: " + error);
        } else {
            if (poison) {
                invalid_hlsl(problems, "D3D11");
            }
            if (!Workload::d3d11(device, words, nullptr, error)) {
                problems.add("D3D11: " + error);
            } else {
                hashes["d3d11"] = hex_hash(words);
            }
            if (poison) {
                invalid_hlsl(problems, "D3D11");
            }
        }
    }
    if (Kit::enabled("d3d12")) {
        D3d12Rig rig;
        Words words;
        if (!TargetAdapter::pick(adapter, error) || !rig.open(adapter, D3D_FEATURE_LEVEL_11_0, error)) {
            problems.add("D3D12: " + error);
        } else if (!Workload::d3d12(rig, words, error)) {
            problems.add("D3D12: " + error);
        } else {
            hashes["d3d12"] = hex_hash(words);
        }
    }
    if (Kit::enabled("vulkan")) {
        std::string value;
        const Outcome outcome = vk::Kit::each_device(context, [&](VulkanRig& rig, Problems& found) -> std::string {
            Words words;
            const VkResult result = Workload::vulkan(rig, words);
            if (result != VK_SUCCESS) {
                found.add("the compute pipeline failed: " + vk::Kit::name(result));
            } else if (value.empty()) {
                value = hex_hash(words);
            } else if (value != hex_hash(words)) {
                found.add("two devices of the same adapter list gave different output");
            }
            return {};
        });
        if (outcome.status() == Status::Fail) {
            problems.add("Vulkan: " + outcome.reason());
        } else if (!value.empty()) {
            hashes["vulkan"] = value;
        }
    }
    if (Kit::enabled("gl")) {
        GlRig rig;
        if (!gl::Kit::open(rig, context, error, 64, 64)) {
            problems.add("OpenGL: " + error);
        } else if (Workload::gl_supported(rig.gl)) {
            Words words;
            if (poison) {
                GlProgram bad;
                if (bad.build_compute(rig.gl, "#version 430\nlayout(local_size_x = 64) in;\nvoid main() { undeclared = ; }\n", error)) {
                    problems.add("OpenGL: an invalid compute shader compiled");
                }
                rig.gl.drain_errors();
            }
            if (!Workload::gl(rig.gl, words, error)) {
                problems.add("OpenGL: " + error);
            } else {
                hashes["gl"] = hex_hash(words);
            }
        } else {
            skips.push_back("OpenGL compute shaders are not available");
        }
    }
    if (Kit::enabled("opencl")) {
        std::string value;
        const Outcome outcome = cl::Kit::each_device(context, [&](ClRig& rig, Problems& found) {
            Words words;
            std::string device_error;
            if (poison) {
                ClProgram bad;
                if (bad.build(rig, "__kernel void broken(__global uint* b) { b[0] = undeclared +; }", "", device_error)) {
                    found.add("an invalid kernel built");
                }
            }
            if (!Workload::opencl_run(rig, words, device_error)) {
                found.add(device_error);
            } else if (value.empty()) {
                value = hex_hash(words);
            }
        });
        if (outcome.status() == Status::Fail) {
            problems.add("OpenCL: " + outcome.reason());
        } else if (!value.empty()) {
            hashes["opencl"] = value;
        }
    }
}

}  // namespace

Outcome DeterminismChecks::created_twice(Context& context) {
    Problems problems;
    std::vector<std::string> skips;
    bool exercised = false;
    if (Kit::enabled("d3d11")) {
        d3d11_twice(context, problems);
        exercised = true;
    } else {
        skips.push_back("D3D11 excluded");
    }
    if (Kit::enabled("d3d12")) {
        d3d12_twice(context, problems);
        exercised = true;
    } else {
        skips.push_back("D3D12 excluded");
    }
    if (Kit::enabled("vulkan")) {
        Kit::merge("Vulkan", vulkan_twice(context), problems, skips, exercised);
    } else {
        skips.push_back("Vulkan excluded");
    }
    if (Kit::enabled("gl")) {
        Kit::merge("OpenGL", gl_twice(context), problems, skips, exercised);
    } else {
        skips.push_back("OpenGL excluded");
    }
    if (Kit::enabled("opencl")) {
        Kit::merge("OpenCL", opencl_twice(context), problems, skips, exercised);
    } else {
        skips.push_back("OpenCL excluded");
    }
    return Kit::finish(skips, problems, exercised);
}

Outcome DeterminismChecks::two_processes(Context& context) {
    Problems problems;
    std::vector<std::string> skips;
    Hashes own;
    if (ChildMode::active()) {
        const bool poison = ChildMode::variable(L"LSW_SUITES_ROLE") == "poison";
        run_all(context, poison, own, problems, skips);
        for (const auto& item : own) {
            context.measure("hash." + item.first, item.second);
        }
        return problems.outcome();
    }
    CheckResult plain;
    CheckResult poisoned;
    ChildRunOptions plain_options;
    plain_options.timeout_seconds = 150;
    plain_options.env = {{"LSW_SUITES_ROLE", "plain"}};
    ChildRunOptions poison_options = plain_options;
    poison_options.env = {{"LSW_SUITES_ROLE", "poison"}};
    std::thread first([&] { plain = ChildRuns::run_self("sanity.shader.determinism.two_processes", plain_options); });
    std::thread second([&] { poisoned = ChildRuns::run_self("sanity.shader.determinism.two_processes", poison_options); });
    run_all(context, false, own, problems, skips);
    first.join();
    second.join();
    bool exercised = !own.empty();
    for (const auto* result : {&plain, &poisoned}) {
        const std::string who = result == &plain ? "the plain process" : "the process compiling invalid shaders";
        if (result->status != Status::Ok) {
            problems.add(who + ": " + Outcome::name(result->status) + " " + result->reason);
            continue;
        }
        for (const auto& item : own) {
            const std::string theirs = ChildRuns::text(*result, "hash." + item.first);
            if (theirs.empty()) {
                problems.add(who + " reported no output for " + item.first);
            } else if (theirs != item.second) {
                problems.add(who + " got different output for " + item.first + " (" + theirs + " against " + item.second + ")");
            }
        }
    }
    return Kit::finish(skips, problems, exercised);
}

}  // namespace lsw::suites::sanity::shader
