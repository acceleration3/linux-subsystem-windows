#include "sanity/d3d12/opt_checks.hpp"

#include "sanity/d3d12/kit.hpp"

#include <cstring>

namespace lsw::suites::sanity::d3d12 {

namespace {

const char* kRateVs =
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "  float2 p = float2((id << 1) & 2, id & 2);\n"
    "  return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "}\n";

const char* kRatePs =
    "float4 main(float4 p : SV_Position) : SV_Target { return float4(float(uint(p.x) & 1u), float(uint(p.y) & 1u), 0.0, 1.0); }\n";

const char* kRovVs =
    "struct VO { float4 pos : SV_Position; nointerpolation uint iid : IID; };\n"
    "VO main(uint id : SV_VertexID, uint iid : SV_InstanceID) {\n"
    "  float2 p = float2((id << 1) & 2, id & 2);\n"
    "  VO o; o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1); o.iid = iid; return o;\n"
    "}\n";

const char* kRovPs =
    "struct VO { float4 pos : SV_Position; nointerpolation uint iid : IID; };\n"
    "RasterizerOrderedTexture2D<uint> rov : register(u0);\n"
    "float4 main(VO i) : SV_Target { uint2 c = uint2(i.pos.xy); rov[c] = rov[c] * 3u + i.iid + 1u; return float4(0, 0, 0, 0); }\n";

const char* kTinyVs =
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "  float2 p[3] = {float2(3.1, 4.1), float2(3.4, 4.1), float2(3.1, 4.4)};\n"
    "  return float4(p[id].x / 4.0 - 1.0, 1.0 - p[id].y / 4.0, 0, 1);\n"
    "}\n";

const char* kWhitePs = "float4 main() : SV_Target { return float4(1, 1, 1, 1); }\n";

const char* kLoadR32 =
    "RWTexture2D<uint> t : register(u0);\n"
    "RWStructuredBuffer<uint> o : register(u1);\n"
    "[numthreads(8, 8, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) { o[id.y * 8u + id.x] = t[id.xy]; }\n";

const char* kLoadRgba8 =
    "RWTexture2D<unorm float4> t : register(u0);\n"
    "RWStructuredBuffer<uint> o : register(u1);\n"
    "[numthreads(8, 8, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) {\n"
    "  float4 v = t[id.xy];\n"
    "  o[id.y * 8u + id.x] = uint(v.x * 255.0 + 0.5) | (uint(v.y * 255.0 + 0.5) << 8) | (uint(v.z * 255.0 + 0.5) << 16) | (uint(v.w * 255.0 + 0.5) << 24);\n"
    "}\n";

const char* kLoadRg16 =
    "RWTexture2D<uint2> t : register(u0);\n"
    "RWStructuredBuffer<uint> o : register(u1);\n"
    "[numthreads(8, 8, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) { uint2 v = t[id.xy]; o[id.y * 8u + id.x] = v.x | (v.y << 16); }\n";

bool tile_uniform(const TexelImage& image, std::string& where) {
    for (UINT y = 0; y + 1 < image.height; y += 2) {
        for (UINT x = 0; x + 1 < image.width; x += 2) {
            const std::uint32_t a = image.pixel(x, y);
            if (image.pixel(x + 1, y) != a || image.pixel(x, y + 1) != a || image.pixel(x + 1, y + 1) != a) {
                where = "the 2x2 block at (" + std::to_string(x) + "," + std::to_string(y) + ") is not uniform";
                return false;
            }
        }
    }
    return true;
}

bool fine_pattern(const TexelImage& image, std::string& where) {
    for (UINT y = 0; y < image.height; ++y) {
        for (UINT x = 0; x < image.width; ++x) {
            const std::uint32_t want = Kit::rgba((x & 1) != 0 ? 255 : 0, (y & 1) != 0 ? 255 : 0, 0, 255);
            if (image.pixel(x, y) != want) {
                where = "pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is 0x" + Text::hex(image.pixel(x, y), 8) + ", expected 0x" + Text::hex(want, 8);
                return false;
            }
        }
    }
    return true;
}

bool coarse_pattern(const TexelImage& image, std::string& where) {
    if (!tile_uniform(image, where)) {
        return false;
    }
    for (UINT y = 0; y < image.height; ++y) {
        for (UINT x = 0; x < image.width; ++x) {
            const std::uint32_t value = image.pixel(x, y);
            if (((value & 0xff) != 0 && (value & 0xff) != 255) || (((value >> 8) & 0xff) != 0 && ((value >> 8) & 0xff) != 255) || ((value >> 16) & 0xff) != 0 || (value >> 24) != 255) {
                where = "pixel (" + std::to_string(x) + "," + std::to_string(y) + ") has an impossible value 0x" + Text::hex(value, 8);
                return false;
            }
        }
    }
    return true;
}

}  // namespace

