#include "sanity/d3d12/dev_checks.hpp"

#include "sanity/d3d12/kit.hpp"
#include "support/d3d11_device.hpp"
#include "support/format_codec.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::sanity::d3d12 {

namespace {

const char* level_name(D3D_FEATURE_LEVEL level) {
    switch (level) {
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_12_0: return "12_0";
    case D3D_FEATURE_LEVEL_12_1: return "12_1";
    case D3D_FEATURE_LEVEL_12_2: return "12_2";
    default: return "?";
    }
}

D3D_SHADER_MODEL highest_shader_model(ID3D12Device* device) {
    static const int candidates[] = {0x51, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68};
    int best = 0;
    for (int candidate : candidates) {
        D3D12_FEATURE_DATA_SHADER_MODEL model{};
        model.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(candidate);
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model)))) {
            best = std::max(best, static_cast<int>(model.HighestShaderModel));
        }
    }
    return static_cast<D3D_SHADER_MODEL>(best);
}

}  // namespace

Outcome DevChecks::create_levels(Context& context) {
    return Kit::each_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_12_0,
                                            D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_2};
        D3D12_FEATURE_DATA_FEATURE_LEVELS query{};
        query.NumFeatureLevels = 5;
        query.pFeatureLevelsRequested = levels;
        if (!Kit::succeeds(problems, "CheckFeatureSupport(FEATURE_LEVELS)", rig.device()->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &query, sizeof(query)))) {
            return {};
        }
        const D3D_FEATURE_LEVEL maximum = query.MaxSupportedFeatureLevel;
        context.measure("d3d12." + std::to_string(rig.luid.LowPart) + ".max_level", level_name(maximum));
        if (maximum < D3D_FEATURE_LEVEL_11_0) {
            problems.add("the device was created at 11_0 but the maximum level is below it");
        }
        DXGI_ADAPTER_DESC1 dxgi{};
        rig.adapter->GetDesc1(&dxgi);
        const LUID device_luid = rig.device()->GetAdapterLuid();
        if (device_luid.LowPart != dxgi.AdapterLuid.LowPart || device_luid.HighPart != dxgi.AdapterLuid.HighPart) {
            problems.add("GetAdapterLuid of the 11_0 device differs from the DXGI adapter LUID");
        }
        for (D3D_FEATURE_LEVEL level : levels) {
            const bool expected = level <= maximum;
            const HRESULT probe = ::D3D12CreateDevice(rig.adapter.get(), level, __uuidof(ID3D12Device), nullptr);
            const bool supported = SUCCEEDED(probe);
            if (supported != expected) {
                problems.add(std::string("D3D12CreateDevice(") + level_name(level) + ", NULL) returned " + Kit::hr(probe) +
                             " but CheckFeatureSupport reports a maximum of " + level_name(maximum));
            }
            if (!expected) {
                context.measure(std::string("unsupported.") + level_name(level), Kit::hr(probe));
                continue;
            }
            Com<ID3D12Device> device;
            const HRESULT created = ::D3D12CreateDevice(rig.adapter.get(), level, __uuidof(ID3D12Device), device.put_void());
            if (!Kit::succeeds(problems, std::string("D3D12CreateDevice at ") + level_name(level), created)) {
                continue;
            }
            const LUID luid = device->GetAdapterLuid();
            if (luid.LowPart != dxgi.AdapterLuid.LowPart || luid.HighPart != dxgi.AdapterLuid.HighPart) {
                problems.add(std::string("the ") + level_name(level) + " device reports another LUID than DXGI");
            }
            D3D12_FEATURE_DATA_FEATURE_LEVELS own{};
            own.NumFeatureLevels = 5;
            own.pFeatureLevelsRequested = levels;
            if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &own, sizeof(own))) && own.MaxSupportedFeatureLevel != maximum) {
                problems.add(std::string("the ") + level_name(level) + " device reports a different maximum level");
            }
        }
        return {};
    });
}

