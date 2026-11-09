#include "render/d3d12/scene_util.hpp"

#include <algorithm>

namespace lsw::suites::render::d3d12 {

namespace {

struct Mesh3 {
    std::vector<float> positions;
    std::vector<std::uint16_t> indices;
};

Mesh3 mesh_of(const MeshData& data) {
    Mesh3 mesh;
    for (const Vertex& v : data.vertices) {
        mesh.positions.insert(mesh.positions.end(), v.pos, v.pos + 3);
    }
    mesh.indices.assign(data.indices.begin(), data.indices.end());
    return mesh;
}

struct Accel {
    ID3D12Resource* result = nullptr;
    ID3D12Resource* scratch = nullptr;
    UINT64 result_size = 0;
};

struct Config {
    bool any_hit = false;
    bool procedural = false;
    bool local_root = false;
    bool shadow_miss = false;
};

class DxrRig {
public:
    [[nodiscard]] bool open(Kit& kit, std::string& error) {
        device5 = kit.rig.as<ID3D12Device5>();
        if (!device5) {
            return fail_with(error, "ID3D12Device5 is not available");
        }
        return true;
    }

    [[nodiscard]] bool make_accel(Kit& kit, const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs, Accel& out, std::string& error, UINT64 min_scratch = 0) {
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
        device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
        HRESULT result = S_OK;
        out.result = D3d12Tools::device_buffer(kit.rig, D3d12Tools::align(info.ResultDataMaxSizeInBytes, 256), D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &result);
        out.scratch = D3d12Tools::device_buffer(kit.rig, D3d12Tools::align(std::max(info.ScratchDataSizeInBytes, std::max(info.UpdateScratchDataSizeInBytes, min_scratch)), 256), D3D12_RESOURCE_STATE_COMMON,
                                                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &result);
        out.result_size = info.ResultDataMaxSizeInBytes;
        if (out.result == nullptr || out.scratch == nullptr) {
            return fail_with(error, "acceleration structure buffers: " + D3d12Tools::hr(result));
        }
        return true;
    }

    static void record_build(ID3D12GraphicsCommandList* list, const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs, const Accel& accel, ID3D12Resource* source = nullptr) {
        ID3D12GraphicsCommandList4* list4 = nullptr;
        if (FAILED(list->QueryInterface(__uuidof(ID3D12GraphicsCommandList4), reinterpret_cast<void**>(&list4)))) {
            return;
        }
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{};
        desc.DestAccelerationStructureData = accel.result->GetGPUVirtualAddress();
        desc.Inputs = inputs;
        desc.SourceAccelerationStructureData = source != nullptr ? source->GetGPUVirtualAddress() : 0;
        desc.ScratchAccelerationStructureData = accel.scratch->GetGPUVirtualAddress();
        list4->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
        const D3D12_RESOURCE_BARRIER barrier = D3d12Tools::uav_barrier(accel.result);
        list->ResourceBarrier(1, &barrier);
        list4->Release();
    }

