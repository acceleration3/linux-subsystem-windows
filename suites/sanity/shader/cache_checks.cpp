#include "sanity/shader/cache_checks.hpp"

#include "core/text.hpp"
#include "sanity/gl/kit.hpp"
#include "sanity/shader/kit.hpp"
#include "sanity/shader/workload.hpp"
#include "sanity/vk/kit.hpp"
#include "support/d3d12_root.hpp"
#include "support/d3d12_tools.hpp"

#include <cstring>

namespace lsw::suites::sanity::shader {

namespace {

using Words = Workload::Words;
using Bytes = std::vector<std::uint8_t>;

constexpr unsigned kProgramBinaryRetrievableHint = 0x8257;
constexpr unsigned kProgramBinaryLength = 0x8741;
constexpr unsigned kNumProgramBinaryFormats = 0x87FE;

Outcome vulkan_part(Context& context) {
    return vk::Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        Words baseline;
        VkResult result = Workload::vulkan(rig, baseline);
        if (result != VK_SUCCESS) {
            problems.add("the reference pipeline failed: " + vk::Kit::name(result));
            return {};
        }
        VkPipelineCacheCreateInfo empty{};
        empty.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        VulkanOwned<VkPipelineCache> cache;
        if (!vk::Kit::succeeds(problems, "pipeline cache", rig.make(cache, rig.fn.vkCreatePipelineCache, rig.fn.vkDestroyPipelineCache, empty))) {
            return {};
        }
        Words words;
        result = Workload::vulkan(rig, words, cache.get());
        if (result != VK_SUCCESS || words != baseline) {
            problems.add("a pipeline built into an empty cache " + (result != VK_SUCCESS ? "failed: " + vk::Kit::name(result) : std::string("gave different output")));
            return {};
        }
        std::size_t size = 0;
        if (!vk::Kit::succeeds(problems, "pipeline cache size query", rig.fn.vkGetPipelineCacheData(rig.device, cache.get(), &size, nullptr))) {
            return {};
        }
        if (size < 32) {
            return "the driver returns no pipeline cache data (" + std::to_string(size) + " bytes)";
        }
        Bytes blob(size);
        std::size_t got = size;
        if (!vk::Kit::succeeds(problems, "pipeline cache data", rig.fn.vkGetPipelineCacheData(rig.device, cache.get(), &got, blob.data()))) {
            return {};
        }
        blob.resize(got);
        const auto build_from = [&](const Bytes& data, const std::string& what, bool must_match_reference) {
            VkPipelineCacheCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
            info.initialDataSize = data.size();
            info.pInitialData = data.data();
            VulkanOwned<VkPipelineCache> loaded;
            const VkResult created = rig.make(loaded, rig.fn.vkCreatePipelineCache, rig.fn.vkDestroyPipelineCache, info);
            if (created != VK_SUCCESS) {
                if (must_match_reference) {
                    problems.add(what + ": vkCreatePipelineCache failed with " + vk::Kit::name(created));
                }
                return;
            }
            Words again;
            const VkResult built = Workload::vulkan(rig, again, loaded.get());
            if (built != VK_SUCCESS) {
                problems.add(what + ": a pipeline built from the cache failed with " + vk::Kit::name(built));
            } else if (again != baseline) {
                problems.add(what + ": a pipeline built from the cache gave different output");
            }
        };
        build_from(blob, "a cache reloaded from its own data", true);
        Bytes wrong_uuid = blob;
        wrong_uuid[16] = static_cast<std::uint8_t>(~wrong_uuid[16]);
        wrong_uuid[17] = static_cast<std::uint8_t>(~wrong_uuid[17]);
        build_from(wrong_uuid, "a cache with a foreign UUID", false);
        Bytes wrong_vendor = blob;
        wrong_vendor[8] = static_cast<std::uint8_t>(~wrong_vendor[8]);
        build_from(wrong_vendor, "a cache with a foreign vendor id", false);
        Bytes wrong_device = blob;
        wrong_device[12] = static_cast<std::uint8_t>(~wrong_device[12]);
        build_from(wrong_device, "a cache with a foreign device id", false);
        Bytes wrong_version = blob;
        wrong_version[4] = 0x7F;
        build_from(wrong_version, "a cache with an unknown header version", false);
        Bytes header_only(blob.begin(), blob.begin() + 32);
        build_from(header_only, "a cache holding only its header", false);
        return {};
    });
}