Outcome DevChecks::options_consistent(Context& context) {
    return Kit::each_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        ID3D12Device* d = rig.device();
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_12_0,
                                            D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_2};
        D3D12_FEATURE_DATA_FEATURE_LEVELS level_query{};
        level_query.NumFeatureLevels = 5;
        level_query.pFeatureLevelsRequested = levels;
        Kit::succeeds(problems, "FEATURE_LEVELS", d->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &level_query, sizeof(level_query)));
        const D3D_FEATURE_LEVEL level = level_query.MaxSupportedFeatureLevel;
        const int shader_model = static_cast<int>(highest_shader_model(d));
        context.measure("d3d12.shader_model", shader_model);
        if (shader_model < 0x51) {
            problems.add("the highest shader model is below 5.1, which every D3D12 device supports");
        }
        {
            bool previous = true;
            for (int candidate : {0x51, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68}) {
                D3D12_FEATURE_DATA_SHADER_MODEL model{};
                model.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(candidate);
                const HRESULT result = d->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model));
                const bool supported = SUCCEEDED(result);
                if (supported && !previous) {
                    problems.add("shader model 0x" + Text::hex(static_cast<unsigned>(candidate), 2) + " is supported although a lower one is not");
                }
                if (supported && static_cast<int>(model.HighestShaderModel) != candidate) {
                    problems.add("SHADER_MODEL " + Text::hex(static_cast<unsigned>(candidate), 2) + " succeeded but answered " + Text::hex(static_cast<unsigned>(model.HighestShaderModel), 2));
                }
                if (!supported && static_cast<int>(model.HighestShaderModel) > shader_model) {
                    problems.add("a failed SHADER_MODEL query answers a model above the highest supported");
                }
                previous = supported;
            }
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS o0{};
        if (Kit::succeeds(problems, "OPTIONS", D3d12Tools::feature(d, D3D12_FEATURE_D3D12_OPTIONS, o0))) {
            if (o0.ResourceBindingTier < 1 || o0.ResourceBindingTier > 3) {
                problems.add("ResourceBindingTier is out of range");
            }
            if (o0.ResourceHeapTier != D3D12_RESOURCE_HEAP_TIER_1 && o0.ResourceHeapTier != D3D12_RESOURCE_HEAP_TIER_2) {
                problems.add("ResourceHeapTier is out of range");
            }
            if (o0.TiledResourcesTier < 0 || o0.TiledResourcesTier > 4) {
                problems.add("TiledResourcesTier is out of range");
            }
            if (level >= D3D_FEATURE_LEVEL_12_1 && (o0.ROVsSupported == FALSE || o0.ConservativeRasterizationTier < D3D12_CONSERVATIVE_RASTERIZATION_TIER_1)) {
                problems.add("feature level 12_1 requires ROVs and conservative rasterization tier 1");
            }
            context.measure("d3d12.binding_tier", static_cast<int>(o0.ResourceBindingTier));
            context.measure("d3d12.heap_tier", static_cast<int>(o0.ResourceHeapTier));
            context.measure("d3d12.tiled_tier", static_cast<int>(o0.TiledResourcesTier));
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 o1{};
        if (Kit::succeeds(problems, "OPTIONS1", D3d12Tools::feature(d, D3D12_FEATURE_D3D12_OPTIONS1, o1))) {
            if (o1.WaveOps && shader_model < 0x60) {
                problems.add("WaveOps is reported but shader model 6.0 is not");
            }
            if (o1.WaveOps && o1.WaveLaneCountMin > o1.WaveLaneCountMax) {
                problems.add("WaveLaneCountMin exceeds WaveLaneCountMax");
            }
            if (o1.WaveOps && o1.TotalLaneCount < o1.WaveLaneCountMax) {
                problems.add("TotalLaneCount is below WaveLaneCountMax");
            }
        }
        D3D12_FEATURE_DATA_ARCHITECTURE arch{};
        D3D12_FEATURE_DATA_ARCHITECTURE1 arch1{};
        const HRESULT arch_result = D3d12Tools::feature(d, D3D12_FEATURE_ARCHITECTURE, arch);
        const HRESULT arch1_result = D3d12Tools::feature(d, D3D12_FEATURE_ARCHITECTURE1, arch1);
        Kit::succeeds(problems, "ARCHITECTURE", arch_result);
        Kit::succeeds(problems, "ARCHITECTURE1", arch1_result);
        if (SUCCEEDED(arch_result) && SUCCEEDED(arch1_result) &&
            (arch.UMA != arch1.UMA || arch.CacheCoherentUMA != arch1.CacheCoherentUMA || arch.TileBasedRenderer != arch1.TileBasedRenderer)) {
            problems.add("ARCHITECTURE and ARCHITECTURE1 disagree for node 0");
        }
        D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT va{};
        if (Kit::succeeds(problems, "GPU_VIRTUAL_ADDRESS_SUPPORT", D3d12Tools::feature(d, D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, va))) {
            if (va.MaxGPUVirtualAddressBitsPerProcess < va.MaxGPUVirtualAddressBitsPerResource) {
                problems.add("the per-process virtual address bits are below the per-resource bits");
            }
            context.measure("d3d12.va_bits_resource", static_cast<int>(va.MaxGPUVirtualAddressBitsPerResource));
        }
        D3D12_FEATURE_DATA_ROOT_SIGNATURE root{};
        root.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        const HRESULT root_result = d->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &root, sizeof(root));
        if (root.HighestVersion != D3D_ROOT_SIGNATURE_VERSION_1_0 && root.HighestVersion != D3D_ROOT_SIGNATURE_VERSION_1_1) {
            problems.add("ROOT_SIGNATURE answers a version that is neither 1.0 nor 1.1");
        }
        if (SUCCEEDED(root_result) && root.HighestVersion != D3D_ROOT_SIGNATURE_VERSION_1_1) {
            problems.add("ROOT_SIGNATURE succeeded for 1.1 but answers a lower version");
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS4 o4{};
        if (SUCCEEDED(D3d12Tools::feature(d, D3D12_FEATURE_D3D12_OPTIONS4, o4)) && o4.Native16BitShaderOpsSupported && shader_model < 0x62) {
            problems.add("Native16BitShaderOpsSupported is reported without shader model 6.2");
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
        const bool have5 = SUCCEEDED(D3d12Tools::feature(d, D3D12_FEATURE_D3D12_OPTIONS5, o5));
        if (have5 && o5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED && shader_model < 0x63) {
            problems.add("raytracing is reported without shader model 6.3");
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS6 o6{};
        const bool have6 = SUCCEEDED(D3d12Tools::feature(d, D3D12_FEATURE_D3D12_OPTIONS6, o6));
        if (have6) {
            if (o6.VariableShadingRateTier == D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED && o6.AdditionalShadingRatesSupported) {
                problems.add("AdditionalShadingRatesSupported is set without a shading rate tier");
            }
            if (o6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2 &&
                o6.ShadingRateImageTileSize != 8 && o6.ShadingRateImageTileSize != 16 && o6.ShadingRateImageTileSize != 32) {
                problems.add("ShadingRateImageTileSize is not 8, 16 or 32 at VRS tier 2");
            }
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7{};
        const bool have7 = SUCCEEDED(D3d12Tools::feature(d, D3D12_FEATURE_D3D12_OPTIONS7, o7));
        if (have7 && (o7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED || o7.SamplerFeedbackTier != D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED) &&
            shader_model < 0x65) {
            problems.add("mesh shaders or sampler feedback are reported without shader model 6.5");
        }
        if (level >= D3D_FEATURE_LEVEL_12_2) {
            if (!have5 || o5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0) {
                problems.add("feature level 12_2 requires raytracing tier 1.0");
            }
            if (!have6 || o6.VariableShadingRateTier < D3D12_VARIABLE_SHADING_RATE_TIER_2) {
                problems.add("feature level 12_2 requires variable rate shading tier 2");
            }
            if (!have7 || o7.MeshShaderTier < D3D12_MESH_SHADER_TIER_1) {
                problems.add("feature level 12_2 requires mesh shaders");
            }
            if (!have7 || o7.SamplerFeedbackTier < D3D12_SAMPLER_FEEDBACK_TIER_0_9) {
                problems.add("feature level 12_2 requires sampler feedback");
            }
        }
        struct Optional {
            D3D12_FEATURE feature;
            std::size_t size;
            const char* name;
        };
        const Optional optional[] = {
            {D3D12_FEATURE_D3D12_OPTIONS2, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS2), "OPTIONS2"},
            {D3D12_FEATURE_D3D12_OPTIONS3, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS3), "OPTIONS3"},
            {D3D12_FEATURE_D3D12_OPTIONS8, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS8), "OPTIONS8"},
            {D3D12_FEATURE_D3D12_OPTIONS9, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS9), "OPTIONS9"},
            {D3D12_FEATURE_D3D12_OPTIONS10, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS10), "OPTIONS10"},
            {D3D12_FEATURE_D3D12_OPTIONS11, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS11), "OPTIONS11"},
            {D3D12_FEATURE_D3D12_OPTIONS12, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS12), "OPTIONS12"},
            {D3D12_FEATURE_D3D12_OPTIONS13, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS13), "OPTIONS13"},
            {D3D12_FEATURE_D3D12_OPTIONS14, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS14), "OPTIONS14"},
            {D3D12_FEATURE_D3D12_OPTIONS15, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS15), "OPTIONS15"},
            {D3D12_FEATURE_D3D12_OPTIONS16, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS16), "OPTIONS16"},
            {D3D12_FEATURE_D3D12_OPTIONS17, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS17), "OPTIONS17"},
            {D3D12_FEATURE_D3D12_OPTIONS18, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS18), "OPTIONS18"},
        };
        for (const Optional& item : optional) {
            std::vector<std::uint8_t> storage(item.size, 0);
            const HRESULT result = d->CheckFeatureSupport(item.feature, storage.data(), static_cast<UINT>(item.size));
            context.measure(std::string("d3d12.") + item.name, Kit::hr(result));
            if (SUCCEEDED(result)) {
                std::vector<std::uint8_t> wrong(item.size + 1, 0);
                const HRESULT mismatch = d->CheckFeatureSupport(item.feature, wrong.data(), static_cast<UINT>(item.size + 1));
                if (SUCCEEDED(mismatch)) {
                    problems.add(std::string(item.name) + " succeeded with a structure size that is not its size");
                }
            }
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 o12{};
        if (SUCCEEDED(D3d12Tools::feature(d, D3D12_FEATURE_D3D12_OPTIONS12, o12))) {
            context.measure("d3d12.enhanced_barriers", o12.EnhancedBarriersSupported != FALSE);
            Com<ID3D12Device10> device10 = rig.as<ID3D12Device10>();
            if (o12.EnhancedBarriersSupported && !device10) {
                problems.add("EnhancedBarriersSupported is reported but ID3D12Device10 is not exposed");
            }
        }
        unsigned unknown = 9999;
        char scratch[64] = {};
        D3D12_FEATURE unknown_feature;
        std::memcpy(&unknown_feature, &unknown, sizeof(unknown_feature));
        if (SUCCEEDED(d->CheckFeatureSupport(unknown_feature, scratch, sizeof(scratch)))) {
            problems.add("CheckFeatureSupport succeeded for a feature value that does not exist");
        }
        if (SUCCEEDED(d->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, scratch, 1))) {
            problems.add("CheckFeatureSupport(OPTIONS) succeeded with a structure size of 1");
        }
        return {};
    });
}