    [[nodiscard]] bool make_pipeline(Kit& kit, const std::string& library, const Config& config, std::string& error) {
        Kit::Bytes code;
        if (!kit.compile_as(library, "", "lib", code, error)) {
            return false;
        }
        D3d12Root global;
        global.constants(8, 0).srv(0).table(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        global_signature = global.build(kit.rig, error);
        if (global_signature == nullptr) {
            return false;
        }
        D3d12Root local;
        local.constants(4, 1, 1);
        D3D12_ROOT_SIGNATURE_DESC local_desc{};
        D3d12Root local_builder;
        local_builder.constants(4, 1, 1).flags(D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
        Com<ID3DBlob> blob;
        if (config.local_root) {
            if (FAILED(local_builder.serialize(blob, error))) {
                return false;
            }
        }
        (void)local_desc;
        std::vector<D3D12_STATE_SUBOBJECT> subobjects;
        subobjects.reserve(12);
        D3D12_DXIL_LIBRARY_DESC library_desc{};
        library_desc.DXILLibrary = {code.data(), code.size()};
        subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &library_desc});
        D3D12_HIT_GROUP_DESC hit{};
        hit.HitGroupExport = L"HG";
        hit.Type = config.procedural ? D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE : D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hit.ClosestHitShaderImport = L"chit";
        hit.AnyHitShaderImport = config.any_hit ? L"ahit" : nullptr;
        hit.IntersectionShaderImport = config.procedural ? L"isect" : nullptr;
        subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hit});
        D3D12_RAYTRACING_SHADER_CONFIG shader_config{16, 8};
        subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shader_config});
        D3D12_GLOBAL_ROOT_SIGNATURE global_sub{global_signature};
        subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &global_sub});
        D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config{3};
        subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipeline_config});
        ID3D12RootSignature* local_signature = nullptr;
        D3D12_LOCAL_ROOT_SIGNATURE local_sub{};
        D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION association{};
        const WCHAR* exports[1] = {L"HG"};
        if (config.local_root) {
            Com<ID3D12RootSignature> created;
            const HRESULT result = kit.device()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature), created.put_void());
            if (FAILED(result)) {
                return fail_with(error, "local root signature: " + D3d12Tools::hr(result));
            }
            local_signature = kit.rig.hold(std::move(created));
            local_sub.pLocalRootSignature = local_signature;
            subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &local_sub});
            association.pSubobjectToAssociate = &subobjects.back();
            association.NumExports = 1;
            association.pExports = exports;
            subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &association});
        }
        D3D12_STATE_OBJECT_DESC desc{D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE, static_cast<UINT>(subobjects.size()), subobjects.data()};
        Com<ID3D12StateObject> object;
        const HRESULT result = device5->CreateStateObject(&desc, __uuidof(ID3D12StateObject), object.put_void());
        if (FAILED(result)) {
            return fail_with(error, "CreateStateObject: " + D3d12Tools::hr(result));
        }
        Com<ID3D12StateObjectProperties> properties;
        if (FAILED(object->QueryInterface(__uuidof(ID3D12StateObjectProperties), properties.put_void()))) {
            return fail_with(error, "ID3D12StateObjectProperties is not available");
        }
        std::vector<std::uint8_t> table(2048, 0);
        auto put = [&](UINT offset, const wchar_t* name) {
            const void* id = properties->GetShaderIdentifier(name);
            if (id != nullptr) {
                std::memcpy(table.data() + offset, id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
            }
        };
        put(0, L"raygen");
        put(256, L"miss");
        if (config.shadow_miss) {
            put(256 + 64, L"miss_shadow");
        }
        put(768, L"HG");
        put(768 + 64, L"HG");
        table_buffer = kit.make_upload(table.data(), table.size(), error);
        if (table_buffer == nullptr) {
            return false;
        }
        if (config.local_root) {
            std::uint8_t* mapped = nullptr;
            const D3D12_RANGE none{0, 0};
            table_buffer->Map(0, &none, reinterpret_cast<void**>(&mapped));
            const float colors[2][4] = {{0.9f, 0.3f, 0.2f, 1}, {0.2f, 0.8f, 0.4f, 1}};
            std::memcpy(mapped + 768 + 32, colors[0], 16);
            std::memcpy(mapped + 768 + 64 + 32, colors[1], 16);
            table_buffer->Unmap(0, nullptr);
        }
        state = kit.rig.hold(std::move(object));
        kit.rig.hold(std::move(properties));
        HRESULT heap_result = S_OK;
        heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true, &heap_result);
        TextureSpec spec;
        spec.width = 128;
        spec.height = 128;
        spec.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        output = TextureFactory::create(kit, spec, false, error);
        if (heap == nullptr || output == nullptr) {
            return heap == nullptr ? fail_with(error, "descriptor heap: " + D3d12Tools::hr(heap_result)) : false;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        kit.device()->CreateUnorderedAccessView(output, nullptr, &uav, heap->GetCPUDescriptorHandleForHeapStart());
        return true;
    }

    void record_dispatch(ID3D12GraphicsCommandList* list, ID3D12Resource* tlas, const float params[8], bool first) {
        ID3D12GraphicsCommandList4* list4 = nullptr;
        if (FAILED(list->QueryInterface(__uuidof(ID3D12GraphicsCommandList4), reinterpret_cast<void**>(&list4)))) {
            return;
        }
        if (first) {
            D3d12Tools::move(list, output, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        list->SetDescriptorHeaps(1, &heap);
        list->SetComputeRootSignature(global_signature);
        list->SetComputeRoot32BitConstants(0, 8, params, 0);
        list->SetComputeRootShaderResourceView(1, tlas->GetGPUVirtualAddress());
        list->SetComputeRootDescriptorTable(2, heap->GetGPUDescriptorHandleForHeapStart());
        list4->SetPipelineState1(state);
        D3D12_DISPATCH_RAYS_DESC rays{};
        const D3D12_GPU_VIRTUAL_ADDRESS base = table_buffer->GetGPUVirtualAddress();
        rays.RayGenerationShaderRecord = {base, 64};
        rays.MissShaderTable = {base + 256, 128, 64};
        rays.HitGroupTable = {base + 768, 128, 64};
        rays.Width = 128;
        rays.Height = 128;
        rays.Depth = 1;
        list4->DispatchRays(&rays);
        const D3D12_RESOURCE_BARRIER barrier = D3d12Tools::uav_barrier(output);
        list->ResourceBarrier(1, &barrier);
        list4->Release();
    }

    Com<ID3D12Device5> device5;
    ID3D12RootSignature* global_signature = nullptr;
    ID3D12StateObject* state = nullptr;
    ID3D12Resource* table_buffer = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    ID3D12Resource* output = nullptr;
};

D3D12_RAYTRACING_GEOMETRY_DESC triangles_of(ID3D12Resource* vertices, ID3D12Resource* indices, UINT vertex_count, UINT index_count, D3D12_RAYTRACING_GEOMETRY_FLAGS flags) {
    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = flags;
    geometry.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.IndexCount = index_count;
    geometry.Triangles.VertexCount = vertex_count;
    geometry.Triangles.IndexBuffer = indices->GetGPUVirtualAddress();
    geometry.Triangles.VertexBuffer = {vertices->GetGPUVirtualAddress(), 12};
    return geometry;
}

void set_instance(D3D12_RAYTRACING_INSTANCE_DESC& instance, const rr::Mat4& m, UINT id, UINT mask, UINT hit_index, D3D12_GPU_VIRTUAL_ADDRESS blas, UINT flags = 0) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            instance.Transform[r][c] = m.m[c * 4 + r];
        }
    }
    instance.InstanceID = id;
    instance.InstanceMask = mask;
    instance.InstanceContributionToHitGroupIndex = hit_index;
    instance.Flags = flags;
    instance.AccelerationStructure = blas;
}

