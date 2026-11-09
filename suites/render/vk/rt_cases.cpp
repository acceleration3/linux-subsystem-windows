#include "render/common/mat4.hpp"
#include "render/vk/quadkit.hpp"
#include "render/vk/scene_kit.hpp"

#include <cmath>
#include <cstring>

namespace lsw::render::vk {

namespace {

CaseSpec rt_spec(const std::string& name, bool pipeline) {
    CaseSpec spec("rt", name);
    spec.min_version(1, 2).extension("VK_KHR_acceleration_structure").extension("VK_KHR_deferred_host_operations").tolerance(Tolerance::exact());
    if (pipeline) {
        spec.extension("VK_KHR_ray_tracing_pipeline");
    } else {
        spec.extension("VK_KHR_ray_query");
    }
    return spec;
}

std::string rt_caps(Kit& kit, bool pipeline) {
    const auto as = kit.query_features<VkPhysicalDeviceAccelerationStructureFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR);
    if (as.accelerationStructure == VK_FALSE) {
        return "accelerationStructure";
    }
    if (pipeline) {
        const auto rt = kit.query_features<VkPhysicalDeviceRayTracingPipelineFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR);
        return rt.rayTracingPipeline != VK_FALSE ? std::string() : std::string("rayTracingPipeline");
    }
    const auto rq = kit.query_features<VkPhysicalDeviceRayQueryFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR);
    return rq.rayQuery != VK_FALSE ? std::string() : std::string("rayQuery");
}

struct Accel {
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    Buffer storage;
    VkDeviceAddress address = 0;
};

Buffer address_buffer(Rig& rig, const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer b = rig.blank(size, usage | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true, true);
    std::memcpy(b.mapped, data, static_cast<std::size_t>(size));
    return b;
}

void create_accel(Rig& rig, Accel& accel, VkAccelerationStructureTypeKHR type, VkDeviceSize size) {
    Kit& kit = rig.kit;
    accel.storage = rig.blank(size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false, true);
    VkAccelerationStructureCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    info.buffer = accel.storage.buffer;
    info.size = size;
    info.type = type;
    rig.vk_ok(kit.api.vkCreateAccelerationStructureKHR(kit.device, &info, nullptr, &accel.handle), "vkCreateAccelerationStructureKHR");
    const VkAccelerationStructureKHR handle = accel.handle;
    kit.defer([&kit, handle] { kit.api.vkDestroyAccelerationStructureKHR(kit.device, handle, nullptr); });
    VkAccelerationStructureDeviceAddressInfoKHR address{};
    address.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    address.accelerationStructure = accel.handle;
    accel.address = kit.api.vkGetAccelerationStructureDeviceAddressKHR(kit.device, &address);
}

void build_accel_geoms(Rig& rig, Accel& accel, VkAccelerationStructureTypeKHR type, const std::vector<VkAccelerationStructureGeometryKHR>& geometries,
                       const std::vector<std::uint32_t>& counts, VkBuildAccelerationStructureFlagsKHR flags, bool update, VkQueryPool compact_pool = VK_NULL_HANDLE) {
    Kit& kit = rig.kit;
    VkAccelerationStructureBuildGeometryInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    info.type = type;
    info.flags = flags;
    info.mode = update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    info.geometryCount = static_cast<std::uint32_t>(geometries.size());
    info.pGeometries = geometries.data();
    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    kit.api.vkGetAccelerationStructureBuildSizesKHR(kit.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, counts.data(), &sizes);
    if (accel.handle == VK_NULL_HANDLE) {
        create_accel(rig, accel, type, sizes.accelerationStructureSize);
    }
    const auto props = kit.query_properties<VkPhysicalDeviceAccelerationStructurePropertiesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR);
    const VkDeviceSize align = std::max<VkDeviceSize>(props.minAccelerationStructureScratchOffsetAlignment, 1);
    const VkDeviceSize scratch_size = (update ? sizes.updateScratchSize : sizes.buildScratchSize) + align;
    const Buffer scratch = rig.blank(scratch_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, true);
    info.dstAccelerationStructure = accel.handle;
    info.srcAccelerationStructure = update ? accel.handle : VK_NULL_HANDLE;
    info.scratchData.deviceAddress = (scratch.address + align - 1) / align * align;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> range_infos(geometries.size());
    for (std::size_t i = 0; i < geometries.size(); ++i) {
        range_infos[i].primitiveCount = counts[i];
    }
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = range_infos.data();
    rig.run([&](VkCommandBuffer cb) {
        if (compact_pool != VK_NULL_HANDLE) {
            kit.api.vkCmdResetQueryPool(cb, compact_pool, 0, 1);
        }
        kit.api.vkCmdBuildAccelerationStructuresKHR(cb, 1, &info, &ranges);
        VkMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        b.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        b.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        kit.api.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &b, 0, nullptr, 0, nullptr);
        if (compact_pool != VK_NULL_HANDLE) {
            kit.api.vkCmdWriteAccelerationStructuresPropertiesKHR(cb, 1, &accel.handle, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, compact_pool, 0);
        }
    });
}

