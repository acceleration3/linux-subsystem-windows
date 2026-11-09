#include "render/d3d12/textures.hpp"

#include <cstring>

namespace lsw::suites::render::d3d12 {

namespace {

struct PlacedTarget {
    ID3D12Resource* resource = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE view{};
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_RENDER_TARGET;
};

ID3D12Heap* make_heap(Kit& kit, UINT64 size, D3D12_HEAP_FLAGS flags, std::string& error, D3D12_HEAP_TYPE type = D3D12_HEAP_TYPE_DEFAULT) {
    D3D12_HEAP_DESC desc{};
    desc.SizeInBytes = size;
    desc.Properties = D3d12Tools::heap_props(type);
    desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    desc.Flags = flags;
    Com<ID3D12Heap> heap;
    const HRESULT result = kit.device()->CreateHeap(&desc, __uuidof(ID3D12Heap), heap.put_void());
    if (FAILED(result)) {
        error = "CreateHeap: " + D3d12Tools::hr(result);
        return nullptr;
    }
    return kit.rig.hold(std::move(heap));
}

bool make_placed_target(Kit& kit, ID3D12Heap* heap, UINT64 offset, PlacedTarget& out, std::string& error) {
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    const D3D12_RESOURCE_DESC desc = D3d12Tools::texture_desc(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    Com<ID3D12Resource> resource;
    const HRESULT result = kit.device()->CreatePlacedResource(heap, offset, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, __uuidof(ID3D12Resource), resource.put_void());
    if (FAILED(result)) {
        return fail_with(error, "CreatePlacedResource: " + D3d12Tools::hr(result));
    }
    out.resource = kit.rig.hold(std::move(resource));
    HRESULT heap_result = S_OK;
    out.heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false, &heap_result);
    if (out.heap == nullptr) {
        return fail_with(error, "RTV heap: " + D3d12Tools::hr(heap_result));
    }
    out.view = out.heap->GetCPUDescriptorHandleForHeapStart();
    kit.device()->CreateRenderTargetView(out.resource, nullptr, out.view);
    return true;
}

void begin_pass(Stage& stage, ID3D12GraphicsCommandList* list, PlacedTarget& target, const float clear[4]) {
    list->OMSetRenderTargets(1, &target.view, FALSE, nullptr);
    list->ClearRenderTargetView(target.view, clear, 0, nullptr);
    stage.canvas.set_viewport(list);
    stage.canvas.binder.bind_graphics(list);
}

void register_placed() {
    Cases::single(rr::CaseSpec("heap", "placed_aliased_targets_switch").tolerance(rr::Tolerance::blend(2)).tag("heap"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error)) {
            return false;
        }
        ID3D12Heap* heap = make_heap(kit, 1 << 20, D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES, error);
        if (heap == nullptr) {
            return false;
        }
        PlacedTarget a;
        PlacedTarget b;
        if (!make_placed_target(kit, heap, 0, a, error) || !make_placed_target(kit, heap, 0, b, error)) {
            return false;
        }
        Pso builder = stage.pso();
        builder.desc.DepthStencilState.DepthEnable = FALSE;
        builder.desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        ID3D12PipelineState* state = builder.build(error);
        if (state == nullptr) {
            return false;
        }
        const float clear_a[4] = {0.1f, 0.1f, 0.2f, 1};
        const float clear_b[4] = {0.2f, 0.12f, 0.08f, 1};
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                begin_pass(stage, list, a, clear_a);
                list->SetPipelineState(state);
                stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.0f), 0.9f, 0.5f, 0.2f);
            }, error)) {
            return false;
        }
        rr::Image first;
        if (!kit.read_image(a.resource, D3D12_RESOURCE_STATE_RENDER_TARGET, 0, DXGI_FORMAT_R8G8B8A8_UNORM, first, error)) {
            return false;
        }
        out.add("a", std::move(first));
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                const D3D12_RESOURCE_BARRIER barrier = D3d12Tools::aliasing_barrier(a.resource, b.resource);
                list->ResourceBarrier(1, &barrier);
                begin_pass(stage, list, b, clear_b);
                list->SetPipelineState(state);
                stage.mesh(list, stage.sphere, rr::Mat4::translate(0.1f, 0.0f, 0), 0.3f, 0.8f, 0.9f);
            }, error)) {
            return false;
        }
        rr::Image second;
        if (!kit.read_image(b.resource, D3D12_RESOURCE_STATE_RENDER_TARGET, 0, DXGI_FORMAT_R8G8B8A8_UNORM, second, error)) {
            return false;
        }
        out.add("b", std::move(second));
        return true;
    });
    Cases::single(rr::CaseSpec("heap", "placed_buffers_suballocated_geometry").tolerance(rr::Tolerance::blend(2)).tag("heap"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
            return false;
        }
        ID3D12Heap* heap = make_heap(kit, 1 << 20, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, error);
        if (heap == nullptr) {
            return false;
        }
        const MeshData mesh = Shapes::torus(24, 12, 0.85f, 0.3f);
        std::vector<std::uint16_t> indices(mesh.indices.begin(), mesh.indices.end());
        const UINT64 vertex_bytes = mesh.vertices.size() * sizeof(Vertex);
        const UINT64 index_bytes = indices.size() * 2;
        ID3D12Resource* vertex_upload = kit.make_upload(mesh.vertices.data(), vertex_bytes, error);
        ID3D12Resource* index_upload = kit.make_upload(indices.data(), index_bytes, error);
        if (vertex_upload == nullptr || index_upload == nullptr) {
            return false;
        }
        Com<ID3D12Resource> vertex;
        Com<ID3D12Resource> index;
        const D3D12_RESOURCE_DESC vertex_desc = D3d12Tools::buffer_desc(vertex_bytes);
        const D3D12_RESOURCE_DESC index_desc = D3d12Tools::buffer_desc(index_bytes);
        HRESULT result = kit.device()->CreatePlacedResource(heap, 0, &vertex_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), vertex.put_void());
        if (SUCCEEDED(result)) {
            result = kit.device()->CreatePlacedResource(heap, 3 * 65536, &index_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), index.put_void());
        }
        if (FAILED(result)) {
            return fail_with(error, "CreatePlacedResource (buffers): " + D3d12Tools::hr(result));
        }
        ID3D12Resource* vb = kit.rig.hold(std::move(vertex));
        ID3D12Resource* ib = kit.rig.hold(std::move(index));
        Pso builder = stage.pso();
        ID3D12PipelineState* state = builder.build(error);
        if (state == nullptr) {
            return false;
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                list->CopyBufferRegion(vb, 0, vertex_upload, 0, vertex_bytes);
                list->CopyBufferRegion(ib, 0, index_upload, 0, index_bytes);
                D3d12Tools::move(list, vb, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
                D3d12Tools::move(list, ib, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                stage.canvas.begin(list, clear);
                list->SetPipelineState(state);
                const D3D12_VERTEX_BUFFER_VIEW vbv{vb->GetGPUVirtualAddress(), static_cast<UINT>(vertex_bytes), sizeof(Vertex)};
                const D3D12_INDEX_BUFFER_VIEW ibv{ib->GetGPUVirtualAddress(), static_cast<UINT>(index_bytes), DXGI_FORMAT_R16_UINT};
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                list->IASetVertexBuffers(0, 1, &vbv);
                list->IASetIndexBuffer(&ibv);
                const float color[4] = {0.9f, 0.6f, 0.2f, 1};
                set_cbv_root(list, stage.view_projection() * rr::Mat4::rotate_x(0.9f), color);
                list->DrawIndexedInstanced(static_cast<UINT>(indices.size()), 1, 0, 0, 0);
            },
            error);
        return ok && stage.finish(out, error);
    });
    Cases::single(
        rr::CaseSpec("heap", "heap_tier2_mixed_buffers_and_textures").tolerance(rr::Tolerance::blend(2)).tag("heap"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            Stage stage;
            if (!stage.open(kit, error)) {
                return false;
            }
            ID3D12Heap* heap = make_heap(kit, 4 << 20, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, error);
            if (heap == nullptr) {
                return false;
            }
            PlacedTarget target;
            if (!make_placed_target(kit, heap, 0, target, error)) {
                return false;
            }
            const D3D12_RESOURCE_DESC texture_desc = D3d12Tools::texture_desc(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM);
            Com<ID3D12Resource> texture_com;
            HRESULT result = kit.device()->CreatePlacedResource(heap, 2 << 20, &texture_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), texture_com.put_void());
            if (FAILED(result)) {
                return fail_with(error, "CreatePlacedResource (texture): " + D3d12Tools::hr(result));
            }
            ID3D12Resource* texture = kit.rig.hold(std::move(texture_com));
            const Texels::Bytes pixels = Texels::content(0, 32, 32);
            if (!D3d12Tools::write_texture(kit.rig, texture, D3D12_RESOURCE_STATE_COPY_DEST, 0, pixels.data(), error)) {
                return false;
            }
            stage.canvas.binder.srv_texture(0, texture, DXGI_FORMAT_R8G8B8A8_UNORM);
            Pso builder = stage.pso(Hlsl12::kTexPs);
            builder.desc.DepthStencilState.DepthEnable = FALSE;
            builder.desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
            ID3D12PipelineState* state = builder.build(error);
            if (state == nullptr) {
                return false;
            }
            const bool ok = kit.run(
                [&](ID3D12GraphicsCommandList* list) {
                    const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                    begin_pass(stage, list, target, clear);
                    list->SetPipelineState(state);
                    const float color[4] = {1, 1, 1, 1};
                    const float scale[4] = {3, 3, 0, 0};
                    set_cbv_root(list, stage.view_projection() * rr::Mat4::rotate_y(0.5f), color, scale);
                    stage.sphere.bind(list);
                    stage.sphere.draw(list);
                },
                error);
            rr::Image image;
            if (!ok || !kit.read_image(target.resource, D3D12_RESOURCE_STATE_RENDER_TARGET, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
                return false;
            }
            out.add("", std::move(image));
            return true;
        },
        [](Kit& kit) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
            const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS, options);
            return SUCCEEDED(result) && options.ResourceHeapTier >= D3D12_RESOURCE_HEAP_TIER_2 ? std::string() : std::string("resource heap tier 2");
        });
    Cases::single(rr::CaseSpec("heap", "evict_and_make_resident_before_use").tolerance(rr::Tolerance::blend(2)).tag("heap"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error)) {
            return false;
        }
        ID3D12Heap* heap = make_heap(kit, 1 << 20, D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES, error);
        PlacedTarget target;
        if (heap == nullptr || !make_placed_target(kit, heap, 0, target, error)) {
            return false;
        }
        ID3D12Pageable* pageable = heap;
        HRESULT result = kit.device()->Evict(1, &pageable);
        if (FAILED(result)) {
            return fail_with(error, "Evict: " + D3d12Tools::hr(result));
        }
        Com<ID3D12Device1> device1 = kit.rig.as<ID3D12Device1>();
        if (device1) {
            const D3D12_RESIDENCY_PRIORITY priority = D3D12_RESIDENCY_PRIORITY_HIGH;
            device1->SetResidencyPriority(1, &pageable, &priority);
        }
        result = kit.device()->MakeResident(1, &pageable);
        if (FAILED(result)) {
            return fail_with(error, "MakeResident: " + D3d12Tools::hr(result));
        }
        Pso builder = stage.pso();
        builder.desc.DepthStencilState.DepthEnable = FALSE;
        builder.desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        ID3D12PipelineState* state = builder.build(error);
        if (state == nullptr) {
            return false;
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                begin_pass(stage, list, target, clear);
                list->SetPipelineState(state);
                stage.mesh(list, stage.torus, rr::Mat4::rotate_x(0.9f), 0.9f, 0.6f, 0.2f);
            },
            error);
        rr::Image image;
        if (!ok || !kit.read_image(target.resource, D3D12_RESOURCE_STATE_RENDER_TARGET, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
            return false;
        }
        out.add("", std::move(image));
        return true;
    });
    Cases::single(
        rr::CaseSpec("heap", "custom_heap_write_back_l0_vertex_buffer").tolerance(rr::Tolerance::blend(2)).tag("heap"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            Stage stage;
            if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
                return false;
            }
            const MeshData mesh = Shapes::torus(24, 12, 0.85f, 0.3f);
            const UINT64 vertex_bytes = mesh.vertices.size() * sizeof(Vertex);
            D3D12_HEAP_PROPERTIES custom{};
            custom.Type = D3D12_HEAP_TYPE_CUSTOM;
            custom.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
            custom.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
            custom.CreationNodeMask = 1;
            custom.VisibleNodeMask = 1;
            HRESULT result = S_OK;
            ID3D12Resource* vb = D3d12Tools::committed(kit.rig, custom, D3D12_HEAP_FLAG_NONE, D3d12Tools::buffer_desc(vertex_bytes), D3D12_RESOURCE_STATE_COMMON, nullptr, &result);
            if (vb == nullptr) {
                return fail_with(error, "custom heap buffer: " + D3d12Tools::hr(result));
            }
            void* mapped = nullptr;
            const D3D12_RANGE none{0, 0};
            result = vb->Map(0, &none, &mapped);
            if (FAILED(result)) {
                return fail_with(error, "Map: " + D3d12Tools::hr(result));
            }
            std::memcpy(mapped, mesh.vertices.data(), static_cast<std::size_t>(vertex_bytes));
            vb->Unmap(0, nullptr);
            std::vector<std::uint16_t> indices(mesh.indices.begin(), mesh.indices.end());
            ID3D12Resource* ib = kit.make_upload(indices.data(), indices.size() * 2, error);
            Pso builder = stage.pso();
            ID3D12PipelineState* state = builder.build(error);
            if (ib == nullptr || state == nullptr) {
                return false;
            }
            const bool ok = kit.run(
                [&](ID3D12GraphicsCommandList* list) {
                    const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                    stage.canvas.begin(list, clear);
                    list->SetPipelineState(state);
                    const D3D12_VERTEX_BUFFER_VIEW vbv{vb->GetGPUVirtualAddress(), static_cast<UINT>(vertex_bytes), sizeof(Vertex)};
                    const D3D12_INDEX_BUFFER_VIEW ibv{ib->GetGPUVirtualAddress(), static_cast<UINT>(indices.size() * 2), DXGI_FORMAT_R16_UINT};
                    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    list->IASetVertexBuffers(0, 1, &vbv);
                    list->IASetIndexBuffer(&ibv);
                    const float color[4] = {0.9f, 0.6f, 0.2f, 1};
                    set_cbv_root(list, stage.view_projection() * rr::Mat4::rotate_x(0.9f), color);
                    list->DrawIndexedInstanced(static_cast<UINT>(indices.size()), 1, 0, 0, 0);
                },
                error);
            return ok && stage.finish(out, error);
        },
        [](Kit& kit) {
            D3D12_HEAP_PROPERTIES custom{};
            custom.Type = D3D12_HEAP_TYPE_CUSTOM;
            custom.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
            custom.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
            custom.CreationNodeMask = 1;
            custom.VisibleNodeMask = 1;
            Com<ID3D12Resource> probe;
            const D3D12_RESOURCE_DESC desc = D3d12Tools::buffer_desc(256);
            const HRESULT result = kit.device()->CreateCommittedResource(&custom, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), probe.put_void());
            return SUCCEEDED(result) ? std::string() : std::string("custom write-back L0 heaps");
        });
}