const char* kHeader =
    "RaytracingAccelerationStructure scene : register(t0);\nRWTexture2D<float4> outimg : register(u0);\ncbuffer C : register(b0) { float4 p0; float4 p1; };\n"
    "struct Payload { float4 color; };\n"
    "float3 primary_origin(uint2 id, uint2 dim) { float2 ndc = (id + 0.5) / dim * 2 - 1; ndc.y = -ndc.y; return float3(ndc.x * 1.4, ndc.y * 1.4, 4); }\n"
    "[shader(\"raygeneration\")] void raygen() { uint2 id = DispatchRaysIndex().xy; uint2 dim = DispatchRaysDimensions().xy; RayDesc ray; ray.Origin = primary_origin(id, dim); ray.Direction = float3(0, 0, -1); ray.TMin = 0.001; ray.TMax = 100;\n"
    "  Payload p; p.color = float4(0.05, 0.06, 0.12, 1); TraceRay(scene, RAY_FLAG_NONE, (uint)p1.x, 0, 1, 0, ray, p); outimg[id] = p.color; }\n"
    "[shader(\"miss\")] void miss(inout Payload p) { float2 d = (float2)DispatchRaysIndex().xy / (float2)DispatchRaysDimensions().xy; p.color = float4(lerp(float3(0.1, 0.12, 0.3), float3(0.5, 0.6, 0.8), d.y), 1); }\n"
    "[shader(\"miss\")] void miss_shadow(inout Payload p) { p.color = float4(1, 1, 1, 1); }\n";

const char* kClosest =
    "[shader(\"closesthit\")] void chit(inout Payload p, BuiltInTriangleIntersectionAttributes a) { float3 b = float3(1 - a.barycentrics.x - a.barycentrics.y, a.barycentrics.x, a.barycentrics.y);\n"
    "  float3 base = lerp(float3(0.9, 0.55, 0.2), float3(0.2, 0.5, 0.9), saturate(InstanceID() / 3.0)); p.color = float4(base * (0.45 + 0.3 * b.x + 0.1 * (PrimitiveIndex() & 3)), 1); }\n";

bool render_once(Kit& kit, DxrRig& rig, ID3D12Resource* tlas, const float params[8], rr::CaptureSet& out, std::string& error) {
    if (!kit.run([&](ID3D12GraphicsCommandList* list) { rig.record_dispatch(list, tlas, params, true); }, error)) {
        return false;
    }
    rr::Image image;
    if (!kit.read_image(rig.output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
        return false;
    }
    out.add("", std::move(image));
    return true;
}

std::string needs_dxr(Kit& kit, D3D12_RAYTRACING_TIER tier) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
    const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS5, options);
    return SUCCEEDED(result) && options.RaytracingTier >= tier ? std::string() : std::string(tier == D3D12_RAYTRACING_TIER_1_1 ? "DXR 1.1 raytracing" : "DXR raytracing");
}

