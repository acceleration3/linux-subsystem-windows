#include "sanity/d3d12/barrier_checks.hpp"

#include "sanity/d3d12/kit.hpp"

#include <cstring>

namespace lsw::suites::sanity::d3d12 {

namespace {

const char* kChain =
    "RWStructuredBuffer<uint> d : register(u0);\n"
    "cbuffer C : register(b0) { uint k; uint3 pad; }\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) { d[id.x] = d[id.x] * 1664525u + 1013904223u + k + id.x; }\n";

const char* kPingPong =
    "StructuredBuffer<uint> s : register(t0);\n"
    "RWStructuredBuffer<uint> d : register(u0);\n"
    "cbuffer C : register(b0) { uint k; uint3 pad; }\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) { d[id.x] = s[id.x] * 3u + k; }\n";

const char* kShowUint =
    "StructuredBuffer<uint> t : register(t0);\n"
    "uint main(float4 p : SV_Position) : SV_Target { return t[uint(p.y) * 32u + uint(p.x)]; }\n";

constexpr UINT kElements = 1024;

}  // namespace

Outcome BarrierChecks::transition_uav(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::string error;
        D3d12Root chain_root;
        chain_root.constants(1, 0).uav(0);
        ID3D12RootSignature* chain_signature = chain_root.build(rig, error);
        ID3D12PipelineState* chain = chain_signature != nullptr ? D3d12Tools::compute_from(rig, chain_signature, kChain, error) : nullptr;
        D3d12Root ping_root;
        ping_root.constants(1, 0).srv(0).uav(0);
        ID3D12RootSignature* ping_signature = ping_root.build(rig, error);
        ID3D12PipelineState* ping = ping_signature != nullptr ? D3d12Tools::compute_from(rig, ping_signature, kPingPong, error) : nullptr;
        D3d12Root show_root;
        show_root.srv(0);
        ID3D12RootSignature* show_signature = show_root.build(rig, error);
        ID3D12PipelineState* show = show_signature != nullptr ? Kit::pipeline(rig, show_signature, kShowUint, DXGI_FORMAT_R32_UINT, error) : nullptr;
        if (chain == nullptr || ping == nullptr || show == nullptr) {
            problems.add("setup failed: " + error);
            return {};
        }
        const std::vector<std::uint8_t> raw = Kit::bytes(800, kElements * 4);
        std::vector<std::uint32_t> reference = Kit::words(raw);
        ID3D12Resource* buffer = D3d12Tools::device_buffer(rig, kElements * 4, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (buffer == nullptr || !D3d12Tools::write_buffer(rig, buffer, D3D12_RESOURCE_STATE_COPY_DEST, 0, raw.data(), raw.size(), error)) {
            problems.add("preparing the chain buffer failed: " + error);
            return {};
        }
        for (std::uint32_t k = 0; k < 1000; ++k) {
            for (std::uint32_t i = 0; i < kElements; ++i) {
                reference[i] = reference[i] * 1664525u + 1013904223u + k + i;
            }
        }
        D3d12Frame frame;
        TexelImage image;
        const bool chained = frame.open(rig, 32, 32, error, DXGI_FORMAT_R32_UINT) &&
            frame.run(
                [&](ID3D12GraphicsCommandList* list) {
                    D3d12Tools::move(list, buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    list->SetComputeRootSignature(chain_signature);
                    list->SetPipelineState(chain);
                    list->SetComputeRootUnorderedAccessView(1, buffer->GetGPUVirtualAddress());
                    for (UINT k = 0; k < 1000; ++k) {
                        list->SetComputeRoot32BitConstant(0, k, 0);
                        list->Dispatch(kElements / 64, 1, 1);
                        D3d12Tools::uav_wait(list, buffer);
                    }
                    D3d12Tools::move(list, buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    list->SetGraphicsRootSignature(show_signature);
                    list->SetPipelineState(show);
                    list->SetGraphicsRootShaderResourceView(0, buffer->GetGPUVirtualAddress());
                    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    list->DrawInstanced(3, 1, 0, 0);
                    D3d12Tools::move(list, buffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
                },
                error) &&
            frame.read(image, error);
        if (!chained) {
            problems.add("1000 chained UAV passes: " + error);
        } else {
            Kit::compare_words(context, problems, "pixel shader read of the 1000-pass compute result", reference, Kit::image_words(image));
            std::vector<std::uint32_t> from_buffer;
            if (D3d12Tools::read_words(rig, buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, kElements, from_buffer, error)) {
                Kit::compare_words(context, problems, "buffer contents after the 1000 UAV-barrier passes", reference, from_buffer);
            }
        }
        ID3D12Resource* a = D3d12Tools::device_buffer(rig, kElements * 4, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ID3D12Resource* b = D3d12Tools::device_buffer(rig, kElements * 4, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (a == nullptr || b == nullptr || !D3d12Tools::write_buffer(rig, a, D3D12_RESOURCE_STATE_COPY_DEST, 0, raw.data(), raw.size(), error)) {
            problems.add("preparing the ping-pong buffers failed: " + error);
            return {};
        }
        std::vector<std::uint32_t> ping_reference = Kit::words(raw);
        for (std::uint32_t k = 0; k < 1000; ++k) {
            for (std::uint32_t i = 0; i < kElements; ++i) {
                ping_reference[i] = ping_reference[i] * 3u + k;
            }
        }
        ID3D12Resource* last_written = nullptr;
        const bool ponged = rig.direct().run_wait(
            [&](ID3D12GraphicsCommandList* list) {
                ID3D12Resource* source = a;
                ID3D12Resource* destination = b;
                D3D12_RESOURCE_BARRIER start[2] = {D3d12Tools::transition(source, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                                                   D3d12Tools::transition(destination, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
                list->ResourceBarrier(2, start);
                list->SetComputeRootSignature(ping_signature);
                list->SetPipelineState(ping);
                for (UINT k = 0; k < 1000; ++k) {
                    list->SetComputeRoot32BitConstant(0, k, 0);
                    list->SetComputeRootShaderResourceView(1, source->GetGPUVirtualAddress());
                    list->SetComputeRootUnorderedAccessView(2, destination->GetGPUVirtualAddress());
                    list->Dispatch(kElements / 64, 1, 1);
                    D3D12_RESOURCE_BARRIER swap[2] = {D3d12Tools::transition(destination, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                                                      D3d12Tools::transition(source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
                    list->ResourceBarrier(2, swap);
                    std::swap(source, destination);
                }
                last_written = source;
                D3d12Tools::move(list, source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3d12Tools::move(list, destination, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            },
            error);
        std::vector<std::uint32_t> got;
        if (!ponged || !D3d12Tools::read_words(rig, last_written, D3D12_RESOURCE_STATE_COPY_SOURCE, kElements, got, error)) {
            problems.add("1000 transition passes: " + error);
        } else {
            Kit::compare_words(context, problems, "buffer contents after 1000 SRV/UAV transition passes", ping_reference, got);
        }
        return {};
    });
}

Outcome BarrierChecks::enhanced(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        ID3D12Device* d = rig.device();
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 options{};
        const HRESULT queried = D3d12Tools::feature(d, D3D12_FEATURE_D3D12_OPTIONS12, options);
        Com<ID3D12Device10> device10 = rig.as<ID3D12Device10>();
        std::string error;
        if (FAILED(queried) || !options.EnhancedBarriersSupported) {
            if (device10) {
                D3D12_RESOURCE_DESC1 desc{};
                desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                desc.Width = 4096;
                desc.Height = 1;
                desc.DepthOrArraySize = 1;
                desc.MipLevels = 1;
                desc.SampleDesc.Count = 1;
                desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                const D3D12_HEAP_PROPERTIES heap = D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT);
                Com<ID3D12Resource> resource;
                const HRESULT made = device10->CreateCommittedResource3(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, nullptr, 0, nullptr,
                                                                       __uuidof(ID3D12Resource), resource.put_void());
                if (SUCCEEDED(made)) {
                    problems.add("EnhancedBarriersSupported is false but CreateCommittedResource3 succeeded");
                } else {
                    context.note("without enhanced barriers CreateCommittedResource3 returns " + Kit::hr(made));
                }
            }
            return "enhanced barriers are not supported by this adapter";
        }
        if (!device10) {
            problems.add("EnhancedBarriersSupported is reported but ID3D12Device10 is not exposed");
            return {};
        }
        const D3D12_HEAP_PROPERTIES heap = D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT);
        auto buffer1 = [&](UINT64 size, D3D12_RESOURCE_FLAGS flags) {
            D3D12_RESOURCE_DESC1 desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = size;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags = flags;
            Com<ID3D12Resource> resource;
            const HRESULT made = device10->CreateCommittedResource3(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, nullptr, 0, nullptr,
                                                                   __uuidof(ID3D12Resource), resource.put_void());
            return SUCCEEDED(made) ? rig.hold(std::move(resource)) : nullptr;
        };
        auto texture1 = [&](UINT size, D3D12_RESOURCE_FLAGS flags, HRESULT& made) {
            D3D12_RESOURCE_DESC1 desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = size;
            desc.Height = size;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Flags = flags;
            Com<ID3D12Resource> resource;
            made = device10->CreateCommittedResource3(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_BARRIER_LAYOUT_COMMON, nullptr, nullptr, 0, nullptr,
                                                     __uuidof(ID3D12Resource), resource.put_void());
            return SUCCEEDED(made) ? rig.hold(std::move(resource)) : nullptr;
        };
        auto buffer_barrier = [](ID3D12Resource* resource, D3D12_BARRIER_SYNC sync_before, D3D12_BARRIER_SYNC sync_after,
                                 D3D12_BARRIER_ACCESS access_before, D3D12_BARRIER_ACCESS access_after) {
            D3D12_BUFFER_BARRIER barrier{};
            barrier.SyncBefore = sync_before;
            barrier.SyncAfter = sync_after;
            barrier.AccessBefore = access_before;
            barrier.AccessAfter = access_after;
            barrier.pResource = resource;
            barrier.Offset = 0;
            barrier.Size = ~0ull;
            return barrier;
        };
        auto apply_buffers = [](ID3D12GraphicsCommandList* list, const D3D12_BUFFER_BARRIER* barriers, UINT count) {
            Com<ID3D12GraphicsCommandList7> list7;
            list->QueryInterface(__uuidof(ID3D12GraphicsCommandList7), list7.put_void());
            if (!list7) {
                return false;
            }
            D3D12_BARRIER_GROUP group{};
            group.Type = D3D12_BARRIER_TYPE_BUFFER;
            group.NumBarriers = count;
            group.pBufferBarriers = barriers;
            list7->Barrier(1, &group);
            return true;
        };
        auto apply_textures = [](ID3D12GraphicsCommandList* list, const D3D12_TEXTURE_BARRIER* barriers, UINT count) {
            Com<ID3D12GraphicsCommandList7> list7;
            list->QueryInterface(__uuidof(ID3D12GraphicsCommandList7), list7.put_void());
            if (!list7) {
                return false;
            }
            D3D12_BARRIER_GROUP group{};
            group.Type = D3D12_BARRIER_TYPE_TEXTURE;
            group.NumBarriers = count;
            group.pTextureBarriers = barriers;
            list7->Barrier(1, &group);
            return true;
        };
        auto texture_barrier = [](ID3D12Resource* resource, D3D12_BARRIER_SYNC sync_before, D3D12_BARRIER_SYNC sync_after, D3D12_BARRIER_ACCESS access_before,
                                  D3D12_BARRIER_ACCESS access_after, D3D12_BARRIER_LAYOUT layout_before, D3D12_BARRIER_LAYOUT layout_after) {
            D3D12_TEXTURE_BARRIER barrier{};
            barrier.SyncBefore = sync_before;
            barrier.SyncAfter = sync_after;
            barrier.AccessBefore = access_before;
            barrier.AccessAfter = access_after;
            barrier.LayoutBefore = layout_before;
            barrier.LayoutAfter = layout_after;
            barrier.pResource = resource;
            barrier.Subresources.IndexOrFirstMipLevel = 0xffffffffu;
            return barrier;
        };
        const UINT64 size = 64 * 1024;
        ID3D12Resource* upload = D3d12Tools::upload(rig, size);
        ID3D12Resource* readback = D3d12Tools::readback(rig, size);
        ID3D12Resource* target = buffer1(size, D3D12_RESOURCE_FLAG_NONE);
        if (upload == nullptr || readback == nullptr || target == nullptr) {
            problems.add("creating the buffers failed");
            return {};
        }
        const std::vector<std::uint8_t> data = Kit::bytes(900, static_cast<std::size_t>(size));
        void* mapped = nullptr;
        const D3D12_RANGE none{0, 0};
        upload->Map(0, &none, &mapped);
        std::memcpy(mapped, data.data(), data.size());
        upload->Unmap(0, nullptr);
        bool barriers_applied = true;
        if (!rig.direct().run_wait(
                [&](ID3D12GraphicsCommandList* list) {
                    D3D12_BUFFER_BARRIER first = buffer_barrier(target, D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_NO_ACCESS, D3D12_BARRIER_ACCESS_COPY_DEST);
                    barriers_applied = apply_buffers(list, &first, 1);
                    list->CopyBufferRegion(target, 0, upload, 0, size);
                    D3D12_BUFFER_BARRIER second = buffer_barrier(target, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_ACCESS_COPY_SOURCE);
                    barriers_applied = apply_buffers(list, &second, 1) && barriers_applied;
                    list->CopyBufferRegion(readback, 0, target, 0, size);
                },
                error)) {
            problems.add("buffer barriers: " + error);
            return {};
        }
        if (!barriers_applied) {
            problems.add("ID3D12GraphicsCommandList7 is not exposed although enhanced barriers are supported");
            return {};
        }
        std::vector<std::uint8_t> back;
        if (!D3d12Tools::map_bytes(readback, 0, size, back, error)) {
            problems.add(error);
            return {};
        }
        static_cast<void>(Kit::same_bytes(context, problems, "buffer written and read under enhanced barriers", data.data(), back.data(), data.size()));
        HRESULT made = S_OK;
        ID3D12Resource* color = texture1(64, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, made);
        ID3D12DescriptorHeap* rtv = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
        if (color == nullptr || rtv == nullptr) {
            problems.add("creating the render target through CreateCommittedResource3 failed: " + Kit::hr(made));
            return {};
        }
        d->CreateRenderTargetView(color, nullptr, rtv->GetCPUDescriptorHandleForHeapStart());
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows = 0;
        UINT64 row_size = 0;
        UINT64 total = 0;
        const D3D12_RESOURCE_DESC color_desc = color->GetDesc();
        d->GetCopyableFootprints(&color_desc, 0, 1, 0, &footprint, &rows, &row_size, &total);
        ID3D12Resource* image_readback = D3d12Tools::readback(rig, total);
        if (image_readback == nullptr) {
            problems.add("creating the image readback buffer failed");
            return {};
        }
        const float red[4] = {1, 0, 0, 1};
        if (!rig.direct().run_wait(
                [&](ID3D12GraphicsCommandList* list) {
                    D3D12_TEXTURE_BARRIER to_target = texture_barrier(color, D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_NO_ACCESS,
                                                                       D3D12_BARRIER_ACCESS_RENDER_TARGET, D3D12_BARRIER_LAYOUT_COMMON, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
                    barriers_applied = apply_textures(list, &to_target, 1);
                    list->ClearRenderTargetView(rtv->GetCPUDescriptorHandleForHeapStart(), red, 0, nullptr);
                    D3D12_TEXTURE_BARRIER to_copy = texture_barrier(color, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_RENDER_TARGET,
                                                                     D3D12_BARRIER_ACCESS_COPY_SOURCE, D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_LAYOUT_COPY_SOURCE);
                    barriers_applied = apply_textures(list, &to_copy, 1) && barriers_applied;
                    D3D12_TEXTURE_COPY_LOCATION destination{};
                    destination.pResource = image_readback;
                    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    destination.PlacedFootprint = footprint;
                    D3D12_TEXTURE_COPY_LOCATION source{};
                    source.pResource = color;
                    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
                },
                error)) {
            problems.add("texture layout barriers: " + error);
            return {};
        }
        std::vector<std::uint8_t> mapped_image;
        if (!D3d12Tools::map_bytes(image_readback, 0, total, mapped_image, error)) {
            problems.add(error);
            return {};
        }
        TexelImage image;
        D3d12Tools::tight_rows(footprint, rows, row_size, mapped_image.data(), image);
        std::string where;
        if (!Kit::uniform(image, Kit::rgba(255, 0, 0, 255), where)) {
            problems.add("render target cleared under layout barriers: " + where);
        }
        if (!rig.open_queue(D3D12_COMMAND_LIST_TYPE_COMPUTE, error)) {
            problems.add(error);
            return {};
        }
        D3d12Root root;
        root.uav(0);
        ID3D12RootSignature* signature = root.build(rig, error);
        ID3D12PipelineState* fill = signature != nullptr ? D3d12Tools::compute_from(rig, signature,
            "RWStructuredBuffer<uint> o : register(u0);\n[numthreads(64, 1, 1)]\nvoid main(uint3 id : SV_DispatchThreadID) { o[id.x] = id.x * 5u + 11u; }\n", error) : nullptr;
        ID3D12Resource* shared = buffer1(256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ID3D12Resource* copy_out = D3d12Tools::readback(rig, 256);
        if (fill == nullptr || shared == nullptr || copy_out == nullptr) {
            problems.add("cross-queue setup failed: " + error);
            return {};
        }
        std::uint64_t produced = 0;
        bool compute_ok = true;
        if (!rig.compute().run(
                [&](ID3D12GraphicsCommandList* list) {
                    D3D12_BUFFER_BARRIER acquire = buffer_barrier(shared, D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_NO_ACCESS,
                                                                  D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
                    compute_ok = apply_buffers(list, &acquire, 1);
                    list->SetComputeRootSignature(signature);
                    list->SetPipelineState(fill);
                    list->SetComputeRootUnorderedAccessView(0, shared->GetGPUVirtualAddress());
                    list->Dispatch(1, 1, 1);
                    D3D12_BUFFER_BARRIER release = buffer_barrier(shared, D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                                                                  D3D12_BARRIER_ACCESS_NO_ACCESS);
                    compute_ok = apply_buffers(list, &release, 1) && compute_ok;
                },
                produced, error) ||
            !compute_ok || !rig.direct().gpu_wait(rig.compute(), produced)) {
            problems.add("compute queue barriers: " + error);
            return {};
        }
        if (!rig.direct().run_wait(
                [&](ID3D12GraphicsCommandList* list) {
                    D3D12_BUFFER_BARRIER acquire = buffer_barrier(shared, D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_NO_ACCESS,
                                                                  D3D12_BARRIER_ACCESS_COPY_SOURCE);
                    static_cast<void>(apply_buffers(list, &acquire, 1));
                    list->CopyBufferRegion(copy_out, 0, shared, 0, 256);
                },
                error) ||
            !D3d12Tools::map_bytes(copy_out, 0, 256, back, error)) {
            problems.add("direct queue acquire: " + error);
            return {};
        }
        std::vector<std::uint32_t> expected(64);
        for (std::uint32_t i = 0; i < 64; ++i) {
            expected[i] = i * 5u + 11u;
        }
        Kit::compare_words(context, problems, "buffer handed from the compute queue to the direct queue with enhanced barriers", expected, Kit::words(back));
        return {};
    });
}

Outcome BarrierChecks::misuse_tolerance(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::string error;
        const UINT64 size = 4096;
        ID3D12Resource* upload = D3d12Tools::upload(rig, size);
        ID3D12Resource* readback = D3d12Tools::readback(rig, size);
        ID3D12Resource* buffer = D3d12Tools::device_buffer(rig, size, D3D12_RESOURCE_STATE_COMMON);
        if (upload == nullptr || readback == nullptr || buffer == nullptr) {
            problems.add("creating the buffers failed");
            return {};
        }
        void* mapped = nullptr;
        const D3D12_RANGE none{0, 0};
        upload->Map(0, &none, &mapped);
        for (int round = 0; round < 5; ++round) {
            const std::vector<std::uint8_t> data = Kit::bytes(950 + static_cast<std::uint64_t>(round), static_cast<std::size_t>(size));
            std::memcpy(mapped, data.data(), data.size());
            std::vector<std::uint8_t> back;
            if (!rig.direct().run_wait([&](ID3D12GraphicsCommandList* list) { list->CopyBufferRegion(buffer, 0, upload, 0, size); }, error) ||
                !rig.direct().run_wait([&](ID3D12GraphicsCommandList* list) { list->CopyBufferRegion(readback, 0, buffer, 0, size); }, error) ||
                !D3d12Tools::map_bytes(readback, 0, size, back, error)) {
                problems.add("promotion and decay of a COMMON buffer, round " + std::to_string(round) + ": " + error);
                return {};
            }
            if (std::memcmp(back.data(), data.data(), data.size()) != 0) {
                problems.add("a COMMON buffer promoted to COPY_DEST and COPY_SOURCE without barriers lost data in round " + std::to_string(round));
                break;
            }
        }
        upload->Unmap(0, nullptr);
        {
            const UINT edge = 16;
            ID3D12Resource* texture = D3d12Tools::texture(rig, D3d12Tools::texture_desc(edge, edge, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS),
                                                          D3D12_RESOURCE_STATE_COMMON);
            ID3D12Resource* stage = D3d12Tools::upload(rig, 256 * edge);
            ID3D12Resource* out = D3d12Tools::readback(rig, 256 * edge);
            if (texture == nullptr || stage == nullptr || out == nullptr) {
                problems.add("creating the simultaneous-access texture failed");
                return {};
            }
            std::vector<std::uint8_t> padded(256 * edge, 0);
            std::vector<std::uint8_t> tight(edge * edge * 4);
            Pattern::fill_pixels(960, edge, edge, edge * 4, tight.data());
            for (UINT y = 0; y < edge; ++y) {
                std::memcpy(padded.data() + y * 256, tight.data() + y * edge * 4, edge * 4);
            }
            stage->Map(0, &none, &mapped);
            std::memcpy(mapped, padded.data(), padded.size());
            stage->Unmap(0, nullptr);
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
            footprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, edge, edge, 1, 256};
            std::vector<std::uint8_t> back;
            const bool done = rig.direct().run_wait(
                              [&](ID3D12GraphicsCommandList* list) {
                                  D3D12_TEXTURE_COPY_LOCATION destination{};
                                  destination.pResource = texture;
                                  destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                                  D3D12_TEXTURE_COPY_LOCATION source{};
                                  source.pResource = stage;
                                  source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                                  source.PlacedFootprint = footprint;
                                  list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
                              },
                              error) &&
                          rig.direct().run_wait(
                              [&](ID3D12GraphicsCommandList* list) {
                                  D3D12_TEXTURE_COPY_LOCATION destination{};
                                  destination.pResource = out;
                                  destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                                  destination.PlacedFootprint = footprint;
                                  D3D12_TEXTURE_COPY_LOCATION source{};
                                  source.pResource = texture;
                                  source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                                  list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
                              },
                              error) &&
                          D3d12Tools::map_bytes(out, 0, padded.size(), back, error);
            if (!done) {
                problems.add("promotion of a simultaneous-access texture: " + error);
            } else {
                for (UINT y = 0; y < edge; ++y) {
                    if (std::memcmp(back.data() + y * 256, tight.data() + y * edge * 4, edge * 4) != 0) {
                        problems.add("a simultaneous-access texture promoted from COMMON without barriers returned wrong data in row " + std::to_string(y));
                        break;
                    }
                }
            }
            const D3D12_RESOURCE_DESC two_mips = D3d12Tools::texture_desc(edge, edge, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE, 2);
            ID3D12Resource* mips = D3d12Tools::texture(rig, two_mips, D3D12_RESOURCE_STATE_COPY_DEST);
            const std::vector<std::uint8_t> level0 = tight;
            std::vector<std::uint8_t> level1(8 * 8 * 4);
            Pattern::fill_pixels(961, 8, 8, 32, level1.data());
            TexelImage got;
            if (mips == nullptr || !D3d12Tools::write_texture(rig, mips, D3D12_RESOURCE_STATE_COPY_DEST, 0, level0.data(), error) ||
                !D3d12Tools::write_texture(rig, mips, D3D12_RESOURCE_STATE_COPY_DEST, 1, level1.data(), error)) {
                problems.add("preparing the per-subresource texture failed: " + error);
            } else {
                const bool split = rig.direct().run_wait(
                    [&](ID3D12GraphicsCommandList* list) {
                        D3D12_RESOURCE_BARRIER begin = D3d12Tools::transition(mips, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE, 0);
                        begin.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
                        list->ResourceBarrier(1, &begin);
                        D3d12Tools::move(list, mips, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE, 1);
                        D3D12_RESOURCE_BARRIER end = D3d12Tools::transition(mips, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE, 0);
                        end.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
                        list->ResourceBarrier(1, &end);
                        const D3D12_RESOURCE_BARRIER everything = D3d12Tools::aliasing_barrier(nullptr, nullptr);
                        list->ResourceBarrier(1, &everything);
                        D3d12Tools::move(list, mips, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                    },
                    error);
                if (!split || !D3d12Tools::read_texture(rig, mips, D3D12_RESOURCE_STATE_COPY_DEST, 0, got, error)) {
                    problems.add("split and per-subresource barriers: " + error);
                } else {
                    static_cast<void>(Kit::same_bytes(context, problems, "mip 0 after split and per-subresource barriers", level0.data(), got.data.data(), level0.size()));
                    if (D3d12Tools::read_texture(rig, mips, D3D12_RESOURCE_STATE_COPY_DEST, 1, got, error)) {
                        static_cast<void>(Kit::same_bytes(context, problems, "mip 1 after split and per-subresource barriers", level1.data(), got.data.data(), level1.size()));
                    }
                }
            }
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::d3d12
