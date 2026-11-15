#include "sanity/d3d12/queue_checks.hpp"

#include "sanity/d3d12/kit.hpp"

#include <cstring>

namespace lsw::suites::sanity::d3d12 {

namespace {

const char* kComputeFill =
    "RWStructuredBuffer<uint> o : register(u0);\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) { o[id.x] = id.x * 7u + 3u; }\n";

const char* type_name(D3D12_COMMAND_LIST_TYPE type) {
    switch (type) {
    case D3D12_COMMAND_LIST_TYPE_DIRECT: return "direct";
    case D3D12_COMMAND_LIST_TYPE_COMPUTE: return "compute";
    case D3D12_COMMAND_LIST_TYPE_COPY: return "copy";
    default: return "other";
    }
}

bool copy_through(D3d12Rig& rig, D3d12Queue& queue, D3D12_COMMAND_LIST_TYPE list_type, std::uint64_t seed, std::string& error) {
    const UINT64 size = 64 * 1024;
    ID3D12Resource* upload = D3d12Tools::upload(rig, size);
    ID3D12Resource* readback = D3d12Tools::readback(rig, size);
    if (upload == nullptr || readback == nullptr) {
        error = "creating the staging buffers failed";
        return false;
    }
    const std::vector<std::uint8_t> data = Kit::bytes(seed, static_cast<std::size_t>(size));
    void* mapped = nullptr;
    const D3D12_RANGE none{0, 0};
    if (FAILED(upload->Map(0, &none, &mapped))) {
        error = "Map failed";
        return false;
    }
    std::memcpy(mapped, data.data(), data.size());
    upload->Unmap(0, nullptr);
    ID3D12CommandList* list = queue.record_as(list_type, [&](ID3D12GraphicsCommandList* l) { l->CopyBufferRegion(readback, 0, upload, 0, size); }, error);
    if (list == nullptr) {
        return false;
    }
    const std::uint64_t value = queue.execute(&list, 1);
    std::vector<std::uint8_t> back;
    if (!queue.wait(value, error) || !D3d12Tools::map_bytes(readback, 0, size, back, error)) {
        return false;
    }
    if (std::memcmp(back.data(), data.data(), data.size()) != 0) {
        error = "the copied data differs";
        return false;
    }
    return true;
}

}  // namespace

Outcome QueueChecks::types(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::string error;
        if (!rig.open_queues(error)) {
            problems.add(error);
            return {};
        }
        for (D3D12_COMMAND_LIST_TYPE type : {D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_TYPE_COPY}) {
            D3d12Queue& queue = rig.queue(type);
            const D3D12_COMMAND_QUEUE_DESC desc = queue.queue()->GetDesc();
            if (desc.Type != type) {
                problems.add(std::string(type_name(type)) + " queue reports another type in GetDesc");
            }
            if (!copy_through(rig, queue, type, 600 + static_cast<std::uint64_t>(type), error)) {
                problems.add(std::string("a copy list of the ") + type_name(type) + " type on its own queue: " + error);
            }
        }
        D3d12Root root;
        root.uav(0);
        ID3D12RootSignature* signature = root.build(rig, error);
        ID3D12PipelineState* state = signature != nullptr ? D3d12Tools::compute_from(rig, signature, kComputeFill, error) : nullptr;
        ID3D12Resource* output = D3d12Tools::device_buffer(rig, 256 * 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ID3D12Resource* readback = D3d12Tools::readback(rig, 256 * 4);
        if (state == nullptr || output == nullptr || readback == nullptr) {
            problems.add("compute setup failed: " + error);
            return {};
        }
        for (D3D12_COMMAND_LIST_TYPE queue_type : {D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_TYPE_DIRECT}) {
            D3d12Queue& queue = rig.queue(queue_type);
            std::vector<std::uint8_t> raw;
            const bool done = queue.run_wait(
                [&](ID3D12GraphicsCommandList* list) {
                    list->SetComputeRootSignature(signature);
                    list->SetPipelineState(state);
                    list->SetComputeRootUnorderedAccessView(0, output->GetGPUVirtualAddress());
                    list->Dispatch(4, 1, 1);
                    D3d12Tools::move(list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
                    list->CopyBufferRegion(readback, 0, output, 0, 256 * 4);
                    D3d12Tools::move(list, output, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                },
                error);
            if (!done || !D3d12Tools::map_bytes(readback, 0, 256 * 4, raw, error)) {
                problems.add(std::string("a dispatch on the ") + type_name(queue_type) + " queue: " + error);
                continue;
            }
            std::vector<std::uint32_t> expected(256);
            for (std::uint32_t i = 0; i < 256; ++i) {
                expected[i] = i * 7u + 3u;
            }
            Kit::compare_words(context, problems, std::string("dispatch on the ") + type_name(queue_type) + " queue", expected, Kit::words(raw));
        }
        {
            D3d12Frame frame;
            D3d12Root empty;
            ID3D12RootSignature* graphics_root = empty.build(rig, error);
            ID3D12PipelineState* draw = graphics_root != nullptr ? Kit::pipeline(rig, graphics_root, Kit::kSolidPs, DXGI_FORMAT_R8G8B8A8_UNORM, error) : nullptr;
            TexelImage image;
            if (draw == nullptr || !frame.open(rig, 16, 16, error) ||
                !frame.run([&](ID3D12GraphicsCommandList* list) { Kit::draw_fullscreen(list, graphics_root, draw); }, error) || !frame.read(image, error)) {
                problems.add("a draw on the direct queue: " + error);
            } else {
                std::string where;
                if (!Kit::uniform(image, Kit::solid_color(), where)) {
                    problems.add("draw on the direct queue: " + where);
                }
            }
        }
        D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY priority{};
        priority.CommandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
        priority.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
        if (SUCCEEDED(rig.device()->CheckFeatureSupport(D3D12_FEATURE_COMMAND_QUEUE_PRIORITY, &priority, sizeof(priority))) && priority.PriorityForTypeIsSupported) {
            D3d12Queue high;
            if (!high.open(rig.device(), D3D12_COMMAND_LIST_TYPE_DIRECT, error, D3D12_COMMAND_QUEUE_PRIORITY_HIGH)) {
                problems.add("a high priority direct queue is reported as supported but cannot be created: " + error);
            } else if (!copy_through(rig, high, D3D12_COMMAND_LIST_TYPE_DIRECT, 620, error)) {
                problems.add("work on the high priority queue: " + error);
            }
        }
        if (rig.removed_reason() != S_OK) {
            problems.add("the device was removed: " + Kit::hr(rig.removed_reason()));
        }
        return {};
    });
}

Outcome QueueChecks::many_lists(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::string error;
        ID3D12Resource* upload = D3d12Tools::upload(rig, 256);
        ID3D12Resource* readback = D3d12Tools::readback(rig, 256);
        if (upload == nullptr || readback == nullptr) {
            problems.add("creating the staging buffers failed");
            return {};
        }
        for (int count : {1, 64, 1000}) {
            ID3D12Resource* chain[2] = {D3d12Tools::device_buffer(rig, 256, D3D12_RESOURCE_STATE_COMMON), D3d12Tools::device_buffer(rig, 256, D3D12_RESOURCE_STATE_COMMON)};
            if (chain[0] == nullptr || chain[1] == nullptr) {
                problems.add("creating the chain buffers failed");
                return {};
            }
            const std::uint32_t marker = 0xC0DE0000u + static_cast<std::uint32_t>(count);
            void* mapped = nullptr;
            const D3D12_RANGE none{0, 0};
            if (FAILED(upload->Map(0, &none, &mapped))) {
                problems.add("Map failed");
                return {};
            }
            std::memcpy(mapped, &marker, 4);
            upload->Unmap(0, nullptr);
            D3D12_RESOURCE_STATES state[2] = {D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON};
            std::vector<ID3D12CommandList*> lists;
            for (int i = 0; i < count && error.empty(); ++i) {
                const int source = (i + 1) % 2;
                const int destination = i % 2;
                ID3D12CommandList* list = rig.direct().record(
                    [&](ID3D12GraphicsCommandList* l) {
                        if (i != 0 && state[source] != D3D12_RESOURCE_STATE_COPY_SOURCE) {
                            D3d12Tools::move(l, chain[source], state[source], D3D12_RESOURCE_STATE_COPY_SOURCE);
                            state[source] = D3D12_RESOURCE_STATE_COPY_SOURCE;
                        }
                        if (state[destination] != D3D12_RESOURCE_STATE_COPY_DEST) {
                            D3d12Tools::move(l, chain[destination], state[destination], D3D12_RESOURCE_STATE_COPY_DEST);
                            state[destination] = D3D12_RESOURCE_STATE_COPY_DEST;
                        }
                        l->CopyBufferRegion(chain[destination], 0, i == 0 ? upload : chain[source], 0, 4);
                    },
                    error);
                lists.push_back(list);
            }
            const int last = (count - 1) % 2;
            ID3D12CommandList* final_list = error.empty() ? rig.direct().record(
                                                                [&](ID3D12GraphicsCommandList* l) {
                                                                    if (state[last] != D3D12_RESOURCE_STATE_COPY_SOURCE) {
                                                                        D3d12Tools::move(l, chain[last], state[last], D3D12_RESOURCE_STATE_COPY_SOURCE);
                                                                    }
                                                                    l->CopyBufferRegion(readback, 0, chain[last], 0, 4);
                                                                },
                                                                error)
                                                          : nullptr;
            if (final_list == nullptr) {
                problems.add(std::to_string(count) + " lists: recording failed: " + error);
                return {};
            }
            lists.push_back(final_list);
            const std::uint64_t value = rig.direct().execute(lists.data(), static_cast<UINT>(lists.size()));
            std::vector<std::uint8_t> raw;
            if (!rig.direct().wait(value, error) || !D3d12Tools::map_bytes(readback, 0, 4, raw, error)) {
                problems.add(std::to_string(count) + " lists: " + error);
                return {};
            }
            std::uint32_t got = 0;
            std::memcpy(&got, raw.data(), 4);
            if (got != marker) {
                problems.add("ExecuteCommandLists with " + std::to_string(count) + " lists passed 0x" + Text::hex(marker, 8) + " along the chain and ended with 0x" + Text::hex(got, 8) +
                             ", so the lists did not run in order");
            }
            rig.release(chain[0]);
            rig.release(chain[1]);
        }
        D3d12Root empty;
        ID3D12RootSignature* signature = empty.build(rig, error);
        ID3D12PipelineState* state = signature != nullptr ? Kit::pipeline(rig, signature, Kit::kSolidPs, DXGI_FORMAT_R8G8B8A8_UNORM, error) : nullptr;
        if (state == nullptr) {
            problems.add("bundle setup failed: " + error);
            return {};
        }
        Com<ID3D12CommandAllocator> allocator;
        Com<ID3D12GraphicsCommandList> bundle;
        if (!Kit::succeeds(problems, "CreateCommandAllocator(BUNDLE)", rig.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, __uuidof(ID3D12CommandAllocator), allocator.put_void())) ||
            !Kit::succeeds(problems, "CreateCommandList(BUNDLE)", rig.device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE, allocator.get(), nullptr, __uuidof(ID3D12GraphicsCommandList), bundle.put_void()))) {
            return {};
        }
        ID3D12GraphicsCommandList* held_bundle = rig.hold(std::move(bundle));
        rig.hold(std::move(allocator));
        held_bundle->SetGraphicsRootSignature(signature);
        held_bundle->SetPipelineState(state);
        held_bundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        held_bundle->DrawInstanced(3, 1, 0, 0);
        Kit::succeeds(problems, "closing the bundle", held_bundle->Close());
        D3d12Frame inline_frame;
        D3d12Frame bundled;
        TexelImage a;
        TexelImage b;
        if (!inline_frame.open(rig, 32, 32, error) || !bundled.open(rig, 32, 32, error) ||
            !inline_frame.run([&](ID3D12GraphicsCommandList* list) { Kit::draw_fullscreen(list, signature, state); }, error) ||
            !bundled.run(
                [&](ID3D12GraphicsCommandList* list) {
                    list->SetGraphicsRootSignature(signature);
                    list->ExecuteBundle(held_bundle);
                },
                error) ||
            !inline_frame.read(a, error) || !bundled.read(b, error)) {
            problems.add("bundle execution failed: " + error);
            return {};
        }
        static_cast<void>(Kit::same_bytes(context, problems, "bundle against inline commands", a.data.data(), b.data.data(), a.data.size()));
        std::string where;
        if (!Kit::uniform(b, Kit::solid_color(), where)) {
            problems.add("bundled draw: " + where);
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::d3d12