struct Scene {
    ID3D12Resource* vertex = nullptr;
    ID3D12Resource* index = nullptr;
    Accel blas;
    Accel tlas;
    ID3D12Resource* instances = nullptr;
    UINT instance_count = 0;
};

bool build_scene(Kit& kit, DxrRig& rig, const Mesh3& mesh, const std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instances, D3D12_RAYTRACING_GEOMETRY_FLAGS flags, Scene& scene, std::string& error) {
    scene.vertex = kit.make_upload(mesh.positions.data(), mesh.positions.size() * 4, error);
    scene.index = kit.make_upload(mesh.indices.data(), mesh.indices.size() * 2, error);
    if (scene.vertex == nullptr || scene.index == nullptr) {
        return false;
    }
    D3D12_RAYTRACING_GEOMETRY_DESC geometry = triangles_of(scene.vertex, scene.index, static_cast<UINT>(mesh.positions.size() / 3), static_cast<UINT>(mesh.indices.size()), flags);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs{};
    blas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blas_inputs.NumDescs = 1;
    blas_inputs.pGeometryDescs = &geometry;
    if (!rig.make_accel(kit, blas_inputs, scene.blas, error)) {
        return false;
    }
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> descs = instances;
    for (auto& d : descs) {
        d.AccelerationStructure = scene.blas.result->GetGPUVirtualAddress();
    }
    scene.instances = kit.make_upload(descs.data(), descs.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), error);
    scene.instance_count = static_cast<UINT>(descs.size());
    if (scene.instances == nullptr) {
        return false;
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
    tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlas_inputs.NumDescs = scene.instance_count;
    tlas_inputs.InstanceDescs = scene.instances->GetGPUVirtualAddress();
    if (!rig.make_accel(kit, tlas_inputs, scene.tlas, error)) {
        return false;
    }
    return kit.run([&](ID3D12GraphicsCommandList* list) {
        DxrRig::record_build(list, blas_inputs, scene.blas);
        DxrRig::record_build(list, tlas_inputs, scene.tlas);
    }, error);
}

std::vector<D3D12_RAYTRACING_INSTANCE_DESC> four_instances(bool masks) {
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> list(4);
    for (UINT k = 0; k < 4; ++k) {
        list[k] = {};
        const float x = -0.7f + 0.47f * static_cast<float>(k);
        set_instance(list[k], rr::Mat4::translate(x, (k % 2 == 0) ? 0.35f : -0.35f, 0.0f) * rr::Mat4::rotate_x(0.5f + 0.4f * static_cast<float>(k)) * rr::Mat4::rotate_y(0.6f) * rr::Mat4::scale(0.2f, 0.2f, 0.2f), k,
                     masks ? (k % 2 == 0 ? 0x01 : 0x02) : 0xff, 0, 0);
    }
    return list;
}

