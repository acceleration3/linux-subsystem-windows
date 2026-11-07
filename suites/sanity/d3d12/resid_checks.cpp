#include "sanity/d3d12/resid_checks.hpp"

#include "sanity/d3d12/kit.hpp"
#include "support/dxgi_budget.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::sanity::d3d12 {

namespace {

constexpr UINT64 kMiB = 1024 * 1024;

bool check_pattern(D3d12Rig& rig, Context& context, Problems& problems, const std::string& label, ID3D12Resource* resource, D3D12_RESOURCE_STATES state, UINT64 offset,
                   UINT64 size, std::uint64_t seed) {
    std::vector<std::uint8_t> back;
    std::string error;
    if (!D3d12Tools::read_buffer(rig, resource, state, offset, size, back, error)) {
        problems.add(label + ": " + error);
        return false;
    }
    const std::vector<std::uint8_t> expected = Kit::bytes(seed, static_cast<std::size_t>(size));
    return Kit::same_bytes(context, problems, label, expected.data(), back.data(), expected.size());
}

}  // namespace

Outcome ResidChecks::make_evict(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        ID3D12Device* d = rig.device();
        std::string error;
        const UINT64 size = 16 * kMiB;
        D3D12_HEAP_DESC heap_desc{};
        heap_desc.SizeInBytes = size;
        heap_desc.Properties = D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT);
        heap_desc.Flags = Kit::buffer_heap_flags(rig);
        Com<ID3D12Heap> heap_owner;
        if (!Kit::succeeds(problems, "CreateHeap", d->CreateHeap(&heap_desc, __uuidof(ID3D12Heap), heap_owner.put_void()))) {
            return {};
        }
        ID3D12Heap* heap = rig.hold(std::move(heap_owner));
        Com<ID3D12Resource> placed_owner;
        const D3D12_RESOURCE_DESC placed_desc = D3d12Tools::buffer_desc(size);
        if (!Kit::succeeds(problems, "CreatePlacedResource", d->CreatePlacedResource(heap, 0, &placed_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                                                    __uuidof(ID3D12Resource), placed_owner.put_void()))) {
            return {};
        }
        ID3D12Resource* placed = rig.hold(std::move(placed_owner));
        ID3D12Resource* committed = D3d12Tools::device_buffer(rig, size, D3D12_RESOURCE_STATE_COPY_DEST);
        if (committed == nullptr) {
            problems.add("creating the committed buffer failed");
            return {};
        }
        const std::vector<std::uint8_t> data_placed = Kit::bytes(1301, static_cast<std::size_t>(size));
        const std::vector<std::uint8_t> data_committed = Kit::bytes(1302, static_cast<std::size_t>(size));
        if (!D3d12Tools::write_buffer(rig, placed, D3D12_RESOURCE_STATE_COPY_DEST, 0, data_placed.data(), size, error) ||
            !D3d12Tools::write_buffer(rig, committed, D3D12_RESOURCE_STATE_COPY_DEST, 0, data_committed.data(), size, error)) {
            problems.add("filling the buffers failed: " + error);
            return {};
        }
        if (!rig.idle(error)) {
            problems.add(error);
            return {};
        }
        ID3D12Pageable* both[2] = {heap, committed};
        Kit::succeeds(problems, "Evict(heap, committed buffer)", d->Evict(2, both));
        Kit::succeeds(problems, "MakeResident(heap, committed buffer)", d->MakeResident(2, both));
        check_pattern(rig, context, problems, "placed buffer after Evict and MakeResident", placed, D3D12_RESOURCE_STATE_COPY_DEST, 0, size, 1301);
        check_pattern(rig, context, problems, "committed buffer after Evict and MakeResident", committed, D3D12_RESOURCE_STATE_COPY_DEST, 0, size, 1302);
        Com<ID3D12Device3> device3 = rig.as<ID3D12Device3>();
        if (device3) {
            Com<ID3D12Fence> fence_owner;
            if (Kit::succeeds(problems, "CreateFence", d->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), fence_owner.put_void()))) {
                ID3D12Fence* fence = rig.hold(std::move(fence_owner));
                UINT64 value = 0;
                for (D3D12_RESIDENCY_FLAGS flags : {D3D12_RESIDENCY_FLAG_NONE, D3D12_RESIDENCY_FLAG_DENY_OVERBUDGET}) {
                    Kit::succeeds(problems, "Evict before EnqueueMakeResident", d->Evict(2, both));
                    ++value;
                    Kit::succeeds(problems, "EnqueueMakeResident", device3->EnqueueMakeResident(flags, 2, both, fence, value));
                    HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
                    fence->SetEventOnCompletion(value, event);
                    const DWORD waited = ::WaitForSingleObject(event, 20000);
                    ::CloseHandle(event);
                    if (waited != WAIT_OBJECT_0) {
                        problems.add("the fence of EnqueueMakeResident was not signalled");
                        return {};
                    }
                    check_pattern(rig, context, problems, "placed buffer after EnqueueMakeResident", placed, D3D12_RESOURCE_STATE_COPY_DEST, 0, size, 1301);
                    check_pattern(rig, context, problems, "committed buffer after EnqueueMakeResident", committed, D3D12_RESOURCE_STATE_COPY_DEST, 0, size, 1302);
                }
            }
        } else {
            context.note("ID3D12Device3 is not exposed; EnqueueMakeResident is not exercised");
        }
        Com<ID3D12Device1> device1 = rig.as<ID3D12Device1>();
        if (device1) {
            const D3D12_RESIDENCY_PRIORITY priorities[] = {D3D12_RESIDENCY_PRIORITY_MINIMUM, D3D12_RESIDENCY_PRIORITY_LOW, D3D12_RESIDENCY_PRIORITY_NORMAL,
                                                           D3D12_RESIDENCY_PRIORITY_HIGH};
            for (D3D12_RESIDENCY_PRIORITY priority : priorities) {
                ID3D12Pageable* objects[2] = {heap, committed};
                const D3D12_RESIDENCY_PRIORITY values[2] = {priority, priority};
                Kit::succeeds(problems, "SetResidencyPriority " + std::to_string(static_cast<unsigned>(priority)), device1->SetResidencyPriority(2, objects, values));
            }
            check_pattern(rig, context, problems, "committed buffer after changing residency priorities", committed, D3D12_RESOURCE_STATE_COPY_DEST, 0, size, 1302);
        } else {
            context.note("ID3D12Device1 is not exposed; SetResidencyPriority is not exercised");
        }
        const UINT64 budget = Kit::device_local_budget(rig);
        if (budget < 1024 * kMiB) {
            context.note("the budget is unknown or below 1 GiB; MakeResident beyond the budget is not exercised");
            return {};
        }
        const UINT64 chunk = 256 * kMiB;
        std::vector<ID3D12Resource*> first;
        std::vector<ID3D12Resource*> second;
        HRESULT made = S_OK;
        for (UINT64 total = 0; total + chunk <= budget * 6 / 10; total += chunk) {
            ID3D12Resource* resource = D3d12Tools::device_buffer(rig, chunk, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_NONE, &made);
            if (resource == nullptr) {
                problems.add("allocating the first group failed: " + Kit::hr(made));
                break;
            }
            first.push_back(resource);
        }
        std::vector<ID3D12Pageable*> first_objects(first.begin(), first.end());
        if (!first_objects.empty()) {
            Kit::succeeds(problems, "Evict(first group)", d->Evict(static_cast<UINT>(first_objects.size()), first_objects.data()));
        }
        for (UINT64 total = 0; total + chunk <= budget * 6 / 10; total += chunk) {
            ID3D12Resource* resource = D3d12Tools::device_buffer(rig, chunk, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_NONE, &made);
            if (resource == nullptr) {
                problems.add("allocating the second group failed: " + Kit::hr(made));
                break;
            }
            second.push_back(resource);
        }
        if (!first_objects.empty()) {
            const HRESULT over = d->MakeResident(static_cast<UINT>(first_objects.size()), first_objects.data());
            context.measure("d3d12.make_resident_over_budget", Kit::hr(over));
            if (FAILED(over)) {
                Kit::expect(problems, "MakeResident of both groups beyond the budget", over, {E_OUTOFMEMORY});
            }
            if (rig.removed_reason() != S_OK) {
                problems.add("MakeResident beyond the budget removed the device: " + Kit::hr(rig.removed_reason()));
            }
            std::vector<ID3D12Pageable*> second_objects(second.begin(), second.end());
            if (!second_objects.empty()) {
                Kit::succeeds(problems, "Evict(second group)", d->Evict(static_cast<UINT>(second_objects.size()), second_objects.data()));
            }
            Kit::succeeds(problems, "MakeResident(first group) after evicting the second", d->MakeResident(static_cast<UINT>(first_objects.size()), first_objects.data()));
        }
        for (ID3D12Resource* resource : first) {
            rig.release(resource);
        }
        for (ID3D12Resource* resource : second) {
            rig.release(resource);
        }
        return {};
    });
}

