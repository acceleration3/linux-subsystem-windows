#include "sanity/bits/parity_checks.hpp"

#include "core/child_mode.hpp"
#include "core/text.hpp"
#include "sanity/sync/sync_device.hpp"
#include "support/api_loop.hpp"
#include "support/api_policy.hpp"
#include "support/child_runs.hpp"
#include "support/cl_rig.hpp"
#include "support/cl_tools.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_tools.hpp"
#include "support/problems.hpp"
#include "support/target_adapter.hpp"
#include "support/vulkan_rig.hpp"

#include <cstring>
#include <map>
#include <random>

namespace lsw::suites::sanity::bits {

namespace {

using Facts = std::map<std::string, std::string>;

std::string hash_text(const std::vector<std::uint32_t>& words) {
    std::uint64_t hash = 1469598103934665603ull;
    for (std::uint32_t word : words) {
        hash = (hash ^ word) * 1099511628211ull;
    }
    return Text::hex(static_cast<std::uint32_t>(hash >> 32), 8) + Text::hex(static_cast<std::uint32_t>(hash), 8);
}

void operations(Facts& facts) {
    for (const std::string& api : ApiPolicy::filter({"d3d11", "d3d12", "vulkan", "opencl"})) {
        std::unique_ptr<sync::SyncDevice> device = sync::SyncDevice::make(api);
        std::string error;
        if (device == nullptr || !device->open(error)) {
            facts["ops." + api] = "unavailable";
            continue;
        }
        std::mt19937 random(2024);
        for (int i = 0; i < 3000; ++i) {
            const std::uint32_t slot = random() % 8;
            const std::uint32_t value = random();
            if (!(random() % 3 == 0 ? device->set(slot, value, error) : device->add(slot, value, error))) {
                break;
            }
        }
        std::string detail;
        std::vector<std::uint32_t> words;
        if (!device->signal(error) || device->wait(30000, detail) != sync::SyncState::Done) {
            facts["ops." + api] = "incomplete";
            continue;
        }
        for (std::uint32_t slot = 0; slot < 8; ++slot) {
            std::uint32_t value = 0;
            static_cast<void>(device->read(slot, value, error));
            words.push_back(value);
        }
        facts["ops." + api] = hash_text(words);
    }
    if (!ApiPolicy::is_skipped("d3d11")) {
        AvAdapter adapter;
        D3d11Device device;
        std::string error;
        if (TargetAdapter::pick(adapter, error) && device.create(adapter.luid, error)) {
            Com<ID3D11Texture2D> target = D3d11Tools::target(device, 64, 64);
            Com<ID3D11RenderTargetView> view = target ? D3d11Tools::rtv(device, target.get()) : Com<ID3D11RenderTargetView>();
            Com<ID3D11PixelShader> shader = D3d11Tools::ps(device, "float4 main(float4 p : SV_Position) : SV_Target { return float4(frac(p.x * 0.0371), frac(p.y * 0.0713), 0.25, 1.0); }", error);
            TexelImage image;
            if (view && shader && D3d11Tools::draw_fullscreen(device, shader.get(), view.get(), 64, 64, error) && D3d11Tools::read_texture(device, target.get(), 0, image, error)) {
                std::vector<std::uint32_t> words(image.data.size() / 4);
                std::memcpy(words.data(), image.data.data(), words.size() * 4);
                facts["render.d3d11"] = hash_text(words);
            } else {
                facts["render.d3d11"] = "unavailable";
            }
        }
    }
}

void widths(Facts& facts) {
    facts["pointer"] = std::to_string(sizeof(void*));
    facts["size_t"] = std::to_string(sizeof(std::size_t));
    facts["handle"] = std::to_string(sizeof(HANDLE));
    facts["gpu_va"] = std::to_string(sizeof(D3D12_GPU_VIRTUAL_ADDRESS));
    facts["cpu_descriptor"] = std::to_string(sizeof(D3D12_CPU_DESCRIPTOR_HANDLE));
    facts["vk_device_size"] = std::to_string(sizeof(VkDeviceSize));
    facts["vk_nondispatchable"] = std::to_string(sizeof(VkBuffer));
    facts["vk_dispatchable"] = std::to_string(sizeof(VkDevice));
    facts["cl_size_t"] = std::to_string(sizeof(size_t));
    facts["cl_mem"] = std::to_string(sizeof(cl_mem));
    facts["luid"] = std::to_string(sizeof(LUID));
    facts["d3d11_box"] = std::to_string(sizeof(D3D11_BOX));
}

void error_facts(Facts& facts) {
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return;
    }
    if (!ApiPolicy::is_skipped("d3d11")) {
        D3d11Device device;
        if (device.create(adapter.luid, error)) {
            HRESULT result = S_OK;
            Com<ID3D11Buffer> zero = D3d11Tools::buffer(device, 0, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, nullptr, &result);
            facts["d3d11.zero_buffer"] = D3d11Tools::hr(result);
            Com<ID3D11Texture2D> texture = D3d11Tools::texture(device, D3d11Tools::desc2d(0, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE), nullptr, &result);
            facts["d3d11.zero_texture"] = D3d11Tools::hr(result);
            Com<ID3D11Texture2D> huge = D3d11Tools::texture(device, D3d11Tools::desc2d(1u << 20, 1u << 20, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE), nullptr, &result);
            facts["d3d11.huge_texture"] = D3d11Tools::hr(result);
        }
    }
    if (!ApiPolicy::is_skipped("d3d12")) {
        D3d12Rig rig;
        if (rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
            D3D12_RESOURCE_DESC desc = D3d12Tools::buffer_desc(0);
            const D3D12_HEAP_PROPERTIES heap = D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT);
            Com<ID3D12Resource> resource;
            facts["d3d12.zero_buffer"] = D3d12Tools::hr(rig.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), resource.put_void()));
            D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
            heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            Com<ID3D12DescriptorHeap> descriptors;
            facts["d3d12.empty_heap"] = D3d12Tools::hr(rig.device()->CreateDescriptorHeap(&heap_desc, __uuidof(ID3D12DescriptorHeap), descriptors.put_void()));
        }
    }
    if (!ApiPolicy::is_skipped("opencl")) {
        ClRig rig;
        if (rig.open(ClRigOptions{}, error)) {
            cl_int code = CL_SUCCESS;
            cl_mem buffer = rig.api.CreateBuffer(rig.context, CL_MEM_READ_WRITE, 0, nullptr, &code);
            facts["cl.zero_buffer"] = ClTools::error_name(code);
            if (buffer != nullptr) {
                rig.api.ReleaseMemObject(buffer);
            }
            buffer = rig.api.CreateBuffer(rig.context, CL_MEM_READ_ONLY | CL_MEM_WRITE_ONLY, 64, nullptr, &code);
            facts["cl.bad_flags"] = ClTools::error_name(code);
            if (buffer != nullptr) {
                rig.api.ReleaseMemObject(buffer);
            }
            cl_ulong largest = 0;
            rig.info(CL_DEVICE_MAX_MEM_ALLOC_SIZE, largest);
            if (largest >= static_cast<cl_ulong>(~static_cast<std::size_t>(0))) {
                facts["cl.huge_buffer"] = "n/a";
            } else {
                buffer = rig.api.CreateBuffer(rig.context, CL_MEM_READ_WRITE, static_cast<std::size_t>(largest) + 1, nullptr, &code);
                facts["cl.huge_buffer"] = ClTools::error_name(code);
                if (buffer != nullptr) {
                    rig.api.ReleaseMemObject(buffer);
                }
            }
        }
    }
    if (!ApiPolicy::is_skipped("vulkan")) {
        const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(error);
        VulkanRig rig;
        if (!adapters.empty() && rig.open(adapters.front(), VulkanRigOptions{}, error)) {
            VulkanOwned<VkFence> fence;
            static_cast<void>(rig.make_fence(fence));
            VkFence raw = fence.get();
            facts["vk.fence_status"] = VulkanTools::result_name(rig.fn.vkGetFenceStatus(rig.device, raw));
            facts["vk.fence_wait_zero"] = VulkanTools::result_name(rig.fn.vkWaitForFences(rig.device, 1, &raw, VK_TRUE, 0));
        }
    }
}

