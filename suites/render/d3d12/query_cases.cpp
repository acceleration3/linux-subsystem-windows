#include "render/d3d12/compute.hpp"

#include <cmath>
#include <cstring>

namespace lsw::suites::render::d3d12 {

namespace {

constexpr UINT kObjects = 4;

struct OcclusionScene {
    Stage stage;
    ID3D12PipelineState* opaque = nullptr;
    ID3D12PipelineState* proxy = nullptr;
    ID3D12QueryHeap* heap = nullptr;
    ID3D12Resource* predicates[3] = {nullptr, nullptr, nullptr};
    D3D12_RESOURCE_STATES predicate_state[3] = {D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON};
    ID3D12Resource* readback = nullptr;
    bool visible[kObjects] = {true, true, true, true};

    bool open(Kit& kit, std::string& error, bool with_predicates) {
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
            return false;
        }
        Pso first = stage.pso();
        Pso second = stage.pso();
        second.desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
        second.desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        opaque = first.build(error);
        proxy = second.build(error);
        D3D12_QUERY_HEAP_DESC desc{D3D12_QUERY_HEAP_TYPE_OCCLUSION, kObjects, 0};
        Com<ID3D12QueryHeap> created;
        const HRESULT result = kit.device()->CreateQueryHeap(&desc, __uuidof(ID3D12QueryHeap), created.put_void());
        if (opaque == nullptr || proxy == nullptr || FAILED(result)) {
            return fail_with(error, error.empty() ? "CreateQueryHeap: " + D3d12Tools::hr(result) : error);
        }
        heap = kit.rig.hold(std::move(created));
        if (with_predicates) {
            const std::uint64_t ones[kObjects] = {1, 1, 1, 1};
            for (auto& buffer : predicates) {
                HRESULT created_result = S_OK;
                buffer = D3d12Tools::device_buffer(kit.rig, 64, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_NONE, &created_result);
                if (buffer == nullptr || !D3d12Tools::write_buffer(kit.rig, buffer, D3D12_RESOURCE_STATE_COMMON, 0, ones, sizeof ones, error)) {
                    return fail_with(error, error.empty() ? "predicate buffer: " + D3d12Tools::hr(created_result) : error);
                }
            }
        } else {
            HRESULT created_result = S_OK;
            readback = D3d12Tools::readback(kit.rig, 64, &created_result);
            if (readback == nullptr) {
                return fail_with(error, "readback: " + D3d12Tools::hr(created_result));
            }
        }
        return true;
    }

    static float wall_x(unsigned frame) { return -0.75f + 0.5f * static_cast<float>(frame % 4); }

    rr::Mat4 object_matrix(UINT index) const {
        return rr::Mat4::translate(-0.75f + 0.5f * static_cast<float>(index), 0.0f, 0.0f) * rr::Mat4::scale(0.25f, 0.25f, 0.25f);
    }

    void record_wall(ID3D12GraphicsCommandList* list, unsigned frame) {
        list->SetPipelineState(opaque);
        stage.mesh(list, stage.plane, rr::Mat4::translate(wall_x(frame), 0.0f, 0.6f) * rr::Mat4::scale(0.28f, 0.9f, 1.0f), 0.55f, 0.55f, 0.6f);
    }