std::string needs_tiled(Kit& kit, D3D12_TILED_RESOURCES_TIER tier) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS, options);
    return SUCCEEDED(result) && options.TiledResourcesTier >= tier ? std::string() : std::string("tiled resources");
}

struct Tiled {
    ID3D12Resource* texture = nullptr;
    ID3D12Heap* heap = nullptr;
};

bool make_tiled(Kit& kit, Tiled& tiled, std::string& error) {
    const D3D12_RESOURCE_DESC desc = D3d12Tools::texture_desc(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM);
    D3D12_RESOURCE_DESC reserved = desc;
    reserved.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
    Com<ID3D12Resource> resource;
    const HRESULT result = kit.device()->CreateReservedResource(&reserved, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), resource.put_void());
    if (FAILED(result)) {
        return fail_with(error, "CreateReservedResource: " + D3d12Tools::hr(result));
    }
    tiled.texture = kit.rig.hold(std::move(resource));
    tiled.heap = make_heap(kit, 4 * 65536, D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES, error);
    return tiled.heap != nullptr;
}

bool map_tiles(Kit& kit, ID3D12Resource* texture, ID3D12Heap* heap, UINT first_tile_x, UINT tile_columns, UINT tile_rows, UINT heap_tile, std::string& error) {
    D3D12_TILED_RESOURCE_COORDINATE start{first_tile_x, 0, 0, 0};
    D3D12_TILE_REGION_SIZE size{tile_columns * tile_rows, TRUE, tile_columns, static_cast<UINT16>(tile_rows), 1};
    const D3D12_TILE_RANGE_FLAGS flags = D3D12_TILE_RANGE_FLAG_NONE;
    const UINT heap_start = heap_tile;
    const UINT count = tile_columns * tile_rows;
    kit.rig.direct().queue()->UpdateTileMappings(texture, 1, &start, &size, heap, 1, &flags, &heap_start, &count, D3D12_TILE_MAPPING_FLAG_NONE);
    return kit.rig.direct().idle(error);
}