void build_accel(Rig& rig, Accel& accel, VkAccelerationStructureTypeKHR type, const VkAccelerationStructureGeometryKHR& geometry, std::uint32_t primitives,
                 VkBuildAccelerationStructureFlagsKHR flags, bool update, VkQueryPool compact_pool = VK_NULL_HANDLE) {
    build_accel_geoms(rig, accel, type, {geometry}, {primitives}, flags, update, compact_pool);
}

VkAccelerationStructureGeometryKHR triangle_geometry(const Buffer& vertices, std::uint32_t vertex_count, const Buffer& indices) {
    VkAccelerationStructureGeometryKHR g{};
    g.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    g.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    g.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    g.geometry.triangles.vertexData.deviceAddress = vertices.address;
    g.geometry.triangles.vertexStride = 12;
    g.geometry.triangles.maxVertex = vertex_count - 1;
    g.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
    g.geometry.triangles.indexData.deviceAddress = indices.address;
    return g;
}

struct InstanceSpec {
    float x, y, z;
    float yaw;
    std::uint32_t custom;
    std::uint32_t mask;
    std::uint32_t sbt_offset;
    VkDeviceAddress blas;
};

Accel build_tlas(Rig& rig, const std::vector<InstanceSpec>& specs, Accel* reuse = nullptr) {
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    for (const InstanceSpec& s : specs) {
        VkAccelerationStructureInstanceKHR inst{};
        const float c = std::cos(s.yaw);
        const float sn = std::sin(s.yaw);
        inst.transform.matrix[0][0] = c;
        inst.transform.matrix[0][2] = sn;
        inst.transform.matrix[1][1] = 1.0f;
        inst.transform.matrix[2][0] = -sn;
        inst.transform.matrix[2][2] = c;
        inst.transform.matrix[0][3] = s.x;
        inst.transform.matrix[1][3] = s.y;
        inst.transform.matrix[2][3] = s.z;
        inst.instanceCustomIndex = s.custom;
        inst.mask = s.mask;
        inst.instanceShaderBindingTableRecordOffset = s.sbt_offset;
        inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        inst.accelerationStructureReference = s.blas;
        instances.push_back(inst);
    }
    const Buffer buffer = address_buffer(rig, instances.data(), instances.size() * sizeof(VkAccelerationStructureInstanceKHR), 0);
    VkAccelerationStructureGeometryKHR g{};
    g.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    g.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    g.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    g.geometry.instances.data.deviceAddress = buffer.address;
    Accel tlas = reuse != nullptr ? *reuse : Accel{};
    build_accel(rig, tlas, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, g, static_cast<std::uint32_t>(instances.size()), 0, false);
    return tlas;
}

struct QuadMesh {
    Buffer vertices;
    Buffer indices;
};

QuadMesh make_quad(Rig& rig, float rotate) {
    const float c = std::cos(rotate);
    const float s = std::sin(rotate);
    const float corners[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
    float v[12];
    for (int i = 0; i < 4; ++i) {
        v[i * 3] = corners[i][0] * c - corners[i][1] * s;
        v[i * 3 + 1] = corners[i][0] * s + corners[i][1] * c;
        v[i * 3 + 2] = 0.0f;
    }
    const std::uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
    QuadMesh m;
    m.vertices = address_buffer(rig, v, sizeof v, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    m.indices = address_buffer(rig, idx, sizeof idx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    return m;
}

struct TraceTarget {
    Texture image;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
};

TraceTarget make_trace_target(Rig& rig, VkShaderStageFlags stages) {
    Kit& kit = rig.kit;
    TraceTarget t;
    TextureDesc desc;
    desc.width = desc.height = 64;
    desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    t.image = rig.texture(desc);
    rig.run([&](VkCommandBuffer cb) { kit.barrier(cb, t.image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL); });
    t.set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, stages, nullptr}, {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, stages, nullptr}});
    VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    rig.vk_ok(kit.api.vkCreateDescriptorPool(kit.device, &pool_info, nullptr, &pool), "vkCreateDescriptorPool");
    kit.defer([&kit, pool] { kit.api.vkDestroyDescriptorPool(kit.device, pool, nullptr); });
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = pool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &t.set_layout;
    rig.vk_ok(kit.api.vkAllocateDescriptorSets(kit.device, &alloc, &t.set), "vkAllocateDescriptorSets");
    return t;
}

void write_trace(Rig& rig, TraceTarget& t, const Accel& tlas) {
    VkWriteDescriptorSetAccelerationStructureKHR as{};
    as.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    as.accelerationStructureCount = 1;
    as.pAccelerationStructures = &tlas.handle;
    const VkDescriptorImageInfo image{VK_NULL_HANDLE, t.image.view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet w[2]{};
    w[0].sType = w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].pNext = &as;
    w[0].dstSet = w[1].dstSet = t.set;
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    w[1].dstBinding = 1;
    w[1].descriptorCount = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[1].pImageInfo = &image;
    rig.kit.api.vkUpdateDescriptorSets(rig.kit.device, 2, w, 0, nullptr);
}

