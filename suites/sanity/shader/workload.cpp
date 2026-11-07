#include "sanity/shader/workload.hpp"

#include "sanity/cl/kit.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_root.hpp"
#include "support/d3d12_tools.hpp"
#include "support/gl_program.hpp"
#include "support/vulkan_buffer.hpp"
#include "support/vulkan_compute.hpp"
#include "support/vulkan_shaders.hpp"

#include <cstring>

namespace lsw::suites::sanity::shader {

const char* Workload::hlsl() {
    return "RWByteAddressBuffer o : register(u0);\n"
           "[numthreads(64, 1, 1)]\n"
           "void main(uint3 id : SV_DispatchThreadID) {\n"
           "  float x = float(id.x) * 0.0078125 + 0.1;\n"
           "  [unroll] for (int k = 0; k < 8; ++k) { x = x * x * -1.9 + 1.0 + 0.125 * sin(x); }\n"
           "  o.Store(id.x * 4, asuint(x) ^ (id.x * 2654435761u));\n"
           "}\n";
}

const char* Workload::glsl() {
    return "#version 430\n"
           "layout(local_size_x = 64) in;\n"
           "layout(std430, binding = 0) buffer B { uint b[]; };\n"
           "void main() {\n"
           "  uint i = gl_GlobalInvocationID.x;\n"
           "  float x = float(i) * 0.0078125 + 0.1;\n"
           "  for (int k = 0; k < 8; ++k) { x = x * x * (-1.9) + 1.0 + 0.125 * sin(x); }\n"
           "  b[i] = floatBitsToUint(x) ^ (i * 2654435761u);\n"
           "}\n";
}

const char* Workload::opencl() {
    return "__kernel void lsw_det(__global uint* b) {\n"
           "  uint i = (uint)get_global_id(0);\n"
           "  float x = (float)i * 0.0078125f + 0.1f;\n"
           "  for (int k = 0; k < 8; ++k) { x = x * x * (-1.9f) + 1.0f + 0.125f * sin(x); }\n"
           "  b[i] = as_uint(x) ^ (i * 2654435761u);\n"
           "}\n";
}

bool Workload::d3d11(D3d11Device& device, Words& out, Bytes* code, std::string& error) {
    Bytes bytecode;
    if (!D3d11Tools::compile(hlsl(), "main", D3d11Tools::profile(device, "cs"), bytecode, error)) {
        return false;
    }
    if (code != nullptr) {
        *code = bytecode;
    }
    Com<ID3D11ComputeShader> shader;
    HRESULT result = device.device->CreateComputeShader(bytecode.data(), bytecode.size(), nullptr, shader.put());
    if (FAILED(result)) {
        error = "CreateComputeShader failed: " + D3d11Tools::hr(result);
        return false;
    }
    Com<ID3D11Buffer> buffer = D3d11Tools::buffer(device, kCount * 4, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0,
                                                  D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, nullptr, &result);
    Com<ID3D11UnorderedAccessView> uav = buffer ? D3d11Tools::raw_uav(device, buffer.get()) : Com<ID3D11UnorderedAccessView>();
    if (!buffer || !uav) {
        error = "creating the output buffer failed: " + D3d11Tools::hr(result);
        return false;
    }
    ID3D11UnorderedAccessView* uavs[1] = {uav.get()};
    device.context->CSSetShader(shader.get(), nullptr, 0);
    device.context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    device.context->Dispatch(kCount / 64, 1, 1);
    D3d11Tools::reset_state(device);
    out = D3d11Tools::read_words(device, buffer.get(), error);
    return out.size() == kCount;
}

bool Workload::d3d12(D3d12Rig& rig, Words& out, std::string& error) {
    D3d12Root root;
    root.uav(0);
    ID3D12RootSignature* signature = root.build(rig, error);
    ID3D12PipelineState* state = signature != nullptr ? D3d12Tools::compute_from(rig, signature, hlsl(), error) : nullptr;
    HRESULT result = S_OK;
    ID3D12Resource* buffer =
        D3d12Tools::device_buffer(rig, kCount * 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &result);
    if (state == nullptr || buffer == nullptr) {
        if (error.empty()) {
            error = "creating the output buffer failed: " + D3d12Tools::hr(result);
        }
        return false;
    }
    const bool ran = rig.direct().run_wait(
        [&](ID3D12GraphicsCommandList* list) {
            list->SetComputeRootSignature(signature);
            list->SetPipelineState(state);
            list->SetComputeRootUnorderedAccessView(0, buffer->GetGPUVirtualAddress());
            list->Dispatch(kCount / 64, 1, 1);
        },
        error);
    return ran && D3d12Tools::read_words(rig, buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kCount, out, error);
}

bool Workload::gl_supported(const GlApi& api) {
    return api.supports(4, 3, "GL_ARB_compute_shader") && api.glDispatchCompute != nullptr && api.glBindBufferBase != nullptr;
}

bool Workload::gl(const GlApi& api, Words& out, std::string& error) {
    GlProgram program;
    if (!program.build_compute(api, glsl(), error)) {
        return false;
    }
    api.drain_errors();
    unsigned buffer = 0;
    api.glGenBuffers(1, &buffer);
    api.glBindBuffer(glc::SHADER_STORAGE_BUFFER, buffer);
    api.glBufferData(glc::SHADER_STORAGE_BUFFER, kCount * 4, nullptr, glc::DYNAMIC_DRAW);
    api.glBindBufferBase(glc::SHADER_STORAGE_BUFFER, 0, buffer);
    program.use();
    api.glDispatchCompute(kCount / 64, 1, 1);
    api.glMemoryBarrier(glc::SHADER_STORAGE_BARRIER_BIT | glc::BUFFER_UPDATE_BARRIER_BIT);
    out.assign(kCount, 0);
    api.glGetBufferSubData(glc::SHADER_STORAGE_BUFFER, 0, kCount * 4, out.data());
    const unsigned failure = api.glGetError();
    api.glFinish();
    api.glUseProgram(0);
    api.glBindBuffer(glc::SHADER_STORAGE_BUFFER, 0);
    api.glDeleteBuffers(1, &buffer);
    if (failure != 0) {
        error = "the compute dispatch raised " + GlApi::error_name(failure);
        return false;
    }
    return true;
}

VkResult Workload::vulkan(VulkanRig& rig, Words& out, VkPipelineCache cache) {
    VulkanCompute compute;
    VkResult result = compute.create(rig, VulkanShaders::words(VulkanShader::DetKernel), {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}}, 0, nullptr, false, cache);
    if (result != VK_SUCCESS) {
        return result;
    }
    VulkanBuffer buffer;
    result = buffer.create(rig, kCount * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (result != VK_SUCCESS) {
        return result;
    }
    std::memset(buffer.mapped, 0, kCount * 4);
    buffer.publish();
    compute.write_buffer(0, buffer.buffer);
    result = compute.run(nullptr, 0, kCount / 64);
    if (result != VK_SUCCESS) {
        rig.settle();
        return result;
    }
    rig.settle();
    buffer.acquire();
    out.assign(buffer.words(), buffer.words() + kCount);
    return VK_SUCCESS;
}

bool Workload::opencl_run(ClRig& rig, Words& out, std::string& error) {
    ClProgram program;
    if (!program.build(rig, opencl(), "", error)) {
        return false;
    }
    ClKernel kernel = program.kernel("lsw_det", error);
    cl_int code = CL_SUCCESS;
    ClOwned<cl_mem> buffer = cl::Kit::buffer(rig, CL_MEM_READ_WRITE, kCount * 4, nullptr, code);
    if (!kernel || !buffer) {
        if (error.empty()) {
            error = "creating the kernel or buffer failed: " + ClTools::error_name(code);
        }
        return false;
    }
    code = kernel.set(buffer);
    if (code == CL_SUCCESS) {
        code = kernel.run(kCount);
    }
    out.assign(kCount, 0);
    if (code == CL_SUCCESS) {
        code = rig.api.EnqueueReadBuffer(rig.queue, buffer.get(), CL_TRUE, 0, kCount * 4, out.data(), 0, nullptr, nullptr);
    }
    rig.api.Finish(rig.queue);
    if (code != CL_SUCCESS) {
        error = "running the kernel failed: " + ClTools::error_name(code);
        return false;
    }
    return true;
}

std::uint64_t Workload::hash(const Words& words) {
    std::uint64_t value = 1469598103934665603ull;
    for (std::uint32_t word : words) {
        value ^= word;
        value *= 1099511628211ull;
    }
    return value;
}

}  // namespace lsw::suites::sanity::shader