Outcome gl_part(Context& context) {
    GlRig rig;
    std::string error;
    if (!gl::Kit::open(rig, context, error, 64, 64)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!Workload::gl_supported(api)) {
        return Context::skip("compute shaders are not available in this context");
    }
    if (!api.supports(4, 1, "GL_ARB_get_program_binary") || api.glGetProgramBinary == nullptr || api.glProgramBinary == nullptr || api.glProgramParameteri == nullptr) {
        return Context::skip("GL_ARB_get_program_binary is not available in this context");
    }
    if (api.integer(kNumProgramBinaryFormats) == 0) {
        return Context::skip("the context advertises no program binary formats");
    }
    Words baseline;
    if (!Workload::gl(api, baseline, error)) {
        return Outcome::fail("the reference program failed: " + error);
    }
    api.drain_errors();
    const auto link = [&](unsigned program) {
        api.glLinkProgram(program);
        int status = 0;
        api.glGetProgramiv(program, glc::LINK_STATUS, &status);
        return status != 0;
    };
    unsigned shader = api.glCreateShader(glc::COMPUTE_SHADER);
    const char* source = Workload::glsl();
    api.glShaderSource(shader, 1, &source, nullptr);
    api.glCompileShader(shader);
    unsigned program = api.glCreateProgram();
    api.glAttachShader(program, shader);
    api.glProgramParameteri(program, kProgramBinaryRetrievableHint, 1);
    Problems problems;
    if (!link(program)) {
        return Outcome::fail("the retrievable program failed to link");
    }
    int length = 0;
    api.glGetProgramiv(program, kProgramBinaryLength, &length);
    if (length <= 0) {
        api.glDeleteProgram(program);
        api.glDeleteShader(shader);
        return Context::skip("the driver returns no program binary (length " + std::to_string(length) + ")");
    }
    Bytes blob(static_cast<std::size_t>(length));
    int written = 0;
    unsigned format = 0;
    api.glGetProgramBinary(program, length, &written, &format, blob.data());
    if (api.glGetError() != 0 || written <= 0) {
        problems.add("glGetProgramBinary failed");
    }
    blob.resize(static_cast<std::size_t>(std::max(written, 0)));
    const auto run_binary = [&](const Bytes& data, unsigned binary_format, const std::string& what, bool must_link) {
        unsigned loaded = api.glCreateProgram();
        api.drain_errors();
        api.glProgramBinary(loaded, binary_format, data.data(), static_cast<int>(data.size()));
        const unsigned raised = api.glGetError();
        int status = 0;
        api.glGetProgramiv(loaded, glc::LINK_STATUS, &status);
        if (status != 0 && raised == 0) {
            unsigned buffer = 0;
            api.glGenBuffers(1, &buffer);
            api.glBindBuffer(glc::SHADER_STORAGE_BUFFER, buffer);
            api.glBufferData(glc::SHADER_STORAGE_BUFFER, Workload::kCount * 4, nullptr, glc::DYNAMIC_DRAW);
            api.glBindBufferBase(glc::SHADER_STORAGE_BUFFER, 0, buffer);
            api.glUseProgram(loaded);
            api.glDispatchCompute(Workload::kCount / 64, 1, 1);
            api.glMemoryBarrier(glc::SHADER_STORAGE_BARRIER_BIT | glc::BUFFER_UPDATE_BARRIER_BIT);
            Words words(Workload::kCount, 0);
            api.glGetBufferSubData(glc::SHADER_STORAGE_BUFFER, 0, Workload::kCount * 4, words.data());
            api.glFinish();
            api.glUseProgram(0);
            api.glBindBuffer(glc::SHADER_STORAGE_BUFFER, 0);
            api.glDeleteBuffers(1, &buffer);
            if (api.glGetError() != 0) {
                problems.add(what + ": running the loaded program raised an error");
            } else if (words != baseline) {
                problems.add(what + ": the loaded program gave different output");
            }
        } else if (must_link) {
            problems.add(what + ": the program did not link (error " + GlApi::error_name(raised) + ")");
        }
        api.glDeleteProgram(loaded);
        api.drain_errors();
    };
    run_binary(blob, format, "a program reloaded from its own binary", true);
    run_binary(blob, format ^ 0x00FFFF00u, "a binary with an unknown format", false);
    Bytes truncated(blob.begin(), blob.begin() + static_cast<std::ptrdiff_t>(blob.size() / 2));
    run_binary(truncated, format, "a truncated binary", false);
    Bytes empty_blob(4, 0);
    run_binary(empty_blob, format, "a binary of zero bytes", false);
    api.glDeleteProgram(program);
    api.glDeleteShader(shader);
    Words fallback;
    if (!Workload::gl(api, fallback, error)) {
        problems.add("compiling again after the rejected binaries failed: " + error);
    } else if (fallback != baseline) {
        problems.add("compiling again after the rejected binaries gave different output");
    }
    return problems.outcome();
}