struct QueryTrace {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

QueryTrace make_query_pipeline(Rig& rig, TraceTarget& t) {
    QueryTrace q;
    q.layout = rig.playout({t.set_layout}, 16, VK_SHADER_STAGE_COMPUTE_BIT);
    q.pipeline = rig.compute("rq.v12.comp", q.layout);
    return q;
}

Image dispatch_query(Rig& rig, TraceTarget& t, QueryTrace& q, float all_mask) {
    rig.run([&](VkCommandBuffer cb) {
        rig.kit.api.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, q.pipeline);
        rig.kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, q.layout, 0, 1, &t.set, 0, nullptr);
        const float params[4] = {0.0f, all_mask, 0, 0};
        rig.push(cb, q.layout, VK_SHADER_STAGE_COMPUTE_BIT, params, 16);
        rig.kit.api.vkCmdDispatch(cb, 8, 8, 1);
    });
    return rig.read(t.image, VK_IMAGE_LAYOUT_GENERAL);
}

std::vector<InstanceSpec> scene_instances(VkDeviceAddress blas, float t) {
    return {{-0.55f, -0.35f, 0.0f, 0.3f + 0.2f * t, 0, 0x1, 0, blas},
            {0.5f, 0.1f, -0.4f, -0.5f + 0.3f * t, 1, 0x2, 0, blas},
            {0.0f, 0.55f, 0.6f, 0.0f, 2, 0x3, 0, blas}};
}

class RefitScene : public RigScene {
protected:
    std::string caps(Kit& kit) override { return rt_caps(kit, false); }

    void init(Rig& rig) override {
        trace_ = make_trace_target(rig, VK_SHADER_STAGE_COMPUTE_BIT);
        query_ = make_query_pipeline(rig, trace_);
    }

    void step(Rig& rig, unsigned frame) override {
        const QuadMesh mesh = make_quad(rig, 0.35f * static_cast<float>(frame));
        meshes_.push_back(mesh);
        const VkAccelerationStructureGeometryKHR g = triangle_geometry(mesh.vertices, 4, mesh.indices);
        build_accel(rig, blas_, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, g, 2, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR, frame != 0);
        tlas_ = build_tlas(rig, scene_instances(blas_.address, static_cast<float>(frame)));
        write_trace(rig, trace_, tlas_);
        image_ = dispatch_query(rig, trace_, query_, 1.0f);
    }

    void grab(Rig&, unsigned, CaptureSet& out) override { out.add("", image_); }

private:
    TraceTarget trace_;
    QueryTrace query_;
    Accel blas_;
    Accel tlas_;
    std::vector<QuadMesh> meshes_;
    Image image_;
};

struct SbtLayout {
    Buffer buffer;
    VkStridedDeviceAddressRegionKHR raygen{};
    VkStridedDeviceAddressRegionKHR miss{};
    VkStridedDeviceAddressRegionKHR hit{};
    VkStridedDeviceAddressRegionKHR callable{};
};