Outcome OptChecks::raytracing(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
        if (FAILED(D3d12Tools::feature(rig.device(), D3D12_FEATURE_D3D12_OPTIONS5, options)) || options.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
            return "raytracing is not reported (RaytracingTier NOT_SUPPORTED)";
        }
        context.measure("d3d12.raytracing_tier", static_cast<int>(options.RaytracingTier));
        Com<ID3D12Device5> device5 = rig.as<ID3D12Device5>();
        if (!device5) {
            problems.add("RaytracingTier is reported but ID3D12Device5 is not exposed");
            return {};
        }
        std::string error;
        const float triangle[9] = {0, 0, 1, 1, 0, 1, 0, 1, 1};
        ID3D12Resource* vertices = D3d12Tools::upload(rig, 256);
        ID3D12Resource* instances = D3d12Tools::upload(rig, 256);
        if (vertices == nullptr || instances == nullptr) {
            problems.add("creating the input buffers failed");
            return {};
        }
        void* mapped = nullptr;
        const D3D12_RANGE none{0, 0};
        vertices->Map(0, &none, &mapped);
        std::memcpy(mapped, triangle, sizeof(triangle));
        vertices->Unmap(0, nullptr);
        D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
        geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geometry.Triangles.VertexCount = 3;
        geometry.Triangles.VertexBuffer.StartAddress = vertices->GetGPUVirtualAddress();
        geometry.Triangles.VertexBuffer.StrideInBytes = 12;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottom{};
        bottom.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        bottom.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        bottom.NumDescs = 1;
        bottom.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        bottom.pGeometryDescs = &geometry;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottom_info{};
        device5->GetRaytracingAccelerationStructurePrebuildInfo(&bottom, &bottom_info);
        if (bottom_info.ResultDataMaxSizeInBytes == 0 || bottom_info.ScratchDataSizeInBytes == 0) {
            problems.add("the prebuild info of a one-triangle BLAS reports a zero result or scratch size");
            return {};
        }
        const D3D12_RESOURCE_FLAGS unordered = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ID3D12Resource* bottom_buffer = D3d12Tools::device_buffer(rig, D3d12Tools::align(bottom_info.ResultDataMaxSizeInBytes, 256), D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, unordered);
        ID3D12Resource* bottom_scratch = D3d12Tools::device_buffer(rig, D3d12Tools::align(bottom_info.ScratchDataSizeInBytes, 256), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, unordered);
        if (bottom_buffer == nullptr || bottom_scratch == nullptr) {
            problems.add("creating the BLAS buffers failed");
            return {};
        }
        D3D12_RAYTRACING_INSTANCE_DESC instance{};
        instance.Transform[0][0] = instance.Transform[1][1] = instance.Transform[2][2] = 1.0f;
        instance.InstanceID = 7;
        instance.InstanceMask = 0xff;
        instance.AccelerationStructure = bottom_buffer->GetGPUVirtualAddress();
        instances->Map(0, &none, &mapped);
        std::memcpy(mapped, &instance, sizeof(instance));
        instances->Unmap(0, nullptr);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top{};
        top.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        top.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        top.NumDescs = 1;
        top.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        top.InstanceDescs = instances->GetGPUVirtualAddress();
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO top_info{};
        device5->GetRaytracingAccelerationStructurePrebuildInfo(&top, &top_info);
        if (top_info.ResultDataMaxSizeInBytes == 0 || top_info.ScratchDataSizeInBytes == 0) {
            problems.add("the prebuild info of a one-instance TLAS reports a zero result or scratch size");
            return {};
        }
        ID3D12Resource* top_buffer = D3d12Tools::device_buffer(rig, D3d12Tools::align(top_info.ResultDataMaxSizeInBytes, 256), D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, unordered);
        ID3D12Resource* top_scratch = D3d12Tools::device_buffer(rig, D3d12Tools::align(top_info.ScratchDataSizeInBytes, 256), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, unordered);
        ID3D12Resource* post = D3d12Tools::device_buffer(rig, 256, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, unordered);
        ID3D12Resource* readback = D3d12Tools::readback(rig, 256);
        if (top_buffer == nullptr || top_scratch == nullptr || post == nullptr || readback == nullptr) {
            problems.add("creating the TLAS buffers failed");
            return {};
        }
        bool have_list4 = true;
        const bool built = rig.direct().run_wait(
            [&](ID3D12GraphicsCommandList* list) {
                Com<ID3D12GraphicsCommandList4> list4;
                list->QueryInterface(__uuidof(ID3D12GraphicsCommandList4), list4.put_void());
                if (!list4) {
                    have_list4 = false;
                    return;
                }
                D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_bottom{};
                build_bottom.DestAccelerationStructureData = bottom_buffer->GetGPUVirtualAddress();
                build_bottom.Inputs = bottom;
                build_bottom.ScratchAccelerationStructureData = bottom_scratch->GetGPUVirtualAddress();
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC infos[2]{};
                infos[0].DestBuffer = post->GetGPUVirtualAddress();
                infos[0].InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_CURRENT_SIZE;
                infos[1].DestBuffer = post->GetGPUVirtualAddress() + 8;
                infos[1].InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
                list4->BuildRaytracingAccelerationStructure(&build_bottom, 2, infos);
                D3d12Tools::uav_wait(list, bottom_buffer);
                D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_top{};
                build_top.DestAccelerationStructureData = top_buffer->GetGPUVirtualAddress();
                build_top.Inputs = top;
                build_top.ScratchAccelerationStructureData = top_scratch->GetGPUVirtualAddress();
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC top_post{};
                top_post.DestBuffer = post->GetGPUVirtualAddress() + 16;
                top_post.InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_CURRENT_SIZE;
                list4->BuildRaytracingAccelerationStructure(&build_top, 1, &top_post);
                D3d12Tools::uav_wait(list, top_buffer);
                D3d12Tools::move(list, post, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
                list->CopyBufferRegion(readback, 0, post, 0, 24);
            },
            error);
        if (!have_list4) {
            problems.add("RaytracingTier is reported but ID3D12GraphicsCommandList4 is not exposed");
            return {};
        }
        std::vector<std::uint8_t> raw;
        if (!built || !D3d12Tools::map_bytes(readback, 0, 24, raw, error)) {
            problems.add("building the acceleration structures failed: " + error);
            return {};
        }
        std::uint64_t sizes[3];
        std::memcpy(sizes, raw.data(), sizeof(sizes));
        if (sizes[0] == 0 || sizes[0] > bottom_info.ResultDataMaxSizeInBytes) {
            problems.add("the current size of the BLAS is " + std::to_string(sizes[0]) + " against a maximum of " + std::to_string(bottom_info.ResultDataMaxSizeInBytes));
        }
        if (sizes[1] == 0 || sizes[1] > sizes[0]) {
            problems.add("the compacted size of the BLAS is " + std::to_string(sizes[1]) + " against a current size of " + std::to_string(sizes[0]));
        }
        if (sizes[2] == 0 || sizes[2] > top_info.ResultDataMaxSizeInBytes) {
            problems.add("the current size of the TLAS is " + std::to_string(sizes[2]) + " against a maximum of " + std::to_string(top_info.ResultDataMaxSizeInBytes));
        }
        context.measure("d3d12.raytracing_traced", false);
        context.note("acceleration structures were built and sized; tracing rays needs DXIL shaders, which this build cannot produce (no dxc), so hit and miss results were not compared");
        return {};
    });
}