bool fill_and_sample(Kit& kit, ID3D12Resource* texture, rr::CaptureSet& out, std::string& error, bool fill_left_only) {
    const Texels::Bytes pixels = Texels::content(0, 256, 256);
    const D3D12_RESOURCE_DESC desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 row_size = 0;
    UINT64 total = 0;
    kit.device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &row_size, &total);
    std::vector<std::uint8_t> staged(total, 0);
    for (UINT y = 0; y < rows; ++y) {
        std::memcpy(staged.data() + y * footprint.Footprint.RowPitch, pixels.data() + static_cast<std::size_t>(y) * 256 * 4, 256 * 4);
    }
    ID3D12Resource* upload = kit.make_upload(staged.data(), total, error);
    if (upload == nullptr) {
        return false;
    }
    if (!kit.run([&](ID3D12GraphicsCommandList* list) {
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = texture;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = upload;
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = footprint;
            const D3D12_BOX box{0, 0, 0, fill_left_only ? 128u : 256u, 256, 1};
            list->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
            D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }, error)) {
        return false;
    }
    const D3D12_SAMPLER_DESC sampler = Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    const float tint[4] = {1, 1, 1, 1};
    const float scale[4] = {1, 1, 0, 0};
    return sample_scene(kit, texture, srv_2d(DXGI_FORMAT_R8G8B8A8_UNORM, 1), &sampler, std::string(kSamplePrefix) + "Texture2D<float4> tex : register(t0); SamplerState smp : register(s0);\nfloat4 main(VsOut i) : SV_Target { return tex.Sample(smp, i.uv); }\n",
                        tint, scale, -0.3f, out, error);
}