void register_trace_rows() {
    struct Row {
        const char* name;
        Config config;
        std::string source;
        bool masks;
        float mask_param;
        bool ground;
    };
    const std::string instance_shading = std::string(kHeader) + kClosest;
    const Row rows[] = {
        {"primary_rays_closest_hit_cube_instance_transform", {}, instance_shading, false, 255, false},
        {"instance_masks_filter_ray_mask", {}, instance_shading, true, 1, false},
        {"instance_masks_filter_ray_mask_other", {}, instance_shading, true, 2, false},
    };
    for (const Row& row : rows) {
        Cases::single(rr::CaseSpec("dxr", row.name).variant(4).tolerance(rr::Tolerance::blend(3).with_edge(1)).tag("raytracing").tag("x64-only"), [row](Kit& kit, rr::CaptureSet& out, std::string& error) {
            DxrRig rig;
            Scene scene;
            if (!rig.open(kit, error) || !rig.make_pipeline(kit, row.source, row.config, error) ||
                !build_scene(kit, rig, mesh_of(Shapes::cube()), four_instances(row.masks), D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE, scene, error)) {
                return false;
            }
            const float params[8] = {0, 0, 0, 0, row.mask_param, 0, 0, 0};
            return render_once(kit, rig, scene.tlas.result, params, out, error);
        }, [](Kit& kit) { return needs_dxr(kit, D3D12_RAYTRACING_TIER_1_0); });
    }
    const std::string shadow_source = std::string(kHeader) +
        "[shader(\"closesthit\")] void chit(inout Payload p, BuiltInTriangleIntersectionAttributes a) { float3 hit = WorldRayOrigin() + WorldRayDirection() * RayTCurrent(); RayDesc ray; ray.Origin = hit + float3(0, 0, 0.002); ray.Direction = normalize(float3(0.6, 0.5, 0.8)); ray.TMin = 0.001; ray.TMax = 50;\n"
        "  Payload shadow; shadow.color = float4(0, 0, 0, 1); TraceRay(scene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 0, 1, 1, ray, shadow);\n"
        "  float3 base = InstanceID() == 9 ? float3(0.6, 0.65, 0.55) : float3(0.9, 0.5, 0.25); p.color = float4(base * (0.3 + 0.7 * shadow.color.x), 1); }\n";
    Cases::single(rr::CaseSpec("dxr", "shadow_rays_from_hit_points_recursion").variant(4).tolerance(rr::Tolerance::blend(3).with_edge(1)).tag("raytracing").tag("x64-only"), [shadow_source](Kit& kit, rr::CaptureSet& out, std::string& error) {
        DxrRig rig;
        Scene scene;
        Config config;
        config.shadow_miss = true;
        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances(3);
        instances[0] = {};
        set_instance(instances[0], rr::Mat4::translate(0, 0, -0.9f) * rr::Mat4::scale(1.3f, 1.3f, 0.05f), 9, 0xff, 0, 0);
        instances[1] = {};
        set_instance(instances[1], rr::Mat4::translate(-0.3f, 0.1f, 0.0f) * rr::Mat4::rotate_y(0.5f) * rr::Mat4::scale(0.25f, 0.25f, 0.25f), 1, 0xff, 0, 0);
        instances[2] = {};
        set_instance(instances[2], rr::Mat4::translate(0.35f, -0.2f, -0.3f) * rr::Mat4::rotate_x(0.7f) * rr::Mat4::scale(0.2f, 0.2f, 0.2f), 2, 0xff, 0, 0);
        if (!rig.open(kit, error) || !rig.make_pipeline(kit, shadow_source, config, error) ||
            !build_scene(kit, rig, mesh_of(Shapes::cube()), instances, D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE, scene, error)) {
            return false;
        }
        const float params[8] = {0, 0, 0, 0, 255, 0, 0, 0};
        return render_once(kit, rig, scene.tlas.result, params, out, error);
    }, [](Kit& kit) { return needs_dxr(kit, D3D12_RAYTRACING_TIER_1_0); });
    const std::string anyhit_source = std::string(kHeader) + kClosest +
        "[shader(\"anyhit\")] void ahit(inout Payload p, BuiltInTriangleIntersectionAttributes a) { if (frac(a.barycentrics.x * 7.0 + a.barycentrics.y * 5.0) < 0.5) IgnoreHit(); }\n";
    Cases::single(rr::CaseSpec("dxr", "any_hit_ignore_hit_alpha_pattern").variant(4).tolerance(rr::Tolerance::blend(3).with_edge(1)).tag("raytracing").tag("x64-only"), [anyhit_source](Kit& kit, rr::CaptureSet& out, std::string& error) {
        DxrRig rig;
        Scene scene;
        Config config;
        config.any_hit = true;
        if (!rig.open(kit, error) || !rig.make_pipeline(kit, anyhit_source, config, error) ||
            !build_scene(kit, rig, mesh_of(Shapes::sphere(10, 14, 1.0f)), four_instances(false), D3D12_RAYTRACING_GEOMETRY_FLAG_NONE, scene, error)) {
            return false;
        }
        const float params[8] = {0, 0, 0, 0, 255, 0, 0, 0};
        return render_once(kit, rig, scene.tlas.result, params, out, error);
    }, [](Kit& kit) { return needs_dxr(kit, D3D12_RAYTRACING_TIER_1_0); });
    const std::string local_source = std::string(kHeader) + "cbuffer Local : register(b1, space1) { float4 local_color; };\n"
        "[shader(\"closesthit\")] void chit(inout Payload p, BuiltInTriangleIntersectionAttributes a) { p.color = float4(local_color.rgb * (0.5 + 0.5 * a.barycentrics.x), 1); }\n";
    Cases::single(rr::CaseSpec("dxr", "local_root_arguments_per_hit_group_record").variant(4).tolerance(rr::Tolerance::blend(3).with_edge(1)).tag("raytracing").tag("x64-only"), [local_source](Kit& kit, rr::CaptureSet& out, std::string& error) {
        DxrRig rig;
        Scene scene;
        Config config;
        config.local_root = true;
        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances = four_instances(false);
        for (UINT k = 0; k < 4; ++k) {
            instances[k].InstanceContributionToHitGroupIndex = k % 2;
        }
        if (!rig.open(kit, error) || !rig.make_pipeline(kit, local_source, config, error) ||
            !build_scene(kit, rig, mesh_of(Shapes::cube()), instances, D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE, scene, error)) {
            return false;
        }
        const float params[8] = {0, 0, 0, 0, 255, 0, 0, 0};
        return render_once(kit, rig, scene.tlas.result, params, out, error);
    }, [](Kit& kit) { return needs_dxr(kit, D3D12_RAYTRACING_TIER_1_0); });
}