Outcome OptChecks::mesh_shaders(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 options{};
        const HRESULT result = D3d12Tools::feature(rig.device(), D3D12_FEATURE_D3D12_OPTIONS7, options);
        context.measure("d3d12.mesh_shader_tier", FAILED(result) ? -1 : static_cast<int>(options.MeshShaderTier));
        D3D12_FEATURE_DATA_SHADER_MODEL model{};
        model.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(0x65);
        const bool sm65 = SUCCEEDED(rig.device()->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model)));
        if (SUCCEEDED(result) && options.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED && !sm65) {
            problems.add("mesh shaders are reported but shader model 6.5 is not");
        }
        if (SUCCEEDED(result) && options.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED && sm65) {
            context.note("shader model 6.5 is reported without mesh shaders");
        }
        if (!problems.empty()) {
            return {};
        }
        return FAILED(result) || options.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED
                   ? "mesh shaders are not reported; their pipelines need DXIL, which this build cannot produce"
                   : "mesh shaders are reported; rendering with them needs DXIL shaders, which this build cannot produce (no dxc)";
    });
}

Outcome OptChecks::vrs_sampler_feedback(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::string error;
        std::string skipped;
        D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6{};
        const bool have6 = SUCCEEDED(D3d12Tools::feature(rig.device(), D3D12_FEATURE_D3D12_OPTIONS6, options6));
        const D3D12_VARIABLE_SHADING_RATE_TIER tier = have6 ? options6.VariableShadingRateTier : D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED;
        context.measure("d3d12.vrs_tier", static_cast<int>(tier));
        if (tier == D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED) {
            skipped = "variable rate shading is not reported";
        } else {
            D3d12Root empty;
            ID3D12RootSignature* signature = empty.build(rig, error);
            ID3D12PipelineState* state = signature != nullptr ? Kit::pipeline_with(rig, signature, kRateVs, kRatePs, DXGI_FORMAT_R8G8B8A8_UNORM, error) : nullptr;
            if (state == nullptr) {
                problems.add("setup failed: " + error);
                return {};
            }
            auto render = [&](const std::string& label, bool coarse, const std::function<void(ID3D12GraphicsCommandList5*)>& setup, const std::function<void(ID3D12GraphicsCommandList*)>& before) {
                D3d12Frame frame;
                TexelImage image;
                bool have5 = true;
                std::string local;
                if (!frame.open(rig, 32, 32, local) ||
                    !frame.run(
                        [&](ID3D12GraphicsCommandList* list) {
                            Com<ID3D12GraphicsCommandList5> list5;
                            list->QueryInterface(__uuidof(ID3D12GraphicsCommandList5), list5.put_void());
                            if (!list5) {
                                have5 = false;
                                return;
                            }
                            if (before) {
                                before(list);
                            }
                            frame.clear(list, 0, 0, 0, 0);
                            setup(list5.get());
                            Kit::draw_fullscreen(list, signature, state);
                            list5->RSSetShadingRate(D3D12_SHADING_RATE_1X1, nullptr);
                        },
                        local) ||
                    !frame.read(image, local)) {
                    problems.add(label + ": " + local);
                    return;
                }
                if (!have5) {
                    problems.add("variable rate shading is reported but ID3D12GraphicsCommandList5 is not exposed");
                    return;
                }
                std::string where;
                if (coarse ? !coarse_pattern(image, where) : !fine_pattern(image, where)) {
                    problems.add(label + ": " + where);
                }
            };
            render("shading rate 1x1", false, [](ID3D12GraphicsCommandList5* list) { list->RSSetShadingRate(D3D12_SHADING_RATE_1X1, nullptr); }, {});
            render("shading rate 2x2", true, [](ID3D12GraphicsCommandList5* list) { list->RSSetShadingRate(D3D12_SHADING_RATE_2X2, nullptr); }, {});
            if (tier >= D3D12_VARIABLE_SHADING_RATE_TIER_2) {
                const UINT tile = options6.ShadingRateImageTileSize;
                const UINT edge = (32 + tile - 1) / tile;
                auto make_image = [&](std::uint8_t rate) -> ID3D12Resource* {
                    ID3D12Resource* image = D3d12Tools::texture(rig, D3d12Tools::texture_desc(edge, edge, DXGI_FORMAT_R8_UINT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
                                                                D3D12_RESOURCE_STATE_COPY_DEST);
                    const std::vector<std::uint8_t> texels(static_cast<std::size_t>(edge) * edge, rate);
                    if (image == nullptr || !D3d12Tools::write_texture(rig, image, D3D12_RESOURCE_STATE_COPY_DEST, 0, texels.data(), error)) {
                        problems.add("creating the shading rate image failed: " + error);
                        return nullptr;
                    }
                    return image;
                };
                ID3D12Resource* coarse_image = make_image(static_cast<std::uint8_t>(D3D12_SHADING_RATE_2X2));
                ID3D12Resource* fine_image = make_image(static_cast<std::uint8_t>(D3D12_SHADING_RATE_1X1));
                if (coarse_image != nullptr && fine_image != nullptr) {
                    const D3D12_SHADING_RATE_COMBINER override_image[2] = {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_OVERRIDE};
                    const D3D12_SHADING_RATE_COMBINER max_image[2] = {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_MAX};
                    const D3D12_SHADING_RATE_COMBINER min_image[2] = {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_MIN};
                    auto to_source = [&](ID3D12Resource* image) {
                        return [image](ID3D12GraphicsCommandList* list) { D3d12Tools::move(list, image, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE); };
                    };
                    render("a 2x2 shading rate image overriding 1x1", true,
                           [&](ID3D12GraphicsCommandList5* list) {
                               list->RSSetShadingRateImage(coarse_image);
                               list->RSSetShadingRate(D3D12_SHADING_RATE_1X1, override_image);
                           },
                           to_source(coarse_image));
                    render("a 2x2 shading rate image combined with 1x1 by MAX", true,
                           [&](ID3D12GraphicsCommandList5* list) {
                               list->RSSetShadingRateImage(coarse_image);
                               list->RSSetShadingRate(D3D12_SHADING_RATE_1X1, max_image);
                           },
                           {});
                    render("a 1x1 shading rate image combined with 2x2 by MIN", false,
                           [&](ID3D12GraphicsCommandList5* list) {
                               list->RSSetShadingRateImage(fine_image);
                               list->RSSetShadingRate(D3D12_SHADING_RATE_2X2, min_image);
                           },
                           to_source(fine_image));
                }
            }
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
        const bool have7 = SUCCEEDED(D3d12Tools::feature(rig.device(), D3D12_FEATURE_D3D12_OPTIONS7, options7));
        const D3D12_SAMPLER_FEEDBACK_TIER feedback = have7 ? options7.SamplerFeedbackTier : D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED;
        context.measure("d3d12.sampler_feedback_tier", static_cast<int>(feedback));
        Com<ID3D12Device8> device8 = rig.as<ID3D12Device8>();
        if (device8) {
            D3D12_RESOURCE_DESC1 desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = 64;
            desc.Height = 64;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = static_cast<DXGI_FORMAT>(189);
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            desc.SamplerFeedbackMipRegion = {4, 4, 1};
            const D3D12_HEAP_PROPERTIES heap = D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT);
            Com<ID3D12Resource> map;
            const HRESULT made = device8->CreateCommittedResource2(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, nullptr,
                                                                  __uuidof(ID3D12Resource), map.put_void());
            if (feedback == D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED && SUCCEEDED(made)) {
                problems.add("a MIN_MIP sampler feedback map was created although SamplerFeedbackTier is NOT_SUPPORTED");
            } else if (feedback != D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED && FAILED(made)) {
                problems.add("a MIN_MIP sampler feedback map cannot be created although the tier is reported: " + Kit::hr(made));
            } else if (SUCCEEDED(made)) {
                static_cast<void>(rig.hold(std::move(map)));
            }
        }
        if (!problems.empty()) {
            return {};
        }
        context.note("sampler feedback maps are created and rejected per the tier; recording which mips a shader samples needs DXIL shaders, which this build cannot produce");
        return skipped;
    });
}