Outcome facts_role(Context& context, const std::string& kind) {
    Facts facts;
    if (kind == "ops") {
        operations(facts);
    } else if (kind == "widths") {
        widths(facts);
    } else if (kind == "errors") {
        error_facts(facts);
    } else {
        return Outcome::fail("unknown row kind " + kind);
    }
    for (const auto& [key, value] : facts) {
        context.measure(key, value);
    }
    return Outcome::ok();
}

Outcome compare_with_sibling(Context& context, const std::string& kind) {
    if (ChildMode::active()) {
        return facts_role(context, kind);
    }
    Facts own;
    if (kind == "ops") {
        operations(own);
    } else if (kind == "errors") {
        error_facts(own);
    }
    ChildRunOptions options;
    options.timeout_seconds = 240;
    options.env = {{"LSW_SUITES_BITS_KIND", kind}};
    CheckResult other;
    std::string error;
    if (!ChildRuns::run_sibling(context.id(), options, other, error)) {
        return Context::skip(error);
    }
    Problems problems;
    for (const auto& [key, value] : own) {
        context.measure(key, value);
        if (!ChildRuns::has(other, key)) {
            problems.add(key + ": the sibling could not open " + key.substr(0, key.find('.')));
            continue;
        }
        const std::string theirs = ChildRuns::text(other, key);
        if (key == "cl.huge_buffer" && value != "n/a" && value != "CL_INVALID_BUFFER_SIZE") {
            problems.add("clCreateBuffer of CL_DEVICE_MAX_MEM_ALLOC_SIZE + 1 bytes returned " + value + ", expected CL_INVALID_BUFFER_SIZE");
        }
        if (value == "n/a" || theirs == "n/a") {
            continue;
        }
        if (theirs != value) {
            problems.add(key + ": " + std::to_string(sizeof(void*) * 8) + "-bit gives '" + value + "', the other bitness gives '" + theirs + "'");
        }
    }
    return problems.outcome();
}

}  // namespace