    void record_queries(ID3D12GraphicsCommandList* list) {
        list->SetPipelineState(proxy);
        for (UINT i = 0; i < kObjects; ++i) {
            list->BeginQuery(heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, i);
            stage.mesh(list, stage.sphere, object_matrix(i), 1, 1, 1);
            list->EndQuery(heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, i);
        }
    }
};

void register_all() {
    Cases::frames(rr::CaseSpec("query", "occlusion_culling_predication_ring").frames(8, {0, 3, 5, 7}).tolerance(rr::Tolerance::blend(3).with_edge(1)).tag("query"), [](Cases::Steps& steps) {
        auto scene = std::make_shared<OcclusionScene>();
        steps.on_setup = [scene](Kit& kit, std::string& error) { return scene->open(kit, error, true); };
        steps.on_frame = [scene](Kit& kit, unsigned frame, std::string& error) {
            ID3D12Resource* current = scene->predicates[frame % 3];
            ID3D12Resource* previous = scene->predicates[(frame + 2) % 3];
            const D3D12_RESOURCE_STATES current_before = scene->predicate_state[frame % 3];
            scene->predicate_state[frame % 3] = D3D12_RESOURCE_STATE_PREDICATION;
            const D3D12_RESOURCE_STATES previous_state = scene->predicate_state[(frame + 2) % 3];
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                scene->stage.canvas.begin(list, clear);
                scene->record_wall(list, frame);
                scene->record_queries(list);
                D3d12Tools::move(list, current, current_before, D3D12_RESOURCE_STATE_COPY_DEST);
                list->ResolveQueryData(scene->heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0, kObjects, current, 0);
                D3d12Tools::move(list, current, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PREDICATION);
                if (frame > 0) {
                    (void)previous_state;
                }
                list->SetPipelineState(scene->opaque);
                for (UINT i = 0; i < kObjects; ++i) {
                    if (frame > 0) {
                        list->SetPredication(previous, i * 8, D3D12_PREDICATION_OP_EQUAL_ZERO);
                    }
                    scene->stage.mesh(list, scene->stage.sphere, scene->object_matrix(i), 0.9f - 0.15f * static_cast<float>(i), 0.4f + 0.15f * static_cast<float>(i), 0.3f);
                }
                list->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
            }, error);
        };
        steps.on_capture = [scene](Kit& kit, unsigned, rr::CaptureSet& out, std::string& error) { return scene->stage.finish(out, error); };
    });
    Cases::frames(rr::CaseSpec("query", "occlusion_result_read_on_cpu_drives_next_frame").frames(8, {1, 4, 7}).tolerance(rr::Tolerance::blend(3).with_edge(1)).tag("query"), [](Cases::Steps& steps) {
        auto scene = std::make_shared<OcclusionScene>();
        steps.on_setup = [scene](Kit& kit, std::string& error) { return scene->open(kit, error, false); };
        steps.on_frame = [scene](Kit& kit, unsigned frame, std::string& error) {
            if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                    const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                    scene->stage.canvas.begin(list, clear);
                    scene->record_wall(list, frame);
                    scene->record_queries(list);
                    list->ResolveQueryData(scene->heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0, kObjects, scene->readback, 0);
                    list->SetPipelineState(scene->opaque);
                    for (UINT i = 0; i < kObjects; ++i) {
                        if (scene->visible[i]) {
                            scene->stage.mesh(list, scene->stage.sphere, scene->object_matrix(i), 0.9f - 0.15f * static_cast<float>(i), 0.4f + 0.15f * static_cast<float>(i), 0.3f);
                        } else {
                            scene->stage.mesh(list, scene->stage.cube, scene->object_matrix(i), 0.8f, 0.2f, 0.2f);
                        }
                    }
                }, error)) {
                return false;
            }
            Kit::Bytes bytes;
            if (!D3d12Tools::map_bytes(scene->readback, 0, kObjects * 8, bytes, error)) {
                return false;
            }
            for (UINT i = 0; i < kObjects; ++i) {
                std::uint64_t value = 0;
                std::memcpy(&value, bytes.data() + i * 8, 8);
                scene->visible[i] = value != 0;
            }
            return true;
        };
        steps.on_capture = [scene](Kit& kit, unsigned, rr::CaptureSet& out, std::string& error) { return scene->stage.finish(out, error); };
    });
    Cases::single(rr::CaseSpec("query", "predication_skips_draw_copy_and_dispatch").tolerance(rr::Tolerance::exact()).tag("query"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Cs cs;
        if (!cs.open(kit, "RWStructuredBuffer<uint> dst : register(u0);\n[numthreads(16, 1, 1)] void main(uint3 id : SV_DispatchThreadID) { dst[id.x] = id.x * 7 + 1; }\n", error)) {
            return false;
        }
        HRESULT result = S_OK;
        const std::uint64_t flags[2] = {0, 1};
        ID3D12Resource* predicate = D3d12Tools::device_buffer(kit.rig, 16, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_NONE, &result);
        ID3D12Resource* target = D3d12Tools::device_buffer(kit.rig, 1024, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &result);
        std::vector<std::uint32_t> source_data(256, 0xaaaaaaaau);
        ID3D12Resource* source = kit.make_upload(source_data.data(), 1024, error);
        if (predicate == nullptr || target == nullptr || source == nullptr || !D3d12Tools::write_buffer(kit.rig, predicate, D3D12_RESOURCE_STATE_COMMON, 0, flags, 16, error)) {
            return fail_with(error, error.empty() ? "buffer: " + D3d12Tools::hr(result) : error);
        }
        const std::vector<std::uint32_t> zeros(256, 0);
        if (!D3d12Tools::write_buffer(kit.rig, target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, zeros.data(), 1024, error)) {
            return false;
        }
        cs.binder.uav_buffer(0, target, DXGI_FORMAT_UNKNOWN, 0, 256, 4);
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                D3d12Tools::move(list, predicate, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PREDICATION);
                cs.bind(list);
                list->SetPredication(predicate, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
                list->Dispatch(1, 1, 1);
                list->SetPredication(predicate, 8, D3D12_PREDICATION_OP_EQUAL_ZERO);
                list->Dispatch(2, 1, 1);
                D3d12Tools::move(list, target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
                list->SetPredication(predicate, 0, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
                list->CopyBufferRegion(target, 512, source, 0, 128);
                list->SetPredication(predicate, 8, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
                list->CopyBufferRegion(target, 768, source, 0, 128);
                list->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
                D3d12Tools::move(list, target, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }, error)) {
            return false;
        }
        std::vector<std::uint32_t> words;
        if (!D3d12Tools::read_words(kit.rig, target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 256, words, error)) {
            return false;
        }
        rr::Image image(8, 8, rr::PixelKind::Rgba32Uint);
        std::memcpy(image.bytes().data(), words.data(), 1024);
        out.add("", std::move(image));
        return true;
    });
    Cases::single(rr::CaseSpec("query", "pipeline_statistics_query_does_not_change_image").tolerance(rr::Tolerance::blend(2)).tag("query"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
            return false;
        }
        D3D12_QUERY_HEAP_DESC desc{D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS, 1, 0};
        Com<ID3D12QueryHeap> created;
        const HRESULT result = kit.device()->CreateQueryHeap(&desc, __uuidof(ID3D12QueryHeap), created.put_void());
        if (FAILED(result)) {
            return fail_with(error, "CreateQueryHeap (pipeline statistics): " + D3d12Tools::hr(result));
        }
        ID3D12QueryHeap* heap = kit.rig.hold(std::move(created));
        HRESULT readback_result = S_OK;
        ID3D12Resource* readback = D3d12Tools::readback(kit.rig, 256, &readback_result);
        Pso builder = stage.pso();
        ID3D12PipelineState* state = builder.build(error);
        if (readback == nullptr || state == nullptr) {
            return false;
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                stage.canvas.begin(list, clear);
                list->SetPipelineState(state);
                list->BeginQuery(heap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
                stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.0f), 0.9f, 0.5f, 0.2f);
                stage.mesh(list, stage.sphere, rr::Mat4::translate(0.1f, 0, 0), 0.2f, 0.7f, 0.9f);
                list->EndQuery(heap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
                list->ResolveQueryData(heap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, 1, readback, 0);
            },
            error);
        return ok && stage.finish(out, error);
    });
}

const struct Registrar {
    Registrar() { register_all(); }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