void register_rt() {
    Cases::add(rt_spec("ray_query_compute_masks_shadows", false).same_as({"rt.ray_query_blas_refit_frames"}, "the all mask capture is the scene of the refit row at frame 0; the masked capture is new"), [](Rig& rig, CaptureSet& out) {
        TraceTarget t = make_trace_target(rig, VK_SHADER_STAGE_COMPUTE_BIT);
        const QuadMesh mesh = make_quad(rig, 0.0f);
        Accel blas;
        build_accel(rig, blas, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, triangle_geometry(mesh.vertices, 4, mesh.indices), 2, 0, false);
        const Accel tlas = build_tlas(rig, scene_instances(blas.address, 0.0f));
        write_trace(rig, t, tlas);
        QueryTrace q = make_query_pipeline(rig, t);
        out.add("masked", dispatch_query(rig, t, q, 0.0f));
        out.add("all", dispatch_query(rig, t, q, 1.0f));
    }, [](Kit& kit) { return rt_caps(kit, false); });
    Cases::add(rt_spec("ray_query_fragment_shader", false), [](Rig& rig, CaptureSet& out) {
        TraceTarget t = make_trace_target(rig, VK_SHADER_STAGE_FRAGMENT_BIT);
        const QuadMesh mesh = make_quad(rig, 0.0f);
        Accel blas;
        build_accel(rig, blas, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, triangle_geometry(mesh.vertices, 4, mesh.indices), 2, 0, false);
        const Accel tlas = build_tlas(rig, scene_instances(blas.address, 0.0f));
        write_trace(rig, t, tlas);
        Target& target = rig.make_target(TargetDesc{});
        const VkPipelineLayout layout = rig.playout({t.set_layout});
        PipelineDesc pipe;
        pipe.vertex = rig.sh("full.vert");
        pipe.fragment = rig.sh("rq_frag.v12.frag");
        pipe.layout = layout;
        pipe.pass = target.clear_pass;
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0); }, layout, {t.set}));
    }, [](Kit& kit) { return rt_caps(kit, false); });
    Cases::add_scene(rt_spec("ray_query_blas_refit_frames", false).same_as({"rt.ray_query_compute_masks_shadows"}, "frame 0 is the unrefit scene the compute row traces; later frames refit").frames(4, {0, 2, 3}), [] { return std::make_unique<RefitScene>(); });
    Cases::add(rt_spec("ray_query_blas_compaction", false), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        TraceTarget t = make_trace_target(rig, VK_SHADER_STAGE_COMPUTE_BIT);
        const QuadMesh mesh = make_quad(rig, 0.2f);
        VkQueryPoolCreateInfo qinfo{};
        qinfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qinfo.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
        qinfo.queryCount = 1;
        VkQueryPool pool = VK_NULL_HANDLE;
        rig.vk_ok(kit.api.vkCreateQueryPool(kit.device, &qinfo, nullptr, &pool), "vkCreateQueryPool");
        kit.defer([&kit, pool] { kit.api.vkDestroyQueryPool(kit.device, pool, nullptr); });
        Accel blas;
        build_accel(rig, blas, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, triangle_geometry(mesh.vertices, 4, mesh.indices), 2,
                    VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR, false, pool);
        VkDeviceSize compact_size = 0;
        rig.vk_ok(kit.api.vkGetQueryPoolResults(kit.device, pool, 0, 1, sizeof compact_size, &compact_size, sizeof compact_size, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
                  "vkGetQueryPoolResults");
        rig.must(compact_size != 0, "the compacted size query returned zero");
        Accel compact;
        create_accel(rig, compact, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, compact_size);
        rig.run([&](VkCommandBuffer cb) {
            VkCopyAccelerationStructureInfoKHR copy{};
            copy.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
            copy.src = blas.handle;
            copy.dst = compact.handle;
            copy.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
            kit.api.vkCmdCopyAccelerationStructureKHR(cb, &copy);
        });
        const Accel tlas = build_tlas(rig, scene_instances(compact.address, 0.5f));
        write_trace(rig, t, tlas);
        QueryTrace q = make_query_pipeline(rig, t);
        out.add("", dispatch_query(rig, t, q, 1.0f));
    }, [](Kit& kit) { return rt_caps(kit, false); });
    Cases::add(rt_spec("pipeline_hit_miss_callable_aabb_shadow", true), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        TraceTarget t = make_trace_target(rig, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
        const QuadMesh mesh = make_quad(rig, 0.1f);
        Accel tri_blas;
        build_accel(rig, tri_blas, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, triangle_geometry(mesh.vertices, 4, mesh.indices), 2, 0, false);
        const float aabb[6] = {0.2f, 0.0f, -0.5f, 1.2f, 1.0f, 0.5f};
        const Buffer aabb_buffer = address_buffer(rig, aabb, sizeof aabb, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        VkAccelerationStructureGeometryKHR ag{};
        ag.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        ag.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
        ag.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        ag.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
        ag.geometry.aabbs.data.deviceAddress = aabb_buffer.address;
        ag.geometry.aabbs.stride = 24;
        Accel aabb_blas;
        build_accel(rig, aabb_blas, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, ag, 1, 0, false);
        const Accel tlas = build_tlas(rig, {{-0.5f, -0.3f, 0.0f, 0.4f, 0, 0xFF, 0, tri_blas.address},
                                            {0.1f, 0.5f, 1.0f, 0.0f, 1, 0xFF, 0, tri_blas.address},
                                            {0.0f, 0.0f, 0.0f, 0.0f, 2, 0xFF, 1, aabb_blas.address}});
        write_trace(rig, t, tlas);
        const VkPipelineLayout layout = rig.playout({t.set_layout});
        const char* names[7] = {"rt.v12.rgen", "rt.v12.rmiss", "rt_shadow.v12.rmiss", "rt.v12.rchit", "rt_sphere.v12.rint", "rt_sphere.v12.rchit", "rt.v12.rcall"};
        const VkShaderStageFlagBits stage_bits[7] = {VK_SHADER_STAGE_RAYGEN_BIT_KHR, VK_SHADER_STAGE_MISS_BIT_KHR, VK_SHADER_STAGE_MISS_BIT_KHR,
                                                     VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, VK_SHADER_STAGE_INTERSECTION_BIT_KHR, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
                                                     VK_SHADER_STAGE_CALLABLE_BIT_KHR};
        VkPipelineShaderStageCreateInfo stages[7]{};
        for (int i = 0; i < 7; ++i) {
            stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[i].stage = stage_bits[i];
            stages[i].module = rig.sh(names[i]);
            stages[i].pName = "main";
        }
        VkRayTracingShaderGroupCreateInfoKHR groups[6]{};
        for (VkRayTracingShaderGroupCreateInfoKHR& g : groups) {
            g.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            g.generalShader = g.closestHitShader = g.anyHitShader = g.intersectionShader = VK_SHADER_UNUSED_KHR;
        }
        groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[0].generalShader = 0;
        groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[1].generalShader = 1;
        groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[2].generalShader = 2;
        groups[3].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        groups[3].closestHitShader = 3;
        groups[4].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
        groups[4].closestHitShader = 5;
        groups[4].intersectionShader = 4;
        groups[5].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[5].generalShader = 6;
        VkRayTracingPipelineCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        info.stageCount = 7;
        info.pStages = stages;
        info.groupCount = 6;
        info.pGroups = groups;
        info.maxPipelineRayRecursionDepth = 2;
        info.layout = layout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        rig.vk_ok(kit.api.vkCreateRayTracingPipelinesKHR(kit.device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline), "vkCreateRayTracingPipelinesKHR");
        kit.defer([&kit, pipeline] { kit.api.vkDestroyPipeline(kit.device, pipeline, nullptr); });
        const auto rtp = kit.query_properties<VkPhysicalDeviceRayTracingPipelinePropertiesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR);
        std::vector<std::uint8_t> handles(static_cast<std::size_t>(rtp.shaderGroupHandleSize) * 6);
        rig.vk_ok(kit.api.vkGetRayTracingShaderGroupHandlesKHR(kit.device, pipeline, 0, 6, handles.size(), handles.data()), "vkGetRayTracingShaderGroupHandlesKHR");
        const auto up = [](VkDeviceSize v, VkDeviceSize a) { return (v + a - 1) / a * a; };
        const VkDeviceSize handle = rtp.shaderGroupHandleSize;
        const VkDeviceSize base = rtp.shaderGroupBaseAlignment;
        const VkDeviceSize raygen_stride = up(handle, base);
        const VkDeviceSize miss_stride = up(handle, rtp.shaderGroupHandleAlignment);
        const VkDeviceSize hit_stride = up(handle + 16, rtp.shaderGroupHandleAlignment);
        const VkDeviceSize call_stride = up(handle, rtp.shaderGroupHandleAlignment);
        const VkDeviceSize raygen_off = 0;
        const VkDeviceSize miss_off = up(raygen_off + raygen_stride, base);
        const VkDeviceSize hit_off = up(miss_off + 2 * miss_stride, base);
        const VkDeviceSize call_off = up(hit_off + 2 * hit_stride, base);
        const VkDeviceSize total = call_off + call_stride + base;
        std::vector<std::uint8_t> sbt(static_cast<std::size_t>(total), 0);
        const auto put = [&](VkDeviceSize offset, int group) { std::memcpy(sbt.data() + offset, handles.data() + static_cast<std::size_t>(handle) * static_cast<std::size_t>(group), static_cast<std::size_t>(handle)); };
        put(raygen_off, 0);
        put(miss_off, 1);
        put(miss_off + miss_stride, 2);
        put(hit_off, 3);
        put(hit_off + hit_stride, 4);
        const float record[4] = {0.9f, 0.55f, 0.3f, 1.0f};
        std::memcpy(sbt.data() + hit_off + handle, record, 16);
        put(call_off, 5);
        const Buffer sbt_buffer = rig.blank(total + base, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR, true, true);
        const VkDeviceSize start = up(sbt_buffer.address, base) - sbt_buffer.address;
        std::memcpy(static_cast<std::uint8_t*>(sbt_buffer.mapped) + start, sbt.data(), sbt.size());
        const VkDeviceAddress a = sbt_buffer.address + start;
        const VkStridedDeviceAddressRegionKHR rgen{a + raygen_off, raygen_stride, raygen_stride};
        const VkStridedDeviceAddressRegionKHR miss{a + miss_off, miss_stride, 2 * miss_stride};
        const VkStridedDeviceAddressRegionKHR hit{a + hit_off, hit_stride, 2 * hit_stride};
        const VkStridedDeviceAddressRegionKHR call{a + call_off, call_stride, call_stride};
        rig.run([&](VkCommandBuffer cb) {
            kit.api.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);
            kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, layout, 0, 1, &t.set, 0, nullptr);
            kit.api.vkCmdTraceRaysKHR(cb, &rgen, &miss, &hit, &call, 64, 64, 1);
        });
        out.add("", rig.read(t.image, VK_IMAGE_LAYOUT_GENERAL));
    }, [](Kit& kit) { return rt_caps(kit, true); });
}