void register_procedural_and_updates() {
    const std::string procedural_source = std::string(kHeader) +
        "struct Sphere { float4 attr; };\n"
        "[shader(\"intersection\")] void isect() { float3 o = ObjectRayOrigin(); float3 d = ObjectRayDirection(); float b = dot(o, d); float c = dot(o, o) - 0.8 * 0.8; float disc = b * b - c;\n"
        "  if (disc >= 0) { float t = -b - sqrt(disc); if (t > RayTMin() && t < RayTCurrent()) { BuiltInTriangleIntersectionAttributes at; at.barycentrics = float2(frac(t * 3.0), 0.5); ReportHit(t, 0, at); } } }\n"
        "[shader(\"closesthit\")] void chit(inout Payload p, BuiltInTriangleIntersectionAttributes a) { float3 hit = ObjectRayOrigin() + ObjectRayDirection() * RayTCurrent(); float3 n = normalize(hit); float l = saturate(dot(n, normalize(float3(0.5, 0.6, 0.7)))) * 0.8 + 0.2; p.color = float4(float3(0.9, 0.6, 0.25) * l, 1); }\n";
    Cases::single(rr::CaseSpec("dxr", "procedural_aabb_intersection_shader_spheres").variant(4).tolerance(rr::Tolerance::blend(4).with_edge(1)).tag("raytracing").tag("x64-only"), [procedural_source](Kit& kit, rr::CaptureSet& out, std::string& error) {
        DxrRig rig;
        Config config;
        config.procedural = true;
        if (!rig.open(kit, error) || !rig.make_pipeline(kit, procedural_source, config, error)) {
            return false;
        }
        const D3D12_RAYTRACING_AABB box{-1, -1, -1, 1, 1, 1};
        ID3D12Resource* boxes = kit.make_upload(&box, sizeof box, error);
        if (boxes == nullptr) {
            return false;
        }
        D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
        geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
        geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geometry.AABBs.AABBCount = 1;
        geometry.AABBs.AABBs = {boxes->GetGPUVirtualAddress(), sizeof box};
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs{};
        blas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        blas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        blas_inputs.NumDescs = 1;
        blas_inputs.pGeometryDescs = &geometry;
        Accel blas;
        if (!rig.make_accel(kit, blas_inputs, blas, error)) {
            return false;
        }
        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances(2);
        instances[0] = {};
        set_instance(instances[0], rr::Mat4::translate(-0.45f, 0.1f, 0) * rr::Mat4::scale(0.4f, 0.4f, 0.4f), 0, 0xff, 0, blas.result->GetGPUVirtualAddress());
        instances[1] = {};
        set_instance(instances[1], rr::Mat4::translate(0.45f, -0.1f, 0) * rr::Mat4::scale(0.5f, 0.3f, 0.5f), 1, 0xff, 0, blas.result->GetGPUVirtualAddress());
        ID3D12Resource* instance_buffer = kit.make_upload(instances.data(), instances.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), error);
        if (instance_buffer == nullptr) {
            return false;
        }
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
        tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        tlas_inputs.NumDescs = 2;
        tlas_inputs.InstanceDescs = instance_buffer->GetGPUVirtualAddress();
        Accel tlas;
        if (!rig.make_accel(kit, tlas_inputs, tlas, error) ||
            !kit.run([&](ID3D12GraphicsCommandList* list) {
                DxrRig::record_build(list, blas_inputs, blas);
                DxrRig::record_build(list, tlas_inputs, tlas);
            }, error)) {
            return false;
        }
        const float params[8] = {0, 0, 0, 0, 255, 0, 0, 0};
        return render_once(kit, rig, tlas.result, params, out, error);
    }, [](Kit& kit) { return needs_dxr(kit, D3D12_RAYTRACING_TIER_1_0); });
    Cases::frames(rr::CaseSpec("dxr", "blas_refit_update_animated_vertices_6_frames").variant(4).frames(6, {0, 2, 5}).tolerance(rr::Tolerance::blend(3).with_edge(1)).tag("raytracing").tag("x64-only"), [](Cases::Steps& steps) {
        struct State {
            DxrRig rig;
            Mesh3 mesh;
            ID3D12Resource* vertex = nullptr;
            ID3D12Resource* index = nullptr;
            ID3D12Resource* instances = nullptr;
            Accel blas;
            Accel tlas;
            D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
            std::vector<float> base;
        };
        auto s = std::make_shared<State>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            s->mesh = mesh_of(Shapes::sphere(14, 20, 1.0f));
            s->base = s->mesh.positions;
            const std::string source = std::string(kHeader) + kClosest;
            if (!s->rig.open(kit, error) || !s->rig.make_pipeline(kit, source, {}, error)) {
                return false;
            }
            s->vertex = kit.make_upload(s->mesh.positions.data(), s->mesh.positions.size() * 4, error);
            s->index = kit.make_upload(s->mesh.indices.data(), s->mesh.indices.size() * 2, error);
            if (s->vertex == nullptr || s->index == nullptr) {
                return false;
            }
            s->geometry = triangles_of(s->vertex, s->index, static_cast<UINT>(s->mesh.positions.size() / 3), static_cast<UINT>(s->mesh.indices.size()), D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE);
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs{};
            blas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            blas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
            blas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            blas_inputs.NumDescs = 1;
            blas_inputs.pGeometryDescs = &s->geometry;
            if (!s->rig.make_accel(kit, blas_inputs, s->blas, error)) {
                return false;
            }
            std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances(1);
            instances[0] = {};
            set_instance(instances[0], rr::Mat4::scale(0.6f, 0.6f, 0.6f) * rr::Mat4::rotate_x(0.4f), 0, 0xff, 0, s->blas.result->GetGPUVirtualAddress());
            s->instances = kit.make_upload(instances.data(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC), error);
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
            tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            tlas_inputs.NumDescs = 1;
            tlas_inputs.InstanceDescs = s->instances->GetGPUVirtualAddress();
            if (!s->rig.make_accel(kit, tlas_inputs, s->tlas, error)) {
                return false;
            }
            return kit.run([&](ID3D12GraphicsCommandList* list) {
                DxrRig::record_build(list, blas_inputs, s->blas);
                DxrRig::record_build(list, tlas_inputs, s->tlas);
            }, error);
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            std::uint8_t* mapped = nullptr;
            const D3D12_RANGE none{0, 0};
            s->vertex->Map(0, &none, reinterpret_cast<void**>(&mapped));
            auto* out = reinterpret_cast<float*>(mapped);
            for (std::size_t i = 0; i + 2 < s->base.size(); i += 3) {
                const float wave = 1.0f + 0.35f * std::sin(s->base[i + 1] * 4.0f + static_cast<float>(frame) * 0.9f);
                out[i] = s->base[i] * wave;
                out[i + 1] = s->base[i + 1];
                out[i + 2] = s->base[i + 2] * wave;
            }
            s->vertex->Unmap(0, nullptr);
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs{};
            blas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            blas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
            blas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            blas_inputs.NumDescs = 1;
            blas_inputs.pGeometryDescs = &s->geometry;
            const float params[8] = {0, 0, 0, 0, 255, 0, 0, 0};
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
            tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            tlas_inputs.NumDescs = 1;
            tlas_inputs.InstanceDescs = s->instances->GetGPUVirtualAddress();
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                DxrRig::record_build(list, blas_inputs, s->blas, s->blas.result);
                DxrRig::record_build(list, tlas_inputs, s->tlas);
                s->rig.record_dispatch(list, s->tlas.result, params, frame == 0);
            }, error);
        };
        steps.on_capture = [s](Kit& kit, unsigned, rr::CaptureSet& out, std::string& error) {
            rr::Image image;
            if (!kit.read_image(s->rig.output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
                return false;
            }
            out.add("", std::move(image));
            return true;
        };
    }, [](Kit& kit) { return needs_dxr(kit, D3D12_RAYTRACING_TIER_1_0); });
}