Outcome ResidChecks::budget_pressure(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        ID3D12Device* d = rig.device();
        std::string error;
        VideoMemory memory;
        if (!DxgiBudget::query(rig.luid, true, memory, error) || memory.budget == 0) {
            return "the video memory budget is not reported: " + error;
        }
        const UINT64 chunk = 128 * kMiB;
        const UINT64 resident_limit = std::max<UINT64>(2, memory.budget / 2 / chunk);
        const UINT64 target = std::min<UINT64>(memory.budget * 3 / 2 / chunk + 1, 64);
        struct Chunk {
            ID3D12Resource* resource = nullptr;
            bool resident = true;
        };
        std::vector<Chunk> chunks;
        UINT64 resident = 0;
        constexpr UINT64 kEnds = 4 * kMiB;
        for (UINT64 i = 0; i < target; ++i) {
            HRESULT made = S_OK;
            ID3D12Resource* resource = D3d12Tools::device_buffer(rig, chunk, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE, &made);
            if (resource == nullptr) {
                Kit::expect(problems, "allocating chunk " + std::to_string(i), made, {E_OUTOFMEMORY});
                break;
            }
            const std::vector<std::uint8_t> head = Kit::bytes(2000 + i, static_cast<std::size_t>(kEnds));
            const std::vector<std::uint8_t> tail = Kit::bytes(3000 + i, static_cast<std::size_t>(kEnds));
            if (!D3d12Tools::write_buffer(rig, resource, D3D12_RESOURCE_STATE_COPY_DEST, 0, head.data(), kEnds, error) ||
                !D3d12Tools::write_buffer(rig, resource, D3D12_RESOURCE_STATE_COPY_DEST, chunk - kEnds, tail.data(), kEnds, error)) {
                problems.add("writing chunk " + std::to_string(i) + ": " + error);
                break;
            }
            chunks.push_back({resource, true});
            ++resident;
            if (!rig.idle(error)) {
                problems.add(error);
                return {};
            }
            for (std::size_t j = 0; j < chunks.size() && resident > resident_limit; ++j) {
                if (chunks[j].resident) {
                    ID3D12Pageable* object = chunks[j].resource;
                    Kit::succeeds(problems, "Evict chunk " + std::to_string(j), d->Evict(1, &object));
                    chunks[j].resident = false;
                    --resident;
                }
            }
        }
        context.measure("d3d12.chunks", static_cast<long long>(chunks.size()));
        for (std::size_t i = 0; i < chunks.size(); ++i) {
            if (!chunks[i].resident) {
                for (std::size_t j = 0; j < chunks.size() && resident + 1 > resident_limit; ++j) {
                    if (chunks[j].resident) {
                        ID3D12Pageable* object = chunks[j].resource;
                        Kit::succeeds(problems, "Evict chunk " + std::to_string(j), d->Evict(1, &object));
                        chunks[j].resident = false;
                        --resident;
                    }
                }
                ID3D12Pageable* object = chunks[i].resource;
                if (!Kit::succeeds(problems, "MakeResident chunk " + std::to_string(i), d->MakeResident(1, &object))) {
                    continue;
                }
                chunks[i].resident = true;
                ++resident;
            }
            check_pattern(rig, context, problems, "head of chunk " + std::to_string(i), chunks[i].resource, D3D12_RESOURCE_STATE_COPY_DEST, 0, kEnds, 2000 + i);
            check_pattern(rig, context, problems, "tail of chunk " + std::to_string(i), chunks[i].resource, D3D12_RESOURCE_STATE_COPY_DEST, chunk - kEnds, kEnds, 3000 + i);
            if (!problems.empty()) {
                break;
            }
        }
        VideoMemory after;
        if (DxgiBudget::query(rig.luid, true, after, error)) {
            context.measure("d3d12.usage_end", static_cast<long long>(after.usage));
            context.measure("d3d12.budget_end", static_cast<long long>(after.budget));
            if (after.usage > after.budget) {
                problems.add("QueryVideoMemoryInfo reports a usage of " + std::to_string(after.usage >> 20) + " MiB above the budget of " + std::to_string(after.budget >> 20) + " MiB");
            }
        }
        for (const Chunk& item : chunks) {
            rig.release(item.resource);
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::d3d12