Outcome DevChecks::format_support(Context& context) {
    return Kit::each_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        ID3D12Device* d = rig.device();
        D3d11Device twin;
        std::string error;
        const bool have_twin = twin.create_ex(rig.luid, nullptr, 0, 0, error);
        if (!have_twin) {
            context.note("the D3D11 twin is unavailable: " + error);
        }
        struct Pair {
            UINT d3d12;
            UINT d3d11;
            const char* name;
        };
        const Pair pairs[] = {
            {D3D12_FORMAT_SUPPORT1_TEXTURE2D, D3D11_FORMAT_SUPPORT_TEXTURE2D, "TEXTURE2D"},
            {D3D12_FORMAT_SUPPORT1_RENDER_TARGET, D3D11_FORMAT_SUPPORT_RENDER_TARGET, "RENDER_TARGET"},
            {D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL, D3D11_FORMAT_SUPPORT_DEPTH_STENCIL, "DEPTH_STENCIL"},
            {D3D12_FORMAT_SUPPORT1_BLENDABLE, D3D11_FORMAT_SUPPORT_BLENDABLE, "BLENDABLE"},
            {D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE, D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE, "MULTISAMPLE_RESOLVE"},
            {D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE, D3D11_FORMAT_SUPPORT_SHADER_SAMPLE, "SHADER_SAMPLE"},
            {D3D12_FORMAT_SUPPORT1_MIP, D3D11_FORMAT_SUPPORT_MIP, "MIP"},
        };
        int compared = 0;
        int created = 0;
        for (int value = 1; value <= 115; ++value) {
            const DXGI_FORMAT format = static_cast<DXGI_FORMAT>(value);
            D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
            support.Format = format;
            const HRESULT result = d->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
            const std::string label = "format " + std::to_string(value);
            D3D12_FEATURE_DATA_FORMAT_INFO info{};
            info.Format = format;
            const HRESULT info_result = d->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &info, sizeof(info));
            if (SUCCEEDED(info_result)) {
                if (info.PlaneCount < 1) {
                    problems.add(label + ": FORMAT_INFO reports no planes");
                } else if (!FormatCodec::is_video(format) && info.PlaneCount != 1) {
                    problems.add(label + ": FORMAT_INFO reports " + std::to_string(info.PlaneCount) + " planes for a non-planar format");
                }
            }
            if (FAILED(result)) {
                continue;
            }
            const UINT s1 = support.Support1;
            if (have_twin) {
                UINT other = 0;
                if (SUCCEEDED(twin.device->CheckFormatSupport(format, &other))) {
                    ++compared;
                    for (const Pair& pair : pairs) {
                        if (((s1 & pair.d3d12) != 0) != ((other & pair.d3d11) != 0)) {
                            problems.add(label + ": " + pair.name + " is " + ((s1 & pair.d3d12) != 0 ? "set" : "clear") +
                                         " in D3D12 and " + ((other & pair.d3d11) != 0 ? "set" : "clear") + " in D3D11");
                        }
                    }
                }
            }
            if ((s1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) != 0) {
                struct Variant {
                    UINT bit;
                    D3D12_RESOURCE_FLAGS flags;
                    const char* name;
                };
                const Variant variants[] = {
                    {D3D12_FORMAT_SUPPORT1_TEXTURE2D, D3D12_RESOURCE_FLAG_NONE, "plain"},
                    {D3D12_FORMAT_SUPPORT1_RENDER_TARGET, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, "render target"},
                    {D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, "depth stencil"},
                    {D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, "unordered access"},
                };
                for (const Variant& variant : variants) {
                    if ((s1 & variant.bit) == 0) {
                        continue;
                    }
                    HRESULT made = S_OK;
                    ID3D12Resource* resource = D3d12Tools::texture(rig, D3d12Tools::texture_desc(16, 16, format, variant.flags),
                                                                   D3D12_RESOURCE_STATE_COMMON, nullptr, &made);
                    ++created;
                    if (resource == nullptr) {
                        problems.add(label + ": the " + variant.name + " bit is reported but creating a 16x16 texture failed with " + Kit::hr(made));
                    } else {
                        rig.release(resource);
                    }
                }
            }
            for (UINT samples : {1u, 2u, 4u, 8u, 16u}) {
                D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{};
                levels.Format = format;
                levels.SampleCount = samples;
                if (FAILED(d->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels)))) {
                    continue;
                }
                const bool target = (s1 & (D3D12_FORMAT_SUPPORT1_RENDER_TARGET | D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL)) != 0;
                if (!target || levels.NumQualityLevels == 0) {
                    continue;
                }
                if (samples > 1 && (s1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET) == 0) {
                    problems.add(label + ": " + std::to_string(samples) + "x quality levels exist but MULTISAMPLE_RENDERTARGET is clear");
                }
                const bool depth = (s1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL) != 0;
                HRESULT made = S_OK;
                ID3D12Resource* resource = D3d12Tools::texture(
                    rig,
                    D3d12Tools::texture_desc(16, 16, format, depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 1, 1,
                                             samples),
                    D3D12_RESOURCE_STATE_COMMON, nullptr, &made);
                if (resource == nullptr) {
                    problems.add(label + ": " + std::to_string(samples) + "x has " + std::to_string(levels.NumQualityLevels) +
                                 " quality levels but creating the multisampled texture failed with " + Kit::hr(made));
                } else {
                    rig.release(resource);
                }
            }
        }
        context.measure("d3d12.format.compared_with_d3d11", compared);
        context.measure("d3d12.format.textures_created", created);
        const DXGI_FORMAT rendered[] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                        DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R8_UNORM,
                                        DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R8G8_UNORM};
        for (DXGI_FORMAT format : rendered) {
            D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
            support.Format = format;
            if (FAILED(d->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) ||
                (support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) == 0) {
                continue;
            }
            D3d12Frame frame;
            std::string frame_error;
            const std::string label = "render " + std::to_string(static_cast<int>(format));
            if (!frame.open(rig, 8, 8, frame_error, format) ||
                !frame.run([&](ID3D12GraphicsCommandList* list) { frame.clear(list, 0.2f, 0.6f, 1.0f, 0.0f); }, frame_error)) {
                problems.add(label + ": " + frame_error);
                continue;
            }
            TexelImage image;
            if (!frame.read(image, frame_error)) {
                problems.add(label + ": " + frame_error);
                continue;
            }
            const FormatSpec spec = FormatCodec::spec(format);
            if (!spec.known) {
                continue;
            }
            const double expected[4] = {0.2, 0.6, 1.0, 0.0};
            for (UINT y = 0; y < image.height; ++y) {
                std::string why;
                if (!FormatCodec::matches(spec, image.data.data() + static_cast<std::size_t>(y) * image.row_bytes, expected, why)) {
                    problems.add(label + ": a cleared row decodes wrong: " + why);
                    break;
                }
            }
        }
        return {};
    });
}

