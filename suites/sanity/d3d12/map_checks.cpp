#include "sanity/d3d12/map_checks.hpp"

#include "sanity/d3d12/kit.hpp"

#include <cstring>

namespace lsw::suites::sanity::d3d12 {

namespace {

constexpr UINT64 kSlot = 16 * 1024;

}  // namespace

Outcome MapChecks::persistent(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        constexpr UINT kSlots = 4;
        ID3D12Resource* upload = D3d12Tools::upload(rig, kSlot * kSlots);
        ID3D12Resource* readback = D3d12Tools::readback(rig, kSlot * kSlots);
        if (upload == nullptr || readback == nullptr) {
            problems.add("creating the buffers failed");
            return {};
        }
        void* up = nullptr;
        void* down = nullptr;
        const D3D12_RANGE none{0, 0};
        const D3D12_RANGE all{0, static_cast<SIZE_T>(kSlot * kSlots)};
        if (!Kit::succeeds(problems, "Map(upload)", upload->Map(0, &none, &up)) || !Kit::succeeds(problems, "Map(readback)", readback->Map(0, &all, &down))) {
            return {};
        }
        void* nested = nullptr;
        if (Kit::succeeds(problems, "nested Map(upload)", upload->Map(0, &none, &nested)) && nested != up) {
            problems.add("a nested Map of an upload buffer returns another pointer");
        }
        void* nested_down = nullptr;
        if (Kit::succeeds(problems, "nested Map(readback)", readback->Map(0, &all, &nested_down)) && nested_down != down) {
            problems.add("a nested Map of a readback buffer returns another pointer");
        }
        upload->Unmap(0, nullptr);
        readback->Unmap(0, &none);
        std::string error;
        std::vector<std::uint8_t> expected(static_cast<std::size_t>(kSlot));
        for (int i = 0; i < 100; ++i) {
            const UINT64 slot = static_cast<UINT64>(i) % kSlots;
            Pattern::fill(static_cast<std::uint64_t>(i) + 1000, 0, expected.data(), expected.size());
            std::memcpy(static_cast<std::uint8_t*>(up) + slot * kSlot, expected.data(), expected.size());
            if (!rig.direct().run_wait([&](ID3D12GraphicsCommandList* list) { list->CopyBufferRegion(readback, slot * kSlot, upload, slot * kSlot, kSlot); }, error)) {
                problems.add("iteration " + std::to_string(i) + ": " + error);
                return {};
            }
            if (std::memcmp(static_cast<std::uint8_t*>(down) + slot * kSlot, expected.data(), expected.size()) != 0) {
                problems.add("iteration " + std::to_string(i) + ": the persistent mappings do not carry the data of the submission");
                break;
            }
        }
        upload->Unmap(0, nullptr);
        readback->Unmap(0, &none);
        void* again = nullptr;
        if (Kit::succeeds(problems, "Map after the last Unmap", upload->Map(0, &none, &again))) {
            if (std::memcmp(static_cast<std::uint8_t*>(again) + 3 * kSlot, expected.data(), expected.size()) != 0) {
                problems.add("the data written through the first mapping is not there after remapping the upload buffer");
            }
            upload->Unmap(0, nullptr);
        }
        return {};
    });
}

Outcome MapChecks::upload_visible(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        const UINT64 size = 64 * 1024;
        ID3D12Resource* upload = D3d12Tools::upload(rig, size);
        ID3D12Resource* readback = D3d12Tools::readback(rig, size);
        if (upload == nullptr || readback == nullptr) {
            problems.add("creating the buffers failed");
            return {};
        }
        void* up = nullptr;
        const D3D12_RANGE none{0, 0};
        if (!Kit::succeeds(problems, "Map(upload)", upload->Map(0, &none, &up))) {
            return {};
        }
        std::string error;
        std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
        for (int i = 0; i < 40; ++i) {
            Pattern::fill(static_cast<std::uint64_t>(i) + 2000, 0, data.data(), data.size());
            std::memcpy(up, data.data(), data.size());
            std::vector<std::uint8_t> back;
            if (!rig.direct().run_wait([&](ID3D12GraphicsCommandList* list) { list->CopyBufferRegion(readback, 0, upload, 0, size); }, error) ||
                !D3d12Tools::map_bytes(readback, 0, size, back, error)) {
                problems.add("submission " + std::to_string(i) + ": " + error);
                return {};
            }
            if (std::memcmp(back.data(), data.data(), data.size()) != 0) {
                problems.add("a write after a fence wait and before the next submit is not visible in submission " + std::to_string(i));
                break;
            }
        }
        const std::vector<std::uint8_t> late = Kit::bytes(77, static_cast<std::size_t>(size));
        ID3D12CommandList* recorded = rig.direct().record([&](ID3D12GraphicsCommandList* list) { list->CopyBufferRegion(readback, 0, upload, 0, size); }, error);
        if (recorded == nullptr) {
            problems.add("recording failed: " + error);
            return {};
        }
        std::memcpy(up, late.data(), late.size());
        ID3D12CommandList* lists[] = {recorded};
        const std::uint64_t value = rig.direct().execute(lists, 1);
        std::vector<std::uint8_t> back;
        if (!rig.direct().wait(value, error) || !D3d12Tools::map_bytes(readback, 0, size, back, error)) {
            problems.add(error);
            return {};
        }
        static_cast<void>(Kit::same_bytes(context, problems, "CPU write between recording and ExecuteCommandLists", late.data(), back.data(), late.size()));
        const std::vector<std::uint8_t> partial = Kit::bytes(78, 1000);
        std::memcpy(static_cast<std::uint8_t*>(up) + 4093, partial.data(), partial.size());
        if (!rig.direct().run_wait([&](ID3D12GraphicsCommandList* list) { list->CopyBufferRegion(readback, 0, upload, 4093, partial.size()); }, error) ||
            !D3d12Tools::map_bytes(readback, 0, partial.size(), back, error)) {
            problems.add(error);
            return {};
        }
        static_cast<void>(Kit::same_bytes(context, problems, "unaligned CPU write", partial.data(), back.data(), partial.size()));
        upload->Unmap(0, nullptr);
        return {};
    });
}

