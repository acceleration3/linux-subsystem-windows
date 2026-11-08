#include "sanity/d3d12/heap_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/d3d12/kit.hpp"
#include "support/child_runs.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::sanity::d3d12 {

namespace {

constexpr UINT64 kKiB = 1024;
constexpr UINT64 kMiB = 1024 * 1024;

bool round_trip(D3d12Rig& rig, UINT64 size, UINT64 upload_offset, UINT64 default_offset, UINT64 readback_offset, std::uint64_t seed,
                std::vector<std::uint8_t>& expected, std::vector<std::uint8_t>& actual, std::string& error) {
    const UINT64 total = size + 16;
    HRESULT result = S_OK;
    ID3D12Resource* up = D3d12Tools::upload(rig, total, &result);
    ID3D12Resource* def = up != nullptr ? D3d12Tools::device_buffer(rig, total, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE, &result) : nullptr;
    ID3D12Resource* rb = def != nullptr ? D3d12Tools::readback(rig, total, &result) : nullptr;
    if (rb == nullptr) {
        error = "creating the buffers failed: " + D3d12Tools::hr(result);
        rig.release(rb);
        rig.release(def);
        rig.release(up);
        return false;
    }
    expected.resize(static_cast<std::size_t>(size));
    Pattern::fill(seed, 0, expected.data(), expected.size());
    void* mapped = nullptr;
    const D3D12_RANGE none{0, 0};
    result = up->Map(0, &none, &mapped);
    if (FAILED(result)) {
        error = "mapping the upload buffer failed: " + D3d12Tools::hr(result);
        rig.release(rb);
        rig.release(def);
        rig.release(up);
        return false;
    }
    std::memcpy(static_cast<std::uint8_t*>(mapped) + upload_offset, expected.data(), expected.size());
    up->Unmap(0, nullptr);
    const bool done = rig.direct().run_wait(
        [&](ID3D12GraphicsCommandList* list) {
            list->CopyBufferRegion(def, default_offset, up, upload_offset, size);
            D3d12Tools::move(list, def, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->CopyBufferRegion(rb, readback_offset, def, default_offset, size);
        },
        error);
    if (done) {
        const D3D12_RANGE all{0, static_cast<SIZE_T>(total)};
        result = rb->Map(0, &all, &mapped);
        if (FAILED(result)) {
            error = "mapping the readback buffer failed: " + D3d12Tools::hr(result);
        } else {
            actual.assign(static_cast<const std::uint8_t*>(mapped) + readback_offset, static_cast<const std::uint8_t*>(mapped) + readback_offset + size);
            rb->Unmap(0, &none);
        }
    }
    rig.release(rb);
    rig.release(def);
    rig.release(up);
    return done && actual.size() == size;
}

D3D12_HEAP_DESC heap_desc(UINT64 size, D3D12_HEAP_TYPE type, D3D12_HEAP_FLAGS flags, UINT64 alignment = 0) {
    D3D12_HEAP_DESC desc{};
    desc.SizeInBytes = size;
    desc.Properties = D3d12Tools::heap_props(type);
    desc.Alignment = alignment;
    desc.Flags = flags;
    return desc;
}

ID3D12Heap* create_heap(D3d12Rig& rig, const D3D12_HEAP_DESC& desc, HRESULT* result = nullptr) {
    Com<ID3D12Heap> heap;
    const HRESULT made = rig.device()->CreateHeap(&desc, __uuidof(ID3D12Heap), heap.put_void());
    if (result != nullptr) {
        *result = made;
    }
    return SUCCEEDED(made) ? rig.hold(std::move(heap)) : nullptr;
}

ID3D12Resource* place(D3d12Rig& rig, ID3D12Heap* heap, UINT64 offset, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state,
                      HRESULT* result = nullptr, const D3D12_CLEAR_VALUE* clear = nullptr) {
    Com<ID3D12Resource> resource;
    const HRESULT made = rig.device()->CreatePlacedResource(heap, offset, &desc, state, clear, __uuidof(ID3D12Resource), resource.put_void());
    if (result != nullptr) {
        *result = made;
    }
    return SUCCEEDED(made) ? rig.hold(std::move(resource)) : nullptr;
}

}  // namespace

Outcome HeapChecks::default_upload_readback(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::vector<UINT64> sizes = {1,    2,     3,     7,     255,   256,     257,      4095,         4096,
                                     4097, 65535, 65536, 65537, kMiB + 13, 16 * kMiB, 64 * kMiB, 128 * kMiB};
        const UINT64 local = Kit::device_local_budget(rig);
        const UINT64 host = Kit::host_budget(rig);
        UINT64 largest = 0;
        for (UINT64 size : sizes) {
            if (size > 16 * kMiB && ((local != 0 && size * 2 > local / 2) || (host != 0 && size * 2 > host / 2))) {
                continue;
            }
            struct Offsets {
                UINT64 up;
                UINT64 def;
                UINT64 rb;
            };
            std::vector<Offsets> variants = {{0, 0, 0}};
            if (size <= kMiB + 13) {
                variants.push_back({1, 3, 5});
                variants.push_back({7, 0, 11});
            }
            for (const Offsets& offsets : variants) {
                std::vector<std::uint8_t> expected;
                std::vector<std::uint8_t> actual;
                std::string error;
                const std::string label = "size " + std::to_string(size) + " offsets " + std::to_string(offsets.up) + "/" +
                                          std::to_string(offsets.def) + "/" + std::to_string(offsets.rb);
                if (!round_trip(rig, size, offsets.up, offsets.def, offsets.rb, size * 31 + offsets.up, expected, actual, error)) {
                    problems.add(label + ": " + error);
                    continue;
                }
                static_cast<void>(Kit::same_bytes(context, problems, label, expected.data(), actual.data(), expected.size()));
            }
            largest = std::max(largest, size);
        }
        context.measure("d3d12.largest_round_trip", static_cast<long long>(largest));
        return {};
    });
}