struct RtStage {
    const char* name;
    VkShaderStageFlagBits bits;
};

struct RtGroup {
    VkRayTracingShaderGroupTypeKHR type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    int general = -1;
    int closest = -1;
    int any = -1;
    int intersection = -1;
};

struct RtRecord {
    int group = 0;
    std::vector<float> data;
};

struct RtTables {
    VkPipeline pipeline = VK_NULL_HANDLE;
    Buffer buffer;
    VkStridedDeviceAddressRegionKHR raygen{};
    VkStridedDeviceAddressRegionKHR miss{};
    VkStridedDeviceAddressRegionKHR hit{};
    VkStridedDeviceAddressRegionKHR callable{};
};

RtTables build_rt(Rig& rig, VkPipelineLayout layout, const std::vector<RtStage>& stage_list, const std::vector<RtGroup>& group_list, int raygen_group,
                  const std::vector<RtRecord>& misses, const std::vector<RtRecord>& hits, std::uint32_t data_bytes) {
    Kit& kit = rig.kit;
    std::vector<VkPipelineShaderStageCreateInfo> stages(stage_list.size());
    for (std::size_t i = 0; i < stage_list.size(); ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = stage_list[i].bits;
        stages[i].module = rig.sh(stage_list[i].name);
        stages[i].pName = "main";
    }
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups(group_list.size());
    const auto unused = [](int v) { return v < 0 ? VK_SHADER_UNUSED_KHR : static_cast<std::uint32_t>(v); };
    for (std::size_t i = 0; i < group_list.size(); ++i) {
        groups[i].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        groups[i].type = group_list[i].type;
        groups[i].generalShader = unused(group_list[i].general);
        groups[i].closestHitShader = unused(group_list[i].closest);
        groups[i].anyHitShader = unused(group_list[i].any);
        groups[i].intersectionShader = unused(group_list[i].intersection);
    }
    VkRayTracingPipelineCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    info.stageCount = static_cast<std::uint32_t>(stages.size());
    info.pStages = stages.data();
    info.groupCount = static_cast<std::uint32_t>(groups.size());
    info.pGroups = groups.data();
    info.maxPipelineRayRecursionDepth = 1;
    info.layout = layout;
    RtTables out;
    rig.vk_ok(kit.api.vkCreateRayTracingPipelinesKHR(kit.device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &info, nullptr, &out.pipeline), "vkCreateRayTracingPipelinesKHR");
    const VkPipeline pipeline = out.pipeline;
    kit.defer([&kit, pipeline] { kit.api.vkDestroyPipeline(kit.device, pipeline, nullptr); });
    const auto rtp = kit.query_properties<VkPhysicalDeviceRayTracingPipelinePropertiesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR);
    const VkDeviceSize handle = rtp.shaderGroupHandleSize;
    std::vector<std::uint8_t> handles(static_cast<std::size_t>(handle) * groups.size());
    rig.vk_ok(kit.api.vkGetRayTracingShaderGroupHandlesKHR(kit.device, pipeline, 0, static_cast<std::uint32_t>(groups.size()), handles.size(), handles.data()),
              "vkGetRayTracingShaderGroupHandlesKHR");
    const auto up = [](VkDeviceSize v, VkDeviceSize a) { return (v + a - 1) / a * a; };
    const VkDeviceSize base = rtp.shaderGroupBaseAlignment;
    const VkDeviceSize stride = up(handle + data_bytes, rtp.shaderGroupHandleAlignment);
    const VkDeviceSize raygen_stride = up(handle, base);
    const VkDeviceSize raygen_off = 0;
    const VkDeviceSize miss_off = up(raygen_off + raygen_stride, base);
    const VkDeviceSize hit_off = up(miss_off + stride * misses.size(), base);
    const VkDeviceSize total = hit_off + stride * hits.size();
    std::vector<std::uint8_t> sbt(static_cast<std::size_t>(total), 0);
    const auto put = [&](VkDeviceSize offset, const RtRecord& record) {
        std::memcpy(sbt.data() + offset, handles.data() + static_cast<std::size_t>(handle) * static_cast<std::size_t>(record.group), static_cast<std::size_t>(handle));
        std::memcpy(sbt.data() + offset + handle, record.data.data(), record.data.size() * sizeof(float));
    };
    put(raygen_off, RtRecord{raygen_group, {}});
    for (std::size_t i = 0; i < misses.size(); ++i) {
        put(miss_off + stride * i, misses[i]);
    }
    for (std::size_t i = 0; i < hits.size(); ++i) {
        put(hit_off + stride * i, hits[i]);
    }
    out.buffer = rig.blank(total + base, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR, true, true);
    const VkDeviceSize start = up(out.buffer.address, base) - out.buffer.address;
    std::memcpy(static_cast<std::uint8_t*>(out.buffer.mapped) + start, sbt.data(), sbt.size());
    const VkDeviceAddress a = out.buffer.address + start;
    out.raygen = {a + raygen_off, raygen_stride, raygen_stride};
    out.miss = {a + miss_off, stride, stride * misses.size()};
    out.hit = {a + hit_off, stride, stride * hits.size()};
    return out;
}