Outcome MapChecks::readback_after_fence(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        const UINT64 size = 256 * 1024;
        ID3D12Resource* staging = D3d12Tools::device_buffer(rig, size, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* readback = D3d12Tools::readback(rig, size);
        if (staging == nullptr || readback == nullptr) {
            problems.add("creating the buffers failed");
            return {};
        }
        std::string error;
        for (int i = 0; i < 20; ++i) {
            const std::vector<std::uint8_t> data = Kit::bytes(300 + static_cast<std::uint64_t>(i), static_cast<std::size_t>(size));
            std::uint64_t value = 0;
            if (!D3d12Tools::write_buffer(rig, staging, D3D12_RESOURCE_STATE_COPY_DEST, 0, data.data(), data.size(), error) ||
                !rig.direct().run(
                    [&](ID3D12GraphicsCommandList* list) {
                        D3d12Tools::move(list, staging, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
                        list->CopyBufferRegion(readback, 0, staging, 0, size);
                        D3d12Tools::move(list, staging, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                    },
                    value, error)) {
                problems.add("iteration " + std::to_string(i) + ": " + error);
                return {};
            }
            HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
            Kit::succeeds(problems, "SetEventOnCompletion", rig.direct().fence()->SetEventOnCompletion(value, event));
            const DWORD waited = ::WaitForSingleObject(event, 20000);
            ::CloseHandle(event);
            if (waited != WAIT_OBJECT_0) {
                problems.add("the fence of the copy was not signalled");
                return {};
            }
            if (i % 3 == 0) {
                void* mapped = nullptr;
                const D3D12_RANGE whole{0, static_cast<SIZE_T>(size)};
                if (Kit::succeeds(problems, "Map(whole range)", readback->Map(0, &whole, &mapped))) {
                    static_cast<void>(Kit::same_bytes(context, problems, "readback of iteration " + std::to_string(i), data.data(), mapped, data.size()));
                    readback->Unmap(0, nullptr);
                }
            } else if (i % 3 == 1) {
                void* mapped = nullptr;
                const D3D12_RANGE partial{16, 1016};
                if (Kit::succeeds(problems, "Map(partial range)", readback->Map(0, &partial, &mapped))) {
                    static_cast<void>(Kit::same_bytes(context, problems, "partial readback of iteration " + std::to_string(i), data.data() + 16,
                                                      static_cast<std::uint8_t*>(mapped) + 16, 1000));
                    readback->Unmap(0, &partial);
                }
            } else {
                void* mapped = nullptr;
                const D3D12_RANGE empty{0, 0};
                if (Kit::succeeds(problems, "Map(empty read range)", readback->Map(0, &empty, &mapped)) && mapped == nullptr) {
                    problems.add("Map with an empty read range returned a null pointer");
                }
                readback->Unmap(0, &empty);
                void* whole = nullptr;
                if (Kit::succeeds(problems, "Map(null range)", readback->Map(0, nullptr, &whole))) {
                    static_cast<void>(Kit::same_bytes(context, problems, "null-range readback of iteration " + std::to_string(i), data.data(), whole, data.size()));
                    readback->Unmap(0, nullptr);
                }
            }
            if (!problems.empty()) {
                break;
            }
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::d3d12