Outcome HeapChecks::custom_properties(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        ID3D12Device* d = rig.device();
        D3D12_FEATURE_DATA_ARCHITECTURE1 arch{};
        if (!Kit::succeeds(problems, "ARCHITECTURE1", D3d12Tools::feature(d, D3D12_FEATURE_ARCHITECTURE1, arch))) {
            return {};
        }
        const bool uma = arch.UMA != FALSE;
        const D3D12_CPU_PAGE_PROPERTY cpu_values[] = {D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE,
                                                      D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE, D3D12_CPU_PAGE_PROPERTY_WRITE_BACK};
        const D3D12_MEMORY_POOL pool_values[] = {D3D12_MEMORY_POOL_UNKNOWN, D3D12_MEMORY_POOL_L0, D3D12_MEMORY_POOL_L1};
        const char* cpu_names[] = {"UNKNOWN", "NOT_AVAILABLE", "WRITE_COMBINE", "WRITE_BACK"};
        const char* pool_names[] = {"UNKNOWN", "L0", "L1"};
        for (int c = 0; c < 4; ++c) {
            for (int p = 0; p < 3; ++p) {
                D3D12_HEAP_PROPERTIES props = D3d12Tools::heap_props(D3D12_HEAP_TYPE_CUSTOM);
                props.CPUPageProperty = cpu_values[c];
                props.MemoryPoolPreference = pool_values[p];
                const std::string label = std::string("custom heap ") + cpu_names[c] + "/" + pool_names[p];
                const bool cpu_visible = c >= 2;
                const bool must_reject = c == 0 || p == 0;
                const bool may_reject = (uma && p == 2) || (!uma && cpu_visible && p == 2);
                HRESULT made = S_OK;
                ID3D12Resource* resource = D3d12Tools::committed(rig, props, D3D12_HEAP_FLAG_NONE, D3d12Tools::buffer_desc(64 * kKiB),
                                                                 D3D12_RESOURCE_STATE_COMMON, nullptr, &made);
                context.measure("custom." + std::string(cpu_names[c]) + "." + pool_names[p], Kit::hr(made));
                if (must_reject) {
                    if (resource != nullptr) {
                        problems.add(label + " was accepted although the properties are not valid on this adapter");
                        rig.release(resource);
                    } else {
                        Kit::expect(problems, label, made, {E_INVALIDARG});
                    }
                } else if (resource != nullptr) {
                    rig.release(resource);
                } else if (may_reject) {
                    Kit::expect(problems, label, made, {E_INVALIDARG});
                }
            }
        }
        for (D3D12_HEAP_TYPE type : {D3D12_HEAP_TYPE_UPLOAD, D3D12_HEAP_TYPE_READBACK}) {
            const bool is_upload = type == D3D12_HEAP_TYPE_UPLOAD;
            const std::string label = is_upload ? "custom equivalent of UPLOAD" : "custom equivalent of READBACK";
            const D3D12_HEAP_PROPERTIES props = d->GetCustomHeapProperties(1, type);
            HRESULT made = S_OK;
            ID3D12Resource* custom = D3d12Tools::committed(rig, props, D3D12_HEAP_FLAG_NONE, D3d12Tools::buffer_desc(256 * kKiB),
                                                           is_upload ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST, nullptr, &made);
            if (custom == nullptr) {
                problems.add(label + " cannot be created: " + Kit::hr(made));
                continue;
            }
            const std::vector<std::uint8_t> data = Kit::bytes(is_upload ? 5 : 6, 256 * kKiB);
            std::string error;
            if (is_upload) {
                void* mapped = nullptr;
                const D3D12_RANGE none{0, 0};
                if (!Kit::succeeds(problems, label + " Map", custom->Map(0, &none, &mapped))) {
                    continue;
                }
                std::memcpy(mapped, data.data(), data.size());
                custom->Unmap(0, nullptr);
                std::vector<std::uint8_t> back;
                ID3D12Resource* def = D3d12Tools::device_buffer(rig, data.size(), D3D12_RESOURCE_STATE_COPY_DEST);
                if (def == nullptr || !rig.direct().run_wait([&](ID3D12GraphicsCommandList* list) { list->CopyBufferRegion(def, 0, custom, 0, data.size()); }, error) ||
                    !D3d12Tools::read_buffer(rig, def, D3D12_RESOURCE_STATE_COPY_DEST, 0, data.size(), back, error)) {
                    problems.add(label + " round trip failed: " + error);
                    continue;
                }
                static_cast<void>(Kit::same_bytes(context, problems, label, data.data(), back.data(), data.size()));
            } else {
                ID3D12Resource* def = D3d12Tools::device_buffer(rig, data.size(), D3D12_RESOURCE_STATE_COPY_DEST);
                if (def == nullptr || !D3d12Tools::write_buffer(rig, def, D3D12_RESOURCE_STATE_COPY_DEST, 0, data.data(), data.size(), error) ||
                    !rig.direct().run_wait(
                        [&](ID3D12GraphicsCommandList* list) {
                            D3d12Tools::move(list, def, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
                            list->CopyBufferRegion(custom, 0, def, 0, data.size());
                        },
                        error)) {
                    problems.add(label + " round trip failed: " + error);
                    continue;
                }
                void* mapped = nullptr;
                const D3D12_RANGE all{0, static_cast<SIZE_T>(data.size())};
                if (!Kit::succeeds(problems, label + " Map", custom->Map(0, &all, &mapped))) {
                    continue;
                }
                static_cast<void>(Kit::same_bytes(context, problems, label, data.data(), mapped, data.size()));
                custom->Unmap(0, nullptr);
            }
        }
        return {};
    });
}