const char* kInlineBody =
    "RaytracingAccelerationStructure scene : register(t0);\nRWTexture2D<float4> outimg : register(u0);\ncbuffer C : register(b0) { float4 p0; float4 p1; };\n"
    "float4 shade(uint2 id, uint2 dim) { float2 ndc = (id + 0.5) / dim * 2 - 1; ndc.y = -ndc.y; RayDesc ray; ray.Origin = float3(ndc.x * 1.4, ndc.y * 1.4, 4); ray.Direction = float3(0, 0, -1); ray.TMin = 0.001; ray.TMax = 100;\n"
    "  RayQuery<RAY_FLAG_FORCE_OPAQUE> q; q.TraceRayInline(scene, RAY_FLAG_NONE, 0xff, ray); q.Proceed();\n"
    "  if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) { float2 b = q.CommittedTriangleBarycentrics(); float3 base = lerp(float3(0.9, 0.55, 0.2), float3(0.2, 0.5, 0.9), saturate(q.CommittedInstanceID() / 3.0)); return float4(base * (0.45 + 0.3 * (1 - b.x - b.y) + 0.1 * (q.CommittedPrimitiveIndex() & 3)), 1); }\n"
    "  return float4(0.05, 0.06, 0.12, 1); }\n";

bool build_inline_scene(Kit& kit, DxrRig& rig, Scene& scene, std::string& error) {
    return rig.open(kit, error) && build_scene(kit, rig, mesh_of(Shapes::cube()), four_instances(false), D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE, scene, error);
}