bool run_pso(D3d12Rig& rig, ID3D12RootSignature* signature, ID3D12PipelineState* state, Words& out, std::string& error) {
    ID3D12Resource* buffer = D3d12Tools::device_buffer(rig, Workload::kCount * 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (buffer == nullptr) {
        error = "creating the output buffer failed";
        return false;
    }
    const bool ran = rig.direct().run_wait(
        [&](ID3D12GraphicsCommandList* list) {
            list->SetComputeRootSignature(signature);
            list->SetPipelineState(state);
            list->SetComputeRootUnorderedAccessView(0, buffer->GetGPUVirtualAddress());
            list->Dispatch(Workload::kCount / 64, 1, 1);
        },
        error);
    return ran && D3d12Tools::read_words(rig, buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, Workload::kCount, out, error);
}

Outcome d3d12_part(Context& context) {
    static_cast<void>(context);
    D3d12Rig rig;
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error) || !rig.open(adapter, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    D3d12Root root;
    root.uav(0);
    ID3D12RootSignature* signature = root.build(rig, error);
    Bytes code;
    if (signature == nullptr || !D3d12Tools::compile(Workload::hlsl(), "main", "cs_5_1", code, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = signature;
    desc.CS = D3d12Tools::bytecode(code);
    Com<ID3D12PipelineState> reference;
    HRESULT result = rig.device()->CreateComputePipelineState(&desc, __uuidof(ID3D12PipelineState), reference.put_void());
    if (FAILED(result)) {
        return Outcome::fail("CreateComputePipelineState failed: " + D3d12Tools::hr(result));
    }
    Words baseline;
    if (!run_pso(rig, signature, reference.get(), baseline, error)) {
        return Outcome::fail(error);
    }
    Com<ID3DBlob> cached;
    result = reference->GetCachedBlob(cached.put());
    if (FAILED(result) || !cached || cached->GetBufferSize() == 0) {
        problems.add("GetCachedBlob returned no data: " + D3d12Tools::hr(result));
    } else {
        const Bytes blob(static_cast<const std::uint8_t*>(cached->GetBufferPointer()),
                         static_cast<const std::uint8_t*>(cached->GetBufferPointer()) + cached->GetBufferSize());
        const auto create_with = [&](const Bytes& data, const std::string& what, bool must_work) {
            D3D12_COMPUTE_PIPELINE_STATE_DESC with_cache = desc;
            with_cache.CachedPSO = {data.data(), data.size()};
            Com<ID3D12PipelineState> state;
            const HRESULT made = rig.device()->CreateComputePipelineState(&with_cache, __uuidof(ID3D12PipelineState), state.put_void());
            if (FAILED(made)) {
                if (must_work) {
                    problems.add(what + " failed with " + D3d12Tools::hr(made));
                } else if (!D3d12Tools::in(made, {D3d12Tools::kDriverVersionMismatch, D3d12Tools::kAdapterNotFound, E_INVALIDARG})) {
                    problems.add(what + " failed with " + D3d12Tools::hr(made) + ", expected D3D12_ERROR_DRIVER_VERSION_MISMATCH, D3D12_ERROR_ADAPTER_NOT_FOUND or E_INVALIDARG");
                }
                return;
            }
            Words words;
            std::string run_error;
            ID3D12PipelineState* held = rig.hold(std::move(state));
            if (!run_pso(rig, signature, held, words, run_error)) {
                problems.add(what + ": " + run_error);
            } else if (words != baseline) {
                problems.add(what + ": the pipeline gave different output");
            }
        };
        create_with(blob, "a pipeline created with its own cached blob", true);
        Bytes garbled = blob;
        for (std::size_t i = 0; i < garbled.size() && i < 64; ++i) {
            garbled[i] = static_cast<std::uint8_t>(~garbled[i]);
        }
        create_with(garbled, "a pipeline created with a garbled cached blob", false);
        Bytes truncated(blob.begin(), blob.begin() + static_cast<std::ptrdiff_t>(blob.size() / 2));
        create_with(truncated, "a pipeline created with a truncated cached blob", false);
    }
    Com<ID3D12Device1> device1 = rig.as<ID3D12Device1>();
    if (device1) {
        Com<ID3D12PipelineLibrary> library;
        result = device1->CreatePipelineLibrary(nullptr, 0, __uuidof(ID3D12PipelineLibrary), library.put_void());
        if (FAILED(result)) {
            problems.add("CreatePipelineLibrary failed: " + D3d12Tools::hr(result));
        } else {
            result = library->StorePipeline(L"workload", reference.get());
            const SIZE_T size = library->GetSerializedSize();
            Bytes serialized(size);
            if (FAILED(result) || size == 0 || FAILED(library->Serialize(serialized.data(), size))) {
                problems.add("storing and serializing the pipeline library failed: " + D3d12Tools::hr(result));
            } else {
                Com<ID3D12PipelineLibrary> reloaded;
                result = device1->CreatePipelineLibrary(serialized.data(), serialized.size(), __uuidof(ID3D12PipelineLibrary), reloaded.put_void());
                Com<ID3D12PipelineState> loaded;
                if (FAILED(result)) {
                    problems.add("a pipeline library reloaded from its own blob failed with " + D3d12Tools::hr(result));
                } else if (FAILED(result = reloaded->LoadComputePipeline(L"workload", &desc, __uuidof(ID3D12PipelineState), loaded.put_void()))) {
                    problems.add("loading the stored pipeline from the reloaded library failed with " + D3d12Tools::hr(result));
                } else {
                    Words words;
                    ID3D12PipelineState* held = rig.hold(std::move(loaded));
                    if (!run_pso(rig, signature, held, words, error)) {
                        problems.add("the pipeline loaded from the library: " + error);
                    } else if (words != baseline) {
                        problems.add("the pipeline loaded from the library gave different output");
                    }
                    Com<ID3D12PipelineState> missing;
                    result = reloaded->LoadComputePipeline(L"absent", &desc, __uuidof(ID3D12PipelineState), missing.put_void());
                    if (SUCCEEDED(result) || result != E_INVALIDARG) {
                        problems.add("loading an unknown name from a pipeline library returned " + D3d12Tools::hr(result) + ", expected E_INVALIDARG");
                    }
                }
                Bytes truncated(serialized.begin(), serialized.begin() + static_cast<std::ptrdiff_t>(serialized.size() / 2));
                Com<ID3D12PipelineLibrary> broken;
                result = device1->CreatePipelineLibrary(truncated.data(), truncated.size(), __uuidof(ID3D12PipelineLibrary), broken.put_void());
                if (SUCCEEDED(result)) {
                    problems.add("a truncated pipeline library blob was accepted");
                } else if (!D3d12Tools::in(result, {D3d12Tools::kDriverVersionMismatch, D3d12Tools::kAdapterNotFound, E_INVALIDARG})) {
                    problems.add("a truncated pipeline library blob returned " + D3d12Tools::hr(result) + ", expected D3D12_ERROR_DRIVER_VERSION_MISMATCH, D3D12_ERROR_ADAPTER_NOT_FOUND or E_INVALIDARG");
                }
            }
        }
    }
    Com<ID3D12PipelineState> fallback;
    result = rig.device()->CreateComputePipelineState(&desc, __uuidof(ID3D12PipelineState), fallback.put_void());
    Words words;
    if (FAILED(result)) {
        problems.add("compiling again after the rejected blobs failed with " + D3d12Tools::hr(result));
    } else {
        ID3D12PipelineState* held = rig.hold(std::move(fallback));
        if (!run_pso(rig, signature, held, words, error) || words != baseline) {
            problems.add("compiling again after the rejected blobs gave " + (error.empty() ? std::string("different output") : error));
        }
    }
    return problems.outcome();
}

}  // namespace

Outcome CacheChecks::reuse_correct(Context& context) {
    Problems problems;
    std::vector<std::string> skips;
    bool exercised = false;
    if (Kit::enabled("vulkan")) {
        Kit::merge("Vulkan", vulkan_part(context), problems, skips, exercised);
    } else {
        skips.push_back("Vulkan excluded");
    }
    if (Kit::enabled("gl")) {
        Kit::merge("OpenGL", gl_part(context), problems, skips, exercised);
    } else {
        skips.push_back("OpenGL excluded");
    }
    if (Kit::enabled("d3d12")) {
        Kit::merge("D3D12", d3d12_part(context), problems, skips, exercised);
    } else {
        skips.push_back("D3D12 excluded");
    }
    return Kit::finish(skips, problems, exercised);
}

}  // namespace lsw::suites::sanity::shader