Outcome HeapChecks::placed_alignment(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        ID3D12Device* d = rig.device();
        const D3D12_RESOURCE_DESC buffer = D3d12Tools::buffer_desc(100000);
        const D3D12_RESOURCE_ALLOCATION_INFO buffer_info = d->GetResourceAllocationInfo(0, 1, &buffer);
        if (buffer_info.Alignment != 64 * kKiB || buffer_info.SizeInBytes < 100000 || buffer_info.SizeInBytes % buffer_info.Alignment != 0) {
            problems.add("a 100000-byte buffer reports alignment " + std::to_string(buffer_info.Alignment) + " and size " + std::to_string(buffer_info.SizeInBytes));
        }
        const D3D12_RESOURCE_DESC texture = D3d12Tools::texture_desc(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        const D3D12_RESOURCE_ALLOCATION_INFO texture_info = d->GetResourceAllocationInfo(0, 1, &texture);
        if (texture_info.Alignment != 64 * kKiB || texture_info.SizeInBytes < 256 * 256 * 4 || texture_info.SizeInBytes % texture_info.Alignment != 0) {
            problems.add("a 256x256 RGBA8 texture reports alignment " + std::to_string(texture_info.Alignment) + " and size " + std::to_string(texture_info.SizeInBytes));
        }
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaa_levels{};
        msaa_levels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        msaa_levels.SampleCount = 4;
        if (SUCCEEDED(d->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msaa_levels, sizeof(msaa_levels))) && msaa_levels.NumQualityLevels > 0) {
            const D3D12_RESOURCE_DESC msaa = D3d12Tools::texture_desc(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 1, 1, 4);
            const D3D12_RESOURCE_ALLOCATION_INFO msaa_info = d->GetResourceAllocationInfo(0, 1, &msaa);
            if (msaa_info.Alignment != 4 * kMiB) {
                problems.add("a 4x MSAA texture with the default alignment reports " + std::to_string(msaa_info.Alignment) + " instead of 4 MiB");
            }
            if (msaa_info.SizeInBytes < 64 * 64 * 4 * 4 || msaa_info.SizeInBytes % msaa_info.Alignment != 0) {
                problems.add("a 4x MSAA texture reports size " + std::to_string(msaa_info.SizeInBytes));
            }
            D3D12_FEATURE_DATA_D3D12_OPTIONS4 o4{};
            if (SUCCEEDED(D3d12Tools::feature(d, D3D12_FEATURE_D3D12_OPTIONS4, o4)) && o4.MSAA64KBAlignedTextureSupported) {
                D3D12_RESOURCE_DESC small = msaa;
                small.Alignment = 64 * kKiB;
                const D3D12_RESOURCE_ALLOCATION_INFO small_info = d->GetResourceAllocationInfo(0, 1, &small);
                if (small_info.Alignment != 64 * kKiB) {
                    problems.add("MSAA64KBAlignedTextureSupported is reported but a 64 KiB aligned MSAA texture reports alignment " + std::to_string(small_info.Alignment));
                }
            }
        }
        const UINT64 first_size = buffer_info.SizeInBytes;
        const UINT64 second_offset = first_size;
        const UINT64 heap_size = D3d12Tools::align(second_offset + buffer_info.SizeInBytes, 4 * kMiB);
        HRESULT made = S_OK;
        ID3D12Heap* heap = create_heap(rig, heap_desc(heap_size, D3D12_HEAP_TYPE_DEFAULT, Kit::buffer_heap_flags(rig)), &made);
        if (heap == nullptr) {
            problems.add("CreateHeap failed: " + Kit::hr(made));
            return {};
        }
        const D3D12_HEAP_DESC reported = heap->GetDesc();
        if (reported.SizeInBytes != heap_size || reported.Alignment < 64 * kKiB || reported.Properties.Type != D3D12_HEAP_TYPE_DEFAULT) {
            problems.add("ID3D12Heap::GetDesc does not return the creation description");
        }
        ID3D12Resource* a = place(rig, heap, 0, buffer, D3D12_RESOURCE_STATE_COPY_DEST, &made);
        if (a == nullptr) {
            problems.add("a placed buffer at offset 0 failed: " + Kit::hr(made));
            return {};
        }
        ID3D12Resource* b = place(rig, heap, second_offset, buffer, D3D12_RESOURCE_STATE_COPY_DEST, &made);
        if (b == nullptr) {
            problems.add("a placed buffer at offset " + std::to_string(second_offset) + " failed: " + Kit::hr(made));
            return {};
        }
        ID3D12Resource* c = place(rig, heap, heap_size - buffer_info.SizeInBytes, buffer, D3D12_RESOURCE_STATE_COPY_DEST, &made);
        if (c == nullptr && heap_size - buffer_info.SizeInBytes != second_offset) {
            problems.add("a placed buffer ending exactly at the end of the heap failed: " + Kit::hr(made));
        }
        const std::vector<std::uint8_t> data_a = Kit::bytes(11, 100000);
        const std::vector<std::uint8_t> data_b = Kit::bytes(12, 100000);
        std::string error;
        if (!D3d12Tools::write_buffer(rig, a, D3D12_RESOURCE_STATE_COPY_DEST, 0, data_a.data(), data_a.size(), error) ||
            !D3d12Tools::write_buffer(rig, b, D3D12_RESOURCE_STATE_COPY_DEST, 0, data_b.data(), data_b.size(), error)) {
            problems.add("writing the placed buffers failed: " + error);
            return {};
        }
        std::vector<std::uint8_t> back_a;
        std::vector<std::uint8_t> back_b;
        if (!D3d12Tools::read_buffer(rig, a, D3D12_RESOURCE_STATE_COPY_DEST, 0, data_a.size(), back_a, error) ||
            !D3d12Tools::read_buffer(rig, b, D3D12_RESOURCE_STATE_COPY_DEST, 0, data_b.size(), back_b, error)) {
            problems.add("reading the placed buffers failed: " + error);
            return {};
        }
        static_cast<void>(Kit::same_bytes(context, problems, "placed buffer at 0", data_a.data(), back_a.data(), data_a.size()));
        static_cast<void>(Kit::same_bytes(context, problems, "adjacent placed buffer", data_b.data(), back_b.data(), data_b.size()));
        ID3D12Heap* texture_heap = create_heap(rig, heap_desc(D3d12Tools::align(texture_info.SizeInBytes * 2, 64 * kKiB), D3D12_HEAP_TYPE_DEFAULT,
                                                              Kit::texture_heap_flags(rig, true)), &made);
        if (texture_heap == nullptr) {
            problems.add("creating a render target heap failed: " + Kit::hr(made));
            return {};
        }
        ID3D12Resource* first = place(rig, texture_heap, 0, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, &made);
        ID3D12Resource* second = place(rig, texture_heap, texture_info.SizeInBytes, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, &made);
        if (first == nullptr || second == nullptr) {
            problems.add("placing two render target textures one after the other failed: " + Kit::hr(made));
            return {};
        }
        ID3D12DescriptorHeap* rtvs = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
        const UINT increment = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        d->CreateRenderTargetView(first, nullptr, D3d12Tools::cpu_at(rtvs, 0, increment));
        d->CreateRenderTargetView(second, nullptr, D3d12Tools::cpu_at(rtvs, 1, increment));
        const float red[4] = {1, 0, 0, 1};
        const float blue[4] = {0, 0, 1, 1};
        const bool cleared = rig.direct().run_wait(
            [&](ID3D12GraphicsCommandList* list) {
                list->ClearRenderTargetView(D3d12Tools::cpu_at(rtvs, 0, increment), red, 0, nullptr);
                list->ClearRenderTargetView(D3d12Tools::cpu_at(rtvs, 1, increment), blue, 0, nullptr);
            },
            error);
        TexelImage image_first;
        TexelImage image_second;
        if (!cleared || !D3d12Tools::read_texture(rig, first, D3D12_RESOURCE_STATE_RENDER_TARGET, 0, image_first, error) ||
            !D3d12Tools::read_texture(rig, second, D3D12_RESOURCE_STATE_RENDER_TARGET, 0, image_second, error)) {
            problems.add("rendering to the placed textures failed: " + error);
            return {};
        }
        std::string where;
        if (!Kit::uniform(image_first, Kit::rgba(255, 0, 0, 255), where)) {
            problems.add("first placed texture: " + where);
        }
        if (!Kit::uniform(image_second, Kit::rgba(0, 0, 255, 255), where)) {
            problems.add("second placed texture: " + where);
        }
        return {};
    });
}