void register_inline_rows() {
    Cases::single(rr::CaseSpec("dxr", "inline_ray_query_compute_shader").variant(6).tolerance(rr::Tolerance::blend(3).with_edge(1)).tag("raytracing").tag("x64-only"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        DxrRig rig;
        Scene scene;
        if (!build_inline_scene(kit, rig, scene, error)) {
            return false;
        }
        D3d12Root global;
        global.constants(8, 0).srv(0).table(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        ID3D12RootSignature* signature = global.build(kit.rig, error);
        const std::string source = std::string(kInlineBody) + "[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) { outimg[id.xy] = shade(id.xy, uint2(128, 128)); }\n";
        Kit::Bytes code;
        if (signature == nullptr || !kit.compile_as(source, "main", "cs", code, error)) {
            return false;
        }
        ID3D12PipelineState* pso = D3d12Tools::compute_pso(kit.rig, signature, code, error);
        HRESULT result = S_OK;
        rig.heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true, &result);
        TextureSpec spec;
        spec.width = 128;
        spec.height = 128;
        spec.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        rig.output = TextureFactory::create(kit, spec, false, error);
        if (pso == nullptr || rig.heap == nullptr || rig.output == nullptr) {
            return false;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        kit.device()->CreateUnorderedAccessView(rig.output, nullptr, &uav, rig.heap->GetCPUDescriptorHandleForHeapStart());
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                D3d12Tools::move(list, rig.output, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                list->SetDescriptorHeaps(1, &rig.heap);
                list->SetComputeRootSignature(signature);
                list->SetPipelineState(pso);
                list->SetComputeRootShaderResourceView(1, scene.tlas.result->GetGPUVirtualAddress());
                list->SetComputeRootDescriptorTable(2, rig.heap->GetGPUDescriptorHandleForHeapStart());
                list->Dispatch(16, 16, 1);
            }, error)) {
            return false;
        }
        rr::Image image;
        if (!kit.read_image(rig.output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
            return false;
        }
        out.add("", std::move(image));
        return true;
    }, [](Kit& kit) { return needs_dxr(kit, D3D12_RAYTRACING_TIER_1_1); });
    Cases::single(rr::CaseSpec("dxr", "inline_ray_query_pixel_shader").variant(6).tolerance(rr::Tolerance::blend(3).with_edge(1)).tag("raytracing").tag("x64-only"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        DxrRig rig;
        Scene scene;
        if (!build_inline_scene(kit, rig, scene, error)) {
            return false;
        }
        D3d12Root global;
        global.constants(8, 0).srv(0).table(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        ID3D12RootSignature* signature = global.build(kit.rig, error);
        Surface target;
        if (signature == nullptr || !target.color(kit, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, error)) {
            return false;
        }
        std::string source = std::string(kInlineBody);
        source.replace(source.find("RWTexture2D<float4> outimg : register(u0);\n"), std::string("RWTexture2D<float4> outimg : register(u0);\n").size(), "");
        source += "float4 main(float4 pos : SV_Position) : SV_Target { return shade(uint2(pos.xy), uint2(128, 128)); }\n";
        Pso builder(kit, signature);
        builder.vs(Hlsl12::kFullVs).ps(source).layout({}).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
        ID3D12PipelineState* pso = builder.build(error);
        if (pso == nullptr) {
            return false;
        }
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                pass_target(list, target, 128, 128, nullptr);
                list->SetGraphicsRootSignature(signature);
                list->SetPipelineState(pso);
                list->SetGraphicsRootShaderResourceView(1, scene.tlas.result->GetGPUVirtualAddress());
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                list->DrawInstanced(3, 1, 0, 0);
            }, error)) {
            return false;
        }
        rr::Image image;
        if (!kit.read_image(target.resource, target.state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
            return false;
        }
        out.add("", std::move(image));
        return true;
    }, [](Kit& kit) { return needs_dxr(kit, D3D12_RAYTRACING_TIER_1_1); });
}

const struct Registrar {
    Registrar() {
        register_trace_rows();
        register_procedural_and_updates();
        register_inline_rows();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