Outcome ParityChecks::operations_same_result(Context& context) {
    if (ChildMode::active()) {
        return facts_role(context, ChildMode::variable(L"LSW_SUITES_BITS_KIND"));
    }
    return compare_with_sibling(context, "ops");
}

Outcome ParityChecks::error_codes(Context& context) {
    if (ChildMode::active()) {
        return facts_role(context, ChildMode::variable(L"LSW_SUITES_BITS_KIND"));
    }
    return compare_with_sibling(context, "errors");
}

Outcome ParityChecks::integer_widths(Context& context) {
    if (ChildMode::active()) {
        return facts_role(context, "widths");
    }
    const std::size_t pointer = sizeof(void*);
    Problems problems;
    Facts facts;
    widths(facts);
    auto expect = [&](const std::string& key, std::size_t want) {
        if (facts[key] != std::to_string(want)) {
            problems.add(key + " is " + facts[key] + " bytes, expected " + std::to_string(want));
        }
    };
    expect("pointer", pointer);
    expect("size_t", pointer);
    expect("handle", pointer);
    expect("gpu_va", 8);
    expect("cpu_descriptor", pointer);
    expect("vk_device_size", 8);
    expect("vk_nondispatchable", 8);
    expect("vk_dispatchable", pointer);
    expect("cl_size_t", pointer);
    expect("cl_mem", pointer);
    expect("luid", 8);
    expect("d3d11_box", 24);
    if (!ApiPolicy::is_skipped("opencl")) {
        ClRig rig;
        std::string error;
        if (rig.open(ClRigOptions{}, error)) {
            cl_uint bits = 0;
            rig.info(CL_DEVICE_ADDRESS_BITS, bits);
            context.measure("cl_address_bits", bits);
        }
    }
    ChildRunOptions options;
    options.timeout_seconds = 120;
    options.env = {{"LSW_SUITES_BITS_KIND", "widths"}};
    CheckResult other;
    std::string error;
    if (ChildRuns::run_sibling(context.id(), options, other, error)) {
        for (const char* key : {"gpu_va", "vk_device_size", "vk_nondispatchable", "luid", "d3d11_box"}) {
            if (ChildRuns::text(other, key) != facts[key]) {
                problems.add(std::string(key) + " differs between the bitnesses");
            }
        }
    }
    return problems.outcome();
}

Outcome ParityChecks::data_roundtrip(Context& context) {
    Problems problems;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        for (std::uint64_t size : BackendHarness::boundary_sizes()) {
            if (size > (256ull << 20) + 4096) {
                continue;
            }
            AllocStatus status = AllocStatus::Ok;
            std::string error;
            if (!BackendHarness::roundtrip(backend, size, size + 5, status, context, error)) {
                problems.add(api + " size " + std::to_string(size) + ": " + error);
            } else if (status != AllocStatus::Ok) {
                problems.add(api + " size " + std::to_string(size) + " could not be allocated: " + GpuBackends::describe(status));
            }
        }
    });
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::bits