Outcome HeapChecks::aliasing(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        ID3D12Device* d = rig.device();
        const D3D12_RESOURCE_DESC texture = D3d12Tools::texture_desc(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        const D3D12_RESOURCE_ALLOCATION_INFO info = d->GetResourceAllocationInfo(0, 1, &texture);
        HRESULT made = S_OK;
        ID3D12Heap* heap = create_heap(rig, heap_desc(info.SizeInBytes, D3D12_HEAP_TYPE_DEFAULT, Kit::texture_heap_flags(rig, true)), &made);
        if (heap == nullptr) {
            problems.add("CreateHeap failed: " + Kit::hr(made));
            return {};
        }
        ID3D12Resource* a = place(rig, heap, 0, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, &made);
        ID3D12Resource* b = place(rig, heap, 0, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, &made);
        if (a == nullptr || b == nullptr) {
            problems.add("placing two textures on the same heap range failed: " + Kit::hr(made));
            return {};
        }
        ID3D12DescriptorHeap* rtvs = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
        const UINT increment = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        d->CreateRenderTargetView(a, nullptr, D3d12Tools::cpu_at(rtvs, 0, increment));
        d->CreateRenderTargetView(b, nullptr, D3d12Tools::cpu_at(rtvs, 1, increment));
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows = 0;
        UINT64 row_size = 0;
        UINT64 total = 0;
        d->GetCopyableFootprints(&texture, 0, 1, 0, &footprint, &rows, &row_size, &total);
        ID3D12Resource* readback = D3d12Tools::readback(rig, total * 3);
        if (readback == nullptr) {
            problems.add("creating the readback buffer failed");
            return {};
        }
        auto copy_out = [&](ID3D12GraphicsCommandList* list, ID3D12Resource* source, UINT64 slot) {
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = readback;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = footprint;
            destination.PlacedFootprint.Offset = slot * total;
            D3D12_TEXTURE_COPY_LOCATION from{};
            from.pResource = source;
            from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            from.SubresourceIndex = 0;
            list->CopyTextureRegion(&destination, 0, 0, 0, &from, nullptr);
        };
        const float first[4] = {1, 0, 0, 1};
        const float second[4] = {0, 1, 0, 1};
        const float third[4] = {0, 0, 1, 1};
        std::string error;
        const bool done = rig.direct().run_wait(
            [&](ID3D12GraphicsCommandList* list) {
                list->ClearRenderTargetView(D3d12Tools::cpu_at(rtvs, 0, increment), first, 0, nullptr);
                D3d12Tools::move(list, a, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
                copy_out(list, a, 0);
                const D3D12_RESOURCE_BARRIER to_b = D3d12Tools::aliasing_barrier(a, b);
                list->ResourceBarrier(1, &to_b);
                list->DiscardResource(b, nullptr);
                list->ClearRenderTargetView(D3d12Tools::cpu_at(rtvs, 1, increment), second, 0, nullptr);
                D3d12Tools::move(list, b, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
                copy_out(list, b, 1);
                const D3D12_RESOURCE_BARRIER to_a = D3d12Tools::aliasing_barrier(b, a);
                list->ResourceBarrier(1, &to_a);
                D3d12Tools::move(list, a, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
                list->DiscardResource(a, nullptr);
                list->ClearRenderTargetView(D3d12Tools::cpu_at(rtvs, 0, increment), third, 0, nullptr);
                D3d12Tools::move(list, a, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
                copy_out(list, a, 2);
            },
            error);
        if (!done) {
            problems.add("the aliasing sequence failed: " + error);
            return {};
        }
        void* mapped = nullptr;
        const D3D12_RANGE all{0, static_cast<SIZE_T>(total * 3)};
        if (!Kit::succeeds(problems, "Map readback", readback->Map(0, &all, &mapped))) {
            return {};
        }
        const std::uint32_t expected[3] = {Kit::rgba(255, 0, 0, 255), Kit::rgba(0, 255, 0, 255), Kit::rgba(0, 0, 255, 255)};
        const char* names[3] = {"the first texture before aliasing", "the aliasing texture after its barrier and clear", "the first texture reactivated"};
        for (int slot = 0; slot < 3; ++slot) {
            TexelImage image;
            D3d12Tools::tight_rows(footprint, rows, row_size, static_cast<const std::uint8_t*>(mapped) + static_cast<std::size_t>(slot) * total, image);
            std::string where;
            if (!Kit::uniform(image, expected[slot], where)) {
                problems.add(std::string(names[slot]) + ": " + where);
            }
        }
        readback->Unmap(0, nullptr);
        ID3D12Heap* buffer_heap = create_heap(rig, heap_desc(kMiB, D3D12_HEAP_TYPE_DEFAULT, Kit::buffer_heap_flags(rig)), &made);
        if (buffer_heap == nullptr) {
            problems.add("creating the buffer heap failed: " + Kit::hr(made));
            return {};
        }
        const D3D12_RESOURCE_DESC buffer_desc = D3d12Tools::buffer_desc(256 * kKiB);
        ID3D12Resource* ba = place(rig, buffer_heap, 0, buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, &made);
        ID3D12Resource* bb = place(rig, buffer_heap, 0, buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, &made);
        if (ba == nullptr || bb == nullptr) {
            problems.add("placing two buffers on the same range failed: " + Kit::hr(made));
            return {};
        }
        const std::vector<std::uint8_t> data_a = Kit::bytes(21, 256 * kKiB);
        const std::vector<std::uint8_t> data_b = Kit::bytes(22, 256 * kKiB);
        std::vector<std::uint8_t> back_a;
        std::vector<std::uint8_t> back_b;
        if (!D3d12Tools::write_buffer(rig, ba, D3D12_RESOURCE_STATE_COPY_DEST, 0, data_a.data(), data_a.size(), error) ||
            !D3d12Tools::read_buffer(rig, ba, D3D12_RESOURCE_STATE_COPY_DEST, 0, data_a.size(), back_a, error)) {
            problems.add("buffer aliasing, first buffer: " + error);
            return {};
        }
        static_cast<void>(Kit::same_bytes(context, problems, "first aliased buffer", data_a.data(), back_a.data(), data_a.size()));
        if (!rig.direct().run_wait(
                [&](ID3D12GraphicsCommandList* list) {
                    const D3D12_RESOURCE_BARRIER barrier = D3d12Tools::aliasing_barrier(ba, bb);
                    list->ResourceBarrier(1, &barrier);
                },
                error) ||
            !D3d12Tools::write_buffer(rig, bb, D3D12_RESOURCE_STATE_COPY_DEST, 0, data_b.data(), data_b.size(), error) ||
            !D3d12Tools::read_buffer(rig, bb, D3D12_RESOURCE_STATE_COPY_DEST, 0, data_b.size(), back_b, error)) {
            problems.add("buffer aliasing, second buffer: " + error);
            return {};
        }
        static_cast<void>(Kit::same_bytes(context, problems, "second aliased buffer", data_b.data(), back_b.data(), data_b.size()));
        return {};
    });
}

Outcome HeapChecks::reserved_tiles(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        ID3D12Device* d = rig.device();
        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        static_cast<void>(D3d12Tools::feature(d, D3D12_FEATURE_D3D12_OPTIONS, options));
        const UINT64 tile = 64 * kKiB;
        const D3D12_RESOURCE_DESC reserved_desc = D3d12Tools::buffer_desc(8 * tile);
        Com<ID3D12Resource> first;
        const HRESULT created = d->CreateReservedResource(&reserved_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), first.put_void());
        context.measure("d3d12.tiled_tier", static_cast<int>(options.TiledResourcesTier));
        if (options.TiledResourcesTier == D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED) {
            if (SUCCEEDED(created)) {
                problems.add("TiledResourcesTier is NOT_SUPPORTED but CreateReservedResource succeeded");
            } else {
                context.note("CreateReservedResource without tiled resources returns " + Kit::hr(created));
            }
            return {};
        }
        if (!Kit::succeeds(problems, "CreateReservedResource", created)) {
            return {};
        }
        ID3D12Resource* ra = rig.hold(std::move(first));
        Com<ID3D12Resource> second;
        if (!Kit::succeeds(problems, "CreateReservedResource (second)", d->CreateReservedResource(&reserved_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), second.put_void()))) {
            return {};
        }
        ID3D12Resource* rb = rig.hold(std::move(second));
        HRESULT made = S_OK;
        ID3D12Heap* heap = create_heap(rig, heap_desc(16 * tile, D3D12_HEAP_TYPE_DEFAULT, Kit::buffer_heap_flags(rig), tile), &made);
        if (heap == nullptr) {
            problems.add("creating the tile pool failed: " + Kit::hr(made));
            return {};
        }
        D3d12Queue& queue = rig.direct();
        auto map_tiles = [&](ID3D12Resource* resource, const std::vector<UINT>& heap_tiles, const std::vector<bool>& null_tiles) {
            std::vector<D3D12_TILED_RESOURCE_COORDINATE> coordinates(heap_tiles.size());
            std::vector<D3D12_TILE_REGION_SIZE> sizes(heap_tiles.size());
            std::vector<D3D12_TILE_RANGE_FLAGS> flags(heap_tiles.size());
            std::vector<UINT> counts(heap_tiles.size(), 1);
            for (std::size_t i = 0; i < heap_tiles.size(); ++i) {
                coordinates[i] = {static_cast<UINT>(i), 0, 0, 0};
                sizes[i] = {1, FALSE, 1, 1, 1};
                flags[i] = null_tiles[i] ? D3D12_TILE_RANGE_FLAG_NULL : D3D12_TILE_RANGE_FLAG_NONE;
            }
            queue.queue()->UpdateTileMappings(resource, static_cast<UINT>(heap_tiles.size()), coordinates.data(), sizes.data(), heap,
                                              static_cast<UINT>(heap_tiles.size()), flags.data(), heap_tiles.data(), counts.data(), D3D12_TILE_MAPPING_FLAG_NONE);
            queue.touch();
        };
        map_tiles(ra, {4, 5, 6, 7, 0, 1, 4, 0}, {false, false, false, false, false, false, false, true});
        std::vector<std::vector<std::uint8_t>> tile_data;
        for (int i = 0; i < 6; ++i) {
            tile_data.push_back(Kit::bytes(40 + static_cast<std::uint64_t>(i), static_cast<std::size_t>(tile)));
        }
        std::vector<std::uint8_t> all_tiles;
        for (const auto& data : tile_data) {
            all_tiles.insert(all_tiles.end(), data.begin(), data.end());
        }
        std::string error;
        if (!D3d12Tools::write_buffer(rig, ra, D3D12_RESOURCE_STATE_COMMON, 0, all_tiles.data(), all_tiles.size(), error)) {
            problems.add("writing through the reserved buffer failed: " + error);
            return {};
        }
        const bool reads_null = options.TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_2;
        const UINT read_tiles = reads_null ? 8 : 7;
        std::vector<std::uint8_t> back;
        if (!D3d12Tools::read_buffer(rig, ra, D3D12_RESOURCE_STATE_COMMON, 0, read_tiles * tile, back, error)) {
            problems.add("reading the reserved buffer failed: " + error);
            return {};
        }
        for (UINT i = 0; i < read_tiles; ++i) {
            const std::vector<std::uint8_t> zero(static_cast<std::size_t>(tile), 0);
            const std::vector<std::uint8_t>& expected = i < 6 ? tile_data[i] : (i == 6 ? tile_data[0] : zero);
            const std::string label = i == 6 ? "tile 6 sharing the heap tile of tile 0" : (i == 7 ? "the null-mapped tile" : "tile " + std::to_string(i));
            static_cast<void>(Kit::same_bytes(context, problems, label, expected.data(), back.data() + i * tile, expected.size()));
        }
        map_tiles(ra, {9, 5, 6, 7, 0, 1, 4, 0}, {false, false, false, false, false, false, false, true});
        std::vector<std::uint8_t> remapped;
        if (!D3d12Tools::read_buffer(rig, ra, D3D12_RESOURCE_STATE_COMMON, 0, 7 * tile, remapped, error)) {
            problems.add("reading after the re-mapping failed: " + error);
            return {};
        }
        const std::vector<std::uint8_t> zero(static_cast<std::size_t>(tile), 0);
        static_cast<void>(Kit::same_bytes(context, problems, "tile 0 re-mapped to a fresh heap tile", zero.data(), remapped.data(), zero.size()));
        static_cast<void>(Kit::same_bytes(context, problems, "tile 6 after tile 0 moved away", tile_data[0].data(), remapped.data() + 6 * tile, tile_data[0].size()));
        static_cast<void>(Kit::same_bytes(context, problems, "tile 1 untouched by the re-mapping", tile_data[1].data(), remapped.data() + tile, tile_data[1].size()));
        const std::vector<std::uint8_t> fresh = Kit::bytes(60, static_cast<std::size_t>(tile));
        std::vector<std::uint8_t> after_write;
        if (!D3d12Tools::write_buffer(rig, ra, D3D12_RESOURCE_STATE_COMMON, 0, fresh.data(), fresh.size(), error) ||
            !D3d12Tools::read_buffer(rig, ra, D3D12_RESOURCE_STATE_COMMON, 0, 7 * tile, after_write, error)) {
            problems.add("writing after the re-mapping failed: " + error);
            return {};
        }
        static_cast<void>(Kit::same_bytes(context, problems, "tile 0 written after the re-mapping", fresh.data(), after_write.data(), fresh.size()));
        static_cast<void>(Kit::same_bytes(context, problems, "tile 6 still holds the old data of heap tile 4", tile_data[0].data(), after_write.data() + 6 * tile, tile_data[0].size()));
        const D3D12_TILED_RESOURCE_COORDINATE from{1, 0, 0, 0};
        const D3D12_TILED_RESOURCE_COORDINATE to{0, 0, 0, 0};
        const D3D12_TILE_REGION_SIZE region{3, FALSE, 3, 1, 1};
        queue.queue()->CopyTileMappings(rb, &to, ra, &from, &region, D3D12_TILE_MAPPING_FLAG_NONE);
        queue.touch();
        std::vector<std::uint8_t> copied;
        if (!D3d12Tools::read_buffer(rig, rb, D3D12_RESOURCE_STATE_COMMON, 0, 3 * tile, copied, error)) {
            problems.add("reading the buffer that received copied mappings failed: " + error);
            return {};
        }
        for (UINT i = 0; i < 3; ++i) {
            static_cast<void>(Kit::same_bytes(context, problems, "copied mapping tile " + std::to_string(i), tile_data[i + 1].data(), copied.data() + i * tile, tile_data[i + 1].size()));
        }
        return {};
    });
}