void trace(Rig& rig, const RtTables& tables, VkPipelineLayout layout, const TraceTarget& t, const void* push, std::uint32_t push_size) {
    Kit& kit = rig.kit;
    rig.run([&](VkCommandBuffer cb) {
        kit.api.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, tables.pipeline);
        kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, layout, 0, 1, &t.set, 0, nullptr);
        if (push_size != 0) {
            rig.push(cb, layout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, push, push_size);
        }
        kit.api.vkCmdTraceRaysKHR(cb, &tables.raygen, &tables.miss, &tables.hit, &tables.callable, 64, 64, 1);
    });
}

QuadMesh make_scaled_quad(Rig& rig, float scale) {
    const float v[12] = {-0.5f * scale, -0.5f * scale, 0.0f, 0.5f * scale, -0.5f * scale, 0.0f, 0.5f * scale, 0.5f * scale, 0.0f, -0.5f * scale, 0.5f * scale, 0.0f};
    const std::uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
    QuadMesh m;
    m.vertices = address_buffer(rig, v, sizeof v, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    m.indices = address_buffer(rig, idx, sizeof idx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    return m;
}

void register_rt_records() {
    Cases::add(rt_spec("pipeline_any_hit_alpha_test", true), [](Rig& rig, CaptureSet& out) {
        TraceTarget t = make_trace_target(rig, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
        const QuadMesh mesh = make_scaled_quad(rig, 3.6f);
        VkAccelerationStructureGeometryKHR front_geometry = triangle_geometry(mesh.vertices, 4, mesh.indices);
        front_geometry.flags = 0;
        Accel front;
        build_accel(rig, front, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, front_geometry, 2, 0, false);
        Accel back;
        build_accel(rig, back, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, triangle_geometry(mesh.vertices, 4, mesh.indices), 2, 0, false);
        const Accel tlas = build_tlas(rig, {{0.0f, 0.0f, 0.3f, 0.0f, 0, 0xFF, 0, front.address}, {0.0f, 0.0f, -0.5f, 0.0f, 1, 0xFF, 0, back.address}});
        write_trace(rig, t, tlas);
        const VkPipelineLayout layout = rig.playout({t.set_layout}, 4, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
        RtGroup rgen;
        rgen.general = 0;
        RtGroup rmiss;
        rmiss.general = 1;
        RtGroup hit;
        hit.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        hit.closest = 2;
        hit.any = 3;
        const RtTables tables = build_rt(rig, layout, {{"rt_ah.v12.rgen", VK_SHADER_STAGE_RAYGEN_BIT_KHR}, {"rt.v12.rmiss", VK_SHADER_STAGE_MISS_BIT_KHR},
                                                       {"rt_ah.v12.rchit", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR}, {"rt_ah.v12.rahit", VK_SHADER_STAGE_ANY_HIT_BIT_KHR}},
                                         {rgen, rmiss, hit}, 0, {RtRecord{1, {}}}, {RtRecord{2, {}}}, 0);
        const float alpha_test = 0.0f;
        trace(rig, tables, layout, t, &alpha_test, 4);
        out.add("alpha_test", rig.read(t.image, VK_IMAGE_LAYOUT_GENERAL));
        const float opaque = 1.0f;
        trace(rig, tables, layout, t, &opaque, 4);
        out.add("opaque_ray_flag", rig.read(t.image, VK_IMAGE_LAYOUT_GENERAL));
    }, [](Kit& kit) { return rt_caps(kit, true); });
    Cases::add(rt_spec("pipeline_sbt_record_data_per_geometry", true), [](Rig& rig, CaptureSet& out) {
        TraceTarget t = make_trace_target(rig, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
        const float v[24] = {-0.45f, -0.4f, 0, -0.05f, -0.4f, 0, -0.05f, 0.4f, 0, -0.45f, 0.4f, 0, 0.05f, -0.4f, 0, 0.45f, -0.4f, 0, 0.45f, 0.4f, 0, 0.05f, 0.4f, 0};
        const std::uint32_t idx[12] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
        const Buffer vertices = address_buffer(rig, v, sizeof v, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        const Buffer indices = address_buffer(rig, idx, sizeof idx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        VkAccelerationStructureGeometryKHR first = triangle_geometry(vertices, 8, indices);
        VkAccelerationStructureGeometryKHR second = first;
        second.geometry.triangles.indexData.deviceAddress = indices.address + 24;
        Accel blas;
        build_accel_geoms(rig, blas, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, {first, second}, {2, 2}, 0, false);
        const Accel tlas = build_tlas(rig, {{-1.0f, 0.7f, 0.0f, 0.2f, 0, 0xFF, 0, blas.address},
                                            {0.2f, -0.2f, 0.3f, -0.3f, 1, 0xFF, 2, blas.address},
                                            {-0.4f, -0.95f, -0.2f, 0.1f, 2, 0xFF, 4, blas.address}});
        write_trace(rig, t, tlas);
        const VkPipelineLayout layout = rig.playout({t.set_layout});
        RtGroup rgen;
        rgen.general = 0;
        RtGroup rmiss;
        rmiss.general = 1;
        RtGroup hit;
        hit.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        hit.closest = 2;
        std::vector<RtRecord> records;
        for (int i = 0; i < 6; ++i) {
            const float* c = palette(static_cast<unsigned>(i));
            records.push_back({2, {c[0], c[1], c[2], 1.0f, 0.45f + 0.08f * static_cast<float>(i), 0.3f - 0.04f * static_cast<float>(i), 0.15f + 0.05f * static_cast<float>(i), 0.0f}});
        }
        const RtTables tables = build_rt(rig, layout, {{"rt_sbt.v12.rgen", VK_SHADER_STAGE_RAYGEN_BIT_KHR}, {"rt_sbt.v12.rmiss", VK_SHADER_STAGE_MISS_BIT_KHR},
                                                       {"rt_sbt.v12.rchit", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR}},
                                         {rgen, rmiss, hit}, 0, {RtRecord{1, {0.15f, 0.25f, 0.45f, 1.0f}}}, records, 32);
        trace(rig, tables, layout, t, nullptr, 0);
        out.add("", rig.read(t.image, VK_IMAGE_LAYOUT_GENERAL));
    }, [](Kit& kit) { return rt_caps(kit, true); });
}

void register_rt_scene() {
    CaseSpec spec = sk::scene_spec("rt_shadow_hybrid_ray_query", 160);
    spec.min_version(1, 2).extension("VK_KHR_acceleration_structure").extension("VK_KHR_deferred_host_operations").extension("VK_KHR_ray_query");
    Cases::add(
        std::move(spec),
        [](Rig& rig, CaptureSet& out) {
            using namespace sk;
            Kit& kit = rig.kit;
            TargetDesc td;
            td.width = td.height = 160;
            td.depth = VK_FORMAT_D32_SFLOAT;
            Target& target = rig.make_target(td);
            target.clear_color[0] = 0.05f;
            target.clear_color[1] = 0.06f;
            target.clear_color[2] = 0.1f;
            Geometry g;
            g.floor(0.0f, 6.0f, {0.6f, 0.62f, 0.58f});
            g.box({-1.6f, 0.9f, -0.8f}, {0.9f, 0.9f, 0.9f}, {0.9f, 0.45f, 0.3f}, 0.5f);
            g.box({1.4f, 0.6f, 0.9f}, {0.6f, 0.6f, 0.6f}, {0.3f, 0.55f, 0.9f}, -0.4f);
            g.box({0.2f, 0.25f, -2.6f}, {1.6f, 0.25f, 0.6f}, {0.9f, 0.85f, 0.3f}, 0.1f);
            g.sphere({0.3f, 0.7f, 2.0f}, 0.7f, {0.85f, 0.35f, 0.75f});
            g.quad({-6, 0, -3.5}, {-6, 4, -3.5}, {6, 4, -3.5}, {6, 0, -3.5}, {0, 0, 1}, {0.75f, 0.7f, 0.65f});
            const Gpu gpu = upload(rig, g);
            const Buffer vb = address_buffer(rig, g.v.data(), g.v.size() * sizeof(SVertex), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            const Buffer ib = address_buffer(rig, g.i.data(), g.i.size() * sizeof(std::uint16_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            VkAccelerationStructureGeometryKHR geometry{};
            geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
            geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            geometry.geometry.triangles.vertexData.deviceAddress = vb.address;
            geometry.geometry.triangles.vertexStride = sizeof(SVertex);
            geometry.geometry.triangles.maxVertex = static_cast<std::uint32_t>(g.v.size() - 1);
            geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT16;
            geometry.geometry.triangles.indexData.deviceAddress = ib.address;
            Accel blas;
            build_accel(rig, blas, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geometry, static_cast<std::uint32_t>(g.i.size() / 3), 0, false);
            const Accel tlas = build_tlas(rig, {{0.0f, 0.0f, 0.0f, 0.0f, 0, 0xFF, 0, blas.address}});
            const VkDescriptorSetLayout set_layout = rig.dlayout({binding(0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_FRAGMENT_BIT)});
            const VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1};
            VkDescriptorPoolCreateInfo pool_info{};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.maxSets = 1;
            pool_info.poolSizeCount = 1;
            pool_info.pPoolSizes = &pool_size;
            VkDescriptorPool pool = VK_NULL_HANDLE;
            rig.vk_ok(kit.api.vkCreateDescriptorPool(kit.device, &pool_info, nullptr, &pool), "vkCreateDescriptorPool");
            kit.defer([&kit, pool] { kit.api.vkDestroyDescriptorPool(kit.device, pool, nullptr); });
            VkDescriptorSetAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc.descriptorPool = pool;
            alloc.descriptorSetCount = 1;
            alloc.pSetLayouts = &set_layout;
            VkDescriptorSet set = VK_NULL_HANDLE;
            rig.vk_ok(kit.api.vkAllocateDescriptorSets(kit.device, &alloc, &set), "vkAllocateDescriptorSets");
            VkWriteDescriptorSetAccelerationStructureKHR as{};
            as.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            as.accelerationStructureCount = 1;
            as.pAccelerationStructures = &tlas.handle;
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.pNext = &as;
            write.dstSet = set;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            kit.api.vkUpdateDescriptorSets(kit.device, 1, &write, 0, nullptr);
            const VkPipelineLayout layout = rig.playout({set_layout}, 96, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
            PipelineDesc pipe;
            pipe.vertex = rig.sh("rtshadow.vert");
            pipe.fragment = rig.sh("rtshadow.v12.frag");
            svertex_input(pipe);
            pipe.layout = layout;
            pipe.pass = target.clear_pass;
            pipe.depth_test = true;
            const VkPipeline pipeline = rig.pipe(pipe);
            const Mat4 vp = Mat4::perspective(1.1f, 1.0f, 0.3f, 40.0f, true) * look_at({4.2f, 3.4f, 6.0f}, {0.0f, 0.5f, -0.3f});
            float push[24];
            std::memcpy(push, vp.m, 64);
            push[16] = push[17] = push[18] = push[19] = 0.0f;
            push[20] = -3.4f;
            push[21] = 3.6f;
            push[22] = 2.2f;
            push[23] = 1.0f;
            out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
                rig.push(cb, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, push, 96);
                draw(rig, cb, gpu);
            }, layout, {set}));
        },
        [](Kit& kit) { return rt_caps(kit, false); });
}

const int registered = (register_rt(), register_rt_records(), register_rt_scene(), 0);

}  // namespace

}  // namespace lsw::render::vk