Outcome OptChecks::work_graphs_dxr11(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::string error;
        ID3D12Device* d = rig.device();
        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        static_cast<void>(D3d12Tools::feature(d, D3D12_FEATURE_D3D12_OPTIONS, options));
        {
            D3d12Root root;
            root.table(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0).uav(1);
            ID3D12RootSignature* signature = root.build(rig, error);
            const std::vector<std::uint8_t> raw = Kit::bytes(1400, 8 * 8 * 4);
            struct Case {
                DXGI_FORMAT format;
                const char* source;
                const char* name;
                bool additional;
            };
            const Case cases[] = {{DXGI_FORMAT_R32_UINT, kLoadR32, "R32_UINT", false},
                                  {DXGI_FORMAT_R8G8B8A8_UNORM, kLoadRgba8, "R8G8B8A8_UNORM", true},
                                  {DXGI_FORMAT_R16G16_UINT, kLoadRg16, "R16G16_UINT", true}};
            for (const Case& item : cases) {
                D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
                support.Format = item.format;
                const bool known = SUCCEEDED(d->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)));
                const bool typed = known && (support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) != 0;
                const bool load = known && (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0;
                if (item.additional && options.TypedUAVLoadAdditionalFormats && typed && !load) {
                    problems.add(std::string(item.name) + " has typed UAV support but no typed load although TypedUAVLoadAdditionalFormats is reported");
                }
                if (!typed || !load || signature == nullptr) {
                    continue;
                }
                ID3D12PipelineState* pso = D3d12Tools::compute_from(rig, signature, item.source, error);
                ID3D12Resource* texture = D3d12Tools::texture(rig, D3d12Tools::texture_desc(8, 8, item.format, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_COPY_DEST);
                ID3D12Resource* output = D3d12Tools::device_buffer(rig, 64 * 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
                ID3D12DescriptorHeap* heap = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
                if (pso == nullptr || texture == nullptr || output == nullptr || heap == nullptr || !D3d12Tools::write_texture(rig, texture, D3D12_RESOURCE_STATE_COPY_DEST, 0, raw.data(), error)) {
                    problems.add(std::string("typed UAV load of ") + item.name + ": setup failed: " + error);
                    continue;
                }
                D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
                view.Format = item.format;
                view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                d->CreateUnorderedAccessView(texture, nullptr, &view, heap->GetCPUDescriptorHandleForHeapStart());
                std::vector<std::uint8_t> back;
                if (!rig.direct().run_wait(
                        [&](ID3D12GraphicsCommandList* list) {
                            D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                            ID3D12DescriptorHeap* heaps[] = {heap};
                            list->SetDescriptorHeaps(1, heaps);
                            list->SetComputeRootSignature(signature);
                            list->SetPipelineState(pso);
                            list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
                            list->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
                            list->Dispatch(1, 1, 1);
                        },
                        error) ||
                    !D3d12Tools::read_buffer(rig, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, 64 * 4, back, error)) {
                    problems.add(std::string("typed UAV load of ") + item.name + ": " + error);
                    continue;
                }
                Kit::compare_words(context, problems, std::string("typed UAV load of ") + item.name, Kit::words(raw), Kit::words(back));
            }
        }
        {
            D3d12Root root;
            root.table(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
            ID3D12RootSignature* signature = root.build(rig, error);
            D3d12Tools::Bytes vs;
            D3d12Tools::Bytes ps;
            std::string compile_error;
            if (signature == nullptr || !D3d12Tools::compile(kRovVs, "main", "vs_5_1", vs, compile_error) || !D3d12Tools::compile(kRovPs, "main", "ps_5_1", ps, compile_error)) {
                problems.add("raster ordered view setup failed: " + error + compile_error);
            } else {
                const D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = D3d12Tools::graphics_desc(signature, vs, ps, DXGI_FORMAT_R8G8B8A8_UNORM);
                HRESULT made = S_OK;
                std::string pso_error;
                ID3D12PipelineState* pso = D3d12Tools::graphics_pso(rig, desc, pso_error, &made);
                if (!options.ROVsSupported) {
                    if (pso != nullptr) {
                        problems.add("ROVsSupported is false but a pixel shader with a rasterizer ordered view was accepted");
                    }
                } else if (pso == nullptr) {
                    problems.add("ROVsSupported is true but the pipeline with a rasterizer ordered view failed: " + pso_error);
                } else {
                    ID3D12Resource* texture = D3d12Tools::texture(rig, D3d12Tools::texture_desc(16, 16, DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
                                                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    ID3D12DescriptorHeap* heap = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
                    D3d12Frame frame;
                    if (texture == nullptr || heap == nullptr || !frame.open(rig, 16, 16, error)) {
                        problems.add("raster ordered view resources failed: " + error);
                    } else {
                        D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
                        view.Format = DXGI_FORMAT_R32_UINT;
                        view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                        d->CreateUnorderedAccessView(texture, nullptr, &view, heap->GetCPUDescriptorHandleForHeapStart());
                        TexelImage image;
                        if (!frame.run(
                                [&](ID3D12GraphicsCommandList* list) {
                                    ID3D12DescriptorHeap* heaps[] = {heap};
                                    list->SetDescriptorHeaps(1, heaps);
                                    list->SetGraphicsRootSignature(signature);
                                    list->SetPipelineState(pso);
                                    list->SetGraphicsRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
                                    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                                    list->DrawInstanced(3, 8, 0, 0);
                                },
                                error) ||
                            !D3d12Tools::read_texture(rig, texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, image, error)) {
                            problems.add("drawing with a rasterizer ordered view: " + error);
                        } else {
                            std::uint32_t expected = 0;
                            for (std::uint32_t i = 0; i < 8; ++i) {
                                expected = expected * 3u + i + 1u;
                            }
                            std::string where;
                            if (!Kit::uniform(image, expected, where)) {
                                problems.add("eight overlapping instances through a rasterizer ordered view did not accumulate in primitive order: " + where);
                            }
                        }
                    }
                }
            }
        }
        {
            D3d12Root empty;
            ID3D12RootSignature* signature = empty.build(rig, error);
            const auto conservative = [](D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) { desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON; };
            std::string conservative_error;
            ID3D12PipelineState* plain = signature != nullptr ? Kit::pipeline_with(rig, signature, kTinyVs, kWhitePs, DXGI_FORMAT_R8G8B8A8_UNORM, error) : nullptr;
            ID3D12PipelineState* wide = signature != nullptr ? Kit::pipeline_with(rig, signature, kTinyVs, kWhitePs, DXGI_FORMAT_R8G8B8A8_UNORM, conservative_error, conservative) : nullptr;
            if (plain == nullptr) {
                problems.add("creating the tiny triangle pipeline failed: " + error);
            } else if (options.ConservativeRasterizationTier == D3D12_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED) {
                if (wide != nullptr) {
                    problems.add("conservative rasterization is not reported but a pipeline with it was created");
                }
            } else if (wide == nullptr) {
                problems.add("conservative rasterization is reported but its pipeline failed: " + conservative_error);
            } else {
                for (int mode = 0; mode < 2; ++mode) {
                    D3d12Frame frame;
                    TexelImage image;
                    ID3D12PipelineState* state = mode == 0 ? plain : wide;
                    if (!frame.open(rig, 8, 8, error) ||
                        !frame.run(
                            [&](ID3D12GraphicsCommandList* list) {
                                frame.clear(list, 0, 0, 0, 0);
                                list->SetGraphicsRootSignature(signature);
                                list->SetPipelineState(state);
                                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                                list->DrawInstanced(3, 1, 0, 0);
                            },
                            error) ||
                        !frame.read(image, error)) {
                        problems.add("conservative rasterization draw: " + error);
                        continue;
                    }
                    for (UINT y = 0; y < 8; ++y) {
                        for (UINT x = 0; x < 8; ++x) {
                            const bool on = image.pixel(x, y) != 0;
                            const bool target = x == 3 && y == 4;
                            const bool close_by = (x >= 2 && x <= 4 && y >= 3 && y <= 5);
                            if (mode == 0 && on) {
                                problems.add("without conservative rasterization a triangle that misses every pixel centre covers pixel (" + std::to_string(x) + "," + std::to_string(y) + ")");
                            }
                            if (mode == 1 && target && !on) {
                                problems.add("conservative rasterization misses the pixel the triangle lies in");
                            }
                            if (mode == 1 && !close_by && on) {
                                problems.add("conservative rasterization covers pixel (" + std::to_string(x) + "," + std::to_string(y) + ") far from the triangle");
                            }
                            if (mode == 1 && !target && close_by && on && options.ConservativeRasterizationTier >= D3D12_CONSERVATIVE_RASTERIZATION_TIER_2) {
                                problems.add("conservative rasterization tier 2 covers neighbouring pixel (" + std::to_string(x) + "," + std::to_string(y) + ")");
                            }
                        }
                    }
                }
            }
        }
        context.note("wave operations, 16-bit and 64-bit integer operations, shader-model 6 atomics and work graphs need DXIL shaders, which this build cannot produce (no dxc); only typed UAV loads, rasterizer ordered views and conservative rasterization were exercised");
        return {};
    });
}

}  // namespace lsw::suites::sanity::d3d12