Outcome HeapChecks::committed_flags(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        struct Case {
            const char* name;
            D3D12_HEAP_FLAGS flags;
            D3D12_RESOURCE_DESC desc;
            bool valid;
            bool zero_check;
            bool zeroed;
        };
        const D3D12_RESOURCE_DESC buffer = D3d12Tools::buffer_desc(256 * kKiB);
        const D3D12_RESOURCE_DESC plain = D3d12Tools::texture_desc(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM);
        const D3D12_RESOURCE_DESC target = D3d12Tools::texture_desc(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        const D3D12_RESOURCE_DESC depth = D3d12Tools::texture_desc(64, 64, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        const D3D12_RESOURCE_DESC unordered = D3d12Tools::texture_desc(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        const D3D12_RESOURCE_DESC target_unordered = D3d12Tools::texture_desc(
            64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        const D3D12_RESOURCE_DESC simultaneous = D3d12Tools::texture_desc(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
        const D3D12_RESOURCE_DESC depth_no_srv = D3d12Tools::texture_desc(
            64, 64, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
        const D3D12_RESOURCE_DESC depth_and_target = D3d12Tools::texture_desc(
            64, 64, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        const D3D12_RESOURCE_DESC deny_without_depth = D3d12Tools::texture_desc(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
        const D3D12_RESOURCE_DESC depth_format_as_color = D3d12Tools::texture_desc(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        const bool tier2 = Kit::heap_tier(rig) == D3D12_RESOURCE_HEAP_TIER_2;
        std::vector<Case> cases = {
            {"buffer, no heap flags", D3D12_HEAP_FLAG_NONE, buffer, true, true, true},
            {"buffer, ALLOW_ONLY_BUFFERS", D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, buffer, true, true, true},
            {"buffer, SHARED", D3D12_HEAP_FLAG_SHARED, buffer, true, true, true},
            {"buffer, CREATE_NOT_ZEROED", D3D12_HEAP_FLAG_CREATE_NOT_ZEROED, buffer, true, false, false},
            {"buffer, DENY_RT_DS_TEXTURES", D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES, buffer, true, false, false},
            {"buffer, DENY_NON_RT_DS_TEXTURES", D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES, buffer, true, false, false},
            {"texture, no heap flags", D3D12_HEAP_FLAG_NONE, plain, true, true, true},
            {"texture, ALLOW_ONLY_NON_RT_DS_TEXTURES", D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES, plain, true, true, true},
            {"texture, DENY_BUFFERS", D3D12_HEAP_FLAG_DENY_BUFFERS, plain, true, false, false},
            {"texture, DENY_RT_DS_TEXTURES", D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES, plain, true, false, false},
            {"render target, ALLOW_ONLY_RT_DS_TEXTURES", D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES, target, true, false, false},
            {"render target, DENY_NON_RT_DS_TEXTURES", D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES, target, true, false, false},
            {"depth, ALLOW_ONLY_RT_DS_TEXTURES", D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES, depth, true, false, false},
            {"depth with DENY_SHADER_RESOURCE", D3D12_HEAP_FLAG_NONE, depth_no_srv, true, false, false},
            {"unordered access texture", D3D12_HEAP_FLAG_NONE, unordered, true, false, false},
            {"render target and unordered access", D3D12_HEAP_FLAG_NONE, target_unordered, true, false, false},
            {"simultaneous access, SHARED", D3D12_HEAP_FLAG_SHARED, simultaneous, true, false, false},
            {"texture, SHARED", D3D12_HEAP_FLAG_SHARED, plain, true, false, false},
            {"buffer, ALLOW_ONLY_NON_RT_DS_TEXTURES", D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES, buffer, false, false, false},
            {"buffer, ALLOW_ONLY_RT_DS_TEXTURES", D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES, buffer, false, false, false},
            {"buffer, DENY_BUFFERS", D3D12_HEAP_FLAG_DENY_BUFFERS, buffer, false, false, false},
            {"texture, ALLOW_ONLY_BUFFERS", D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, plain, false, false, false},
            {"render target, DENY_RT_DS_TEXTURES", D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES, target, false, false, false},
            {"texture, DENY_NON_RT_DS_TEXTURES", D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES, plain, false, false, false},
            {"depth and render target flags together", D3D12_HEAP_FLAG_NONE, depth_and_target, false, false, false},
            {"DENY_SHADER_RESOURCE without DEPTH_STENCIL", D3D12_HEAP_FLAG_NONE, deny_without_depth, false, false, false},
            {"ALLOW_DEPTH_STENCIL on a colour format", D3D12_HEAP_FLAG_NONE, depth_format_as_color, false, false, false},
        };
        if (tier2) {
            cases.push_back({"buffer, ALLOW_ALL_BUFFERS_AND_TEXTURES", D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, buffer, true, true, true});
            cases.push_back({"texture, ALLOW_ALL_BUFFERS_AND_TEXTURES", D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, plain, true, true, true});
        }
        int accepted_invalid = 0;
        int differing_from_warp = 0;
        D3d12Rig warp;
        std::string warp_error;
        const bool have_warp = warp.open_warp(D3D_FEATURE_LEVEL_11_0, warp_error);
        for (const Case& item : cases) {
            HRESULT made = S_OK;
            const bool is_buffer = item.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
            ID3D12Resource* resource = D3d12Tools::committed(rig, D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT), item.flags, item.desc,
                                                             D3D12_RESOURCE_STATE_COMMON, nullptr, &made);
            if (have_warp) {
                HRESULT reference = S_OK;
                ID3D12Resource* twin = D3d12Tools::committed(warp, D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT), item.flags, item.desc, D3D12_RESOURCE_STATE_COMMON, nullptr, &reference);
                if ((twin != nullptr) != (resource != nullptr)) {
                    ++differing_from_warp;
                    context.measure(std::string("d3d12.committed_flags_vs_warp.") + item.name, std::string("accelerated ") + (resource != nullptr ? "accepts" : "rejects with " + Kit::hr(made)) + ", WARP " +
                                                                                                  (twin != nullptr ? "accepts" : "rejects with " + Kit::hr(reference)));
                }
                if (twin != nullptr) {
                    warp.release(twin);
                }
            }
            if (item.valid) {
                if (resource == nullptr) {
                    problems.add(std::string(item.name) + " failed with " + Kit::hr(made));
                    continue;
                }
                if (item.zero_check) {
                    std::string error;
                    if (is_buffer) {
                        std::vector<std::uint8_t> back;
                        if (!D3d12Tools::read_buffer(rig, resource, D3D12_RESOURCE_STATE_COMMON, 0, item.desc.Width, back, error)) {
                            problems.add(std::string(item.name) + ": " + error);
                        } else if (std::any_of(back.begin(), back.end(), [](std::uint8_t v) { return v != 0; })) {
                            problems.add(std::string(item.name) + " reads non-zero data right after creation");
                        }
                    } else {
                        TexelImage image;
                        if (!D3d12Tools::read_texture(rig, resource, D3D12_RESOURCE_STATE_COMMON, 0, image, error)) {
                            problems.add(std::string(item.name) + ": " + error);
                        } else if (std::any_of(image.data.begin(), image.data.end(), [](std::uint8_t v) { return v != 0; })) {
                            problems.add(std::string(item.name) + " reads non-zero data right after creation");
                        }
                    }
                }
                rig.release(resource);
            } else if (resource != nullptr) {
                ++accepted_invalid;
                context.note(std::string(item.name) + " was accepted although the flags contradict each other");
                rig.release(resource);
            } else {
                Kit::expect(problems, item.name, made, {E_INVALIDARG});
            }
        }
        context.measure("d3d12.accepted_contradictory_combinations", accepted_invalid);
        context.measure("d3d12.committed_flags_differing_from_warp", have_warp ? std::to_string(differing_from_warp) : "WARP12 unavailable: " + warp_error);
        return {};
    });
}

Outcome HeapChecks::zeroed_new(Context& context) {
    const std::string child_name = ChildMode::variable(L"LSW_SUITES_ZERO_CHILD");
    if (ChildMode::active() && !child_name.empty()) {
        return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
            std::string error;
            for (int round = 0; round < 3; ++round) {
                ID3D12Resource* dirty = D3d12Tools::device_buffer(rig, 16 * kMiB, D3D12_RESOURCE_STATE_COPY_DEST);
                if (dirty == nullptr) {
                    problems.add("the writer cannot allocate 16 MiB");
                    return {};
                }
                const std::vector<std::uint8_t> data = Kit::bytes(70 + static_cast<std::uint64_t>(round), 16 * kMiB);
                if (!D3d12Tools::write_buffer(rig, dirty, D3D12_RESOURCE_STATE_COPY_DEST, 0, data.data(), data.size(), error)) {
                    problems.add("the writer fails: " + error);
                    return {};
                }
            }
            return {};
        });
    }
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::string error;
        ChildRunOptions options;
        options.timeout_seconds = 120;
        options.env = {{"LSW_SUITES_ZERO_CHILD", "1"}};
        const CheckResult child = ChildRuns::run_self("sanity.d3d12.heap.zeroed_new", options);
        if (child.status != Status::Ok) {
            problems.add("the process that dirtied video memory failed: " + child.reason);
        }
        const UINT64 size = 4 * kMiB;
        for (int round = 0; round < 6; ++round) {
            const std::string label = "round " + std::to_string(round);
            HRESULT made = S_OK;
            ID3D12Heap* heap = create_heap(rig, heap_desc(size, D3D12_HEAP_TYPE_DEFAULT, Kit::buffer_heap_flags(rig)), &made);
            ID3D12Resource* placed = heap != nullptr ? place(rig, heap, 0, D3d12Tools::buffer_desc(size), D3D12_RESOURCE_STATE_COPY_DEST, &made) : nullptr;
            if (placed == nullptr) {
                problems.add(label + ": the placed buffer cannot be created: " + Kit::hr(made));
                return {};
            }
            std::vector<std::uint8_t> back;
            if (!D3d12Tools::read_buffer(rig, placed, D3D12_RESOURCE_STATE_COPY_DEST, 0, size, back, error)) {
                problems.add(label + ": " + error);
                return {};
            }
            if (std::any_of(back.begin(), back.end(), [](std::uint8_t v) { return v != 0; })) {
                problems.add(label + ": a resource placed in a new heap exposes data it never wrote");
            }
            const std::vector<std::uint8_t> data = Kit::bytes(80 + static_cast<std::uint64_t>(round), size);
            if (!D3d12Tools::write_buffer(rig, placed, D3D12_RESOURCE_STATE_COPY_DEST, 0, data.data(), size, error)) {
                problems.add(label + ": " + error);
                return {};
            }
            rig.release(placed);
            rig.release(heap);
            ID3D12Resource* committed = D3d12Tools::device_buffer(rig, size, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE, &made);
            if (committed == nullptr) {
                problems.add(label + ": the committed buffer cannot be created: " + Kit::hr(made));
                return {};
            }
            if (!D3d12Tools::read_buffer(rig, committed, D3D12_RESOURCE_STATE_COPY_DEST, 0, size, back, error)) {
                problems.add(label + ": " + error);
                return {};
            }
            if (std::any_of(back.begin(), back.end(), [](std::uint8_t v) { return v != 0; })) {
                problems.add(label + ": a new committed buffer exposes data from a released resource or from another process");
            }
            rig.release(committed);
        }
        return {};
    });
}

Outcome HeapChecks::exhaustion(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT va{};
        static_cast<void>(D3d12Tools::feature(rig.device(), D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, va));
        const UINT64 local = Kit::device_local_budget(rig);
        const UINT64 host = Kit::host_budget(rig);
        UINT64 request = (local + host) * 2;
        const UINT64 limit = va.MaxGPUVirtualAddressBitsPerResource >= 63 ? ~0ull : (1ull << va.MaxGPUVirtualAddressBitsPerResource);
        if (request == 0 || request >= limit) {
            return "the budget is unknown or the request would exceed the per-resource address space";
        }
        HRESULT made = S_OK;
        ID3D12Resource* huge = D3d12Tools::device_buffer(rig, request, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_NONE, &made);
        if (huge != nullptr) {
            problems.add("a " + std::to_string(request >> 20) + " MiB buffer, twice the local and host budgets together, was created");
            rig.release(huge);
        } else {
            Kit::expect(problems, "creating a buffer twice the size of all video memory", made, {E_OUTOFMEMORY});
        }
        if (rig.removed_reason() != S_OK) {
            problems.add("the device was removed by the failed allocation: " + Kit::hr(rig.removed_reason()));
        }
        std::vector<ID3D12Resource*> held;
        const UINT64 chunk = 256 * kMiB;
        UINT64 total = 0;
        HRESULT last = S_OK;
        while (total + chunk <= local && held.size() < 64) {
            ID3D12Resource* part = D3d12Tools::device_buffer(rig, chunk, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_NONE, &last);
            if (part == nullptr) {
                Kit::expect(problems, "a 256 MiB allocation within the budget failing", last, {E_OUTOFMEMORY});
                break;
            }
            held.push_back(part);
            total += chunk;
        }
        context.measure("d3d12.allocated_before_release", static_cast<long long>(total));
        for (ID3D12Resource* part : held) {
            rig.release(part);
        }
        std::string error;
        if (!rig.idle(error)) {
            problems.add(error);
        }
        ID3D12Resource* again = D3d12Tools::device_buffer(rig, 64 * kMiB, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_NONE, &made);
        if (again == nullptr) {
            problems.add("a 64 MiB allocation after the releases failed: " + Kit::hr(made));
        } else {
            rig.release(again);
        }
        if (rig.removed_reason() != S_OK) {
            problems.add("the device is removed after the exhaustion test: " + Kit::hr(rig.removed_reason()));
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::d3d12