Outcome DevChecks::node_and_uma(Context& context) {
    return Kit::each_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        ID3D12Device* d = rig.device();
        const UINT nodes = d->GetNodeCount();
        if (nodes < 1) {
            problems.add("GetNodeCount returned 0");
            return {};
        }
        context.measure("d3d12.nodes", static_cast<int>(nodes));
        for (UINT node = 0; node < nodes; ++node) {
            const std::string label = "node " + std::to_string(node);
            D3D12_FEATURE_DATA_ARCHITECTURE1 arch1{};
            arch1.NodeIndex = node;
            D3D12_FEATURE_DATA_ARCHITECTURE arch{};
            arch.NodeIndex = node;
            if (!Kit::succeeds(problems, label + " ARCHITECTURE1", d->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &arch1, sizeof(arch1))) ||
                !Kit::succeeds(problems, label + " ARCHITECTURE", d->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &arch, sizeof(arch)))) {
                continue;
            }
            if (arch1.CacheCoherentUMA && !arch1.UMA) {
                problems.add(label + ": CacheCoherentUMA is set without UMA");
            }
            if (arch.UMA != arch1.UMA || arch.CacheCoherentUMA != arch1.CacheCoherentUMA || arch.TileBasedRenderer != arch1.TileBasedRenderer) {
                problems.add(label + ": ARCHITECTURE and ARCHITECTURE1 disagree");
            }
            context.measure("d3d12.uma", arch1.UMA != FALSE);
            context.measure("d3d12.cache_coherent_uma", arch1.CacheCoherentUMA != FALSE);
            for (D3D12_HEAP_TYPE type : {D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_TYPE_UPLOAD, D3D12_HEAP_TYPE_READBACK}) {
                const D3D12_HEAP_PROPERTIES custom = d->GetCustomHeapProperties(1u << node, type);
                const std::string name = label + " heap type " + std::to_string(static_cast<int>(type));
                if (custom.Type != D3D12_HEAP_TYPE_CUSTOM) {
                    problems.add(name + ": GetCustomHeapProperties does not return a CUSTOM heap");
                }
                if (custom.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_UNKNOWN || custom.MemoryPoolPreference == D3D12_MEMORY_POOL_UNKNOWN) {
                    problems.add(name + ": the custom properties still have UNKNOWN fields");
                }
                if (type != D3D12_HEAP_TYPE_DEFAULT && custom.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE) {
                    problems.add(name + ": the CPU cannot access an upload or readback heap");
                }
                if (type != D3D12_HEAP_TYPE_DEFAULT && custom.MemoryPoolPreference != D3D12_MEMORY_POOL_L0) {
                    problems.add(name + ": an upload or readback heap is not in the system memory pool L0");
                }
                if (arch1.UMA && custom.MemoryPoolPreference != D3D12_MEMORY_POOL_L0) {
                    problems.add(name + ": a UMA adapter reports a pool other than L0");
                }
                if (!arch1.UMA && type == D3D12_HEAP_TYPE_DEFAULT &&
                    (custom.MemoryPoolPreference != D3D12_MEMORY_POOL_L1 || custom.CPUPageProperty != D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE)) {
                    problems.add(name + ": a discrete adapter's default heap is not L1 without CPU access");
                }
                if (custom.CreationNodeMask != (1u << node) || custom.VisibleNodeMask != (1u << node)) {
                    problems.add(name + ": the node masks are not the requested node");
                }
                D3D12_HEAP_DESC desc{};
                desc.SizeInBytes = 64 * 1024;
                desc.Properties = custom;
                desc.Flags = Kit::buffer_heap_flags(rig);
                Com<ID3D12Heap> heap;
                const HRESULT made = d->CreateHeap(&desc, __uuidof(ID3D12Heap), heap.put_void());
                if (FAILED(made)) {
                    problems.add(name + ": CreateHeap with the returned custom properties failed with " + Kit::hr(made));
                } else {
                    ID3D12Heap* held = rig.hold(std::move(heap));
                    rig.release(held);
                }
            }
            const D3D12_HEAP_PROPERTIES upload = d->GetCustomHeapProperties(1u << node, D3D12_HEAP_TYPE_UPLOAD);
            const D3D12_HEAP_PROPERTIES readback = d->GetCustomHeapProperties(1u << node, D3D12_HEAP_TYPE_READBACK);
            if (upload.CPUPageProperty != D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE && upload.CPUPageProperty != D3D12_CPU_PAGE_PROPERTY_WRITE_BACK) {
                problems.add(label + ": the upload heap's CPU page property is neither write-combine nor write-back");
            }
            if (readback.CPUPageProperty != D3D12_CPU_PAGE_PROPERTY_WRITE_BACK) {
                problems.add(label + ": the readback heap's CPU page property is not write-back");
            }
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::d3d12