void register_tiled() {
    Cases::single(rr::CaseSpec("heap", "reserved_texture_all_tiles_mapped").tolerance(rr::Tolerance::blend(2)).tag("heap"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Tiled tiled;
        return make_tiled(kit, tiled, error) && map_tiles(kit, tiled.texture, tiled.heap, 0, 2, 2, 0, error) && fill_and_sample(kit, tiled.texture, out, error, false);
    }, [](Kit& kit) { return needs_tiled(kit, D3D12_TILED_RESOURCES_TIER_1); });
    Cases::single(rr::CaseSpec("heap", "reserved_texture_unmapped_tiles_read_zero").tolerance(rr::Tolerance::blend(2)).tag("heap"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Tiled tiled;
        return make_tiled(kit, tiled, error) && map_tiles(kit, tiled.texture, tiled.heap, 0, 1, 2, 0, error) && fill_and_sample(kit, tiled.texture, out, error, true);
    }, [](Kit& kit) { return needs_tiled(kit, D3D12_TILED_RESOURCES_TIER_2); });
    Cases::single(rr::CaseSpec("heap", "reserved_texture_copy_tile_mappings").tolerance(rr::Tolerance::blend(2)).tag("heap"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Tiled source;
        Tiled destination;
        if (!make_tiled(kit, source, error) || !make_tiled(kit, destination, error) || !map_tiles(kit, source.texture, source.heap, 0, 2, 2, 0, error)) {
            return false;
        }
        const D3D12_TILED_RESOURCE_COORDINATE start{0, 0, 0, 0};
        const D3D12_TILE_REGION_SIZE size{4, TRUE, 2, 2, 1};
        kit.rig.direct().queue()->CopyTileMappings(destination.texture, &start, source.texture, &start, &size, D3D12_TILE_MAPPING_FLAG_NONE);
        return kit.rig.direct().idle(error) && fill_and_sample(kit, destination.texture, out, error, false);
    }, [](Kit& kit) { return needs_tiled(kit, D3D12_TILED_RESOURCES_TIER_1); });
}

const struct Registrar {
    Registrar() {
        register_placed();
        register_tiled();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
