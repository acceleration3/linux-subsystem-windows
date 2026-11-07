#include "sanity/d3d12/query_checks.hpp"

#include "sanity/d3d12/kit.hpp"
#include "support/swap_rig.hpp"
#include "support/test_window.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::d3d12 {

namespace {

const char* kTouch =
    "RWStructuredBuffer<uint> o : register(u0);\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) { o[id.x] = id.x; }\n";

ID3D12QueryHeap* query_heap(D3d12Rig& rig, D3D12_QUERY_HEAP_TYPE type, UINT count, HRESULT* result = nullptr) {
    D3D12_QUERY_HEAP_DESC desc{};
    desc.Type = type;
    desc.Count = count;
    Com<ID3D12QueryHeap> heap;
    const HRESULT made = rig.device()->CreateQueryHeap(&desc, __uuidof(ID3D12QueryHeap), heap.put_void());
    if (result != nullptr) {
        *result = made;
    }
    return SUCCEEDED(made) ? rig.hold(std::move(heap)) : nullptr;
}

struct Calibration {
    std::uint64_t gpu = 0;
    std::uint64_t cpu = 0;
};

bool calibrate(D3d12Queue& queue, Calibration& out, Problems& problems) {
    const HRESULT result = queue.queue()->GetClockCalibration(&out.gpu, &out.cpu);
    return Kit::succeeds(problems, "GetClockCalibration", result);
}

}  // namespace

Outcome QueryChecks::occlusion_pipeline(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::string error;
        D3d12Root draw_root;
        draw_root.constants(4, 0);
        ID3D12RootSignature* draw_signature = draw_root.build(rig, error);
        D3d12Root compute_root;
        compute_root.uav(0);
        ID3D12RootSignature* compute_signature = compute_root.build(rig, error);
        if (draw_signature == nullptr || compute_signature == nullptr) {
            problems.add(error);
            return {};
        }
        ID3D12PipelineState* draw = Kit::pipeline_with(rig, draw_signature, Kit::kConstantVs, Kit::kConstantPs, DXGI_FORMAT_R8G8B8A8_UNORM, error, [](D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
            desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            desc.DepthStencilState.DepthEnable = TRUE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        });
        ID3D12PipelineState* touch = D3d12Tools::compute_from(rig, compute_signature, kTouch, error);
        ID3D12QueryHeap* occlusion = query_heap(rig, D3D12_QUERY_HEAP_TYPE_OCCLUSION, 3);
        ID3D12QueryHeap* binary = query_heap(rig, D3D12_QUERY_HEAP_TYPE_OCCLUSION, 2);
        ID3D12QueryHeap* statistics = query_heap(rig, D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS, 2);
        ID3D12Resource* readback = D3d12Tools::readback(rig, 512);
        ID3D12Resource* unordered = D3d12Tools::device_buffer(rig, 192 * 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        D3d12Frame frame;
        if (draw == nullptr || touch == nullptr || occlusion == nullptr || binary == nullptr || statistics == nullptr || readback == nullptr || unordered == nullptr ||
            !frame.open(rig, 16, 16, error, DXGI_FORMAT_R8G8B8A8_UNORM, true)) {
            problems.add("setup failed: " + error);
            return {};
        }
        auto quad = [&](ID3D12GraphicsCommandList* list, float z) {
            const float values[4] = {z, 1.0f, 1.0f, 1.0f};
            list->SetGraphicsRootSignature(draw_signature);
            list->SetPipelineState(draw);
            list->SetGraphicsRoot32BitConstants(0, 4, values, 0);
            list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            list->DrawInstanced(3, 1, 0, 0);
        };
        const bool done = frame.run(
            [&](ID3D12GraphicsCommandList* list) {
                frame.clear_depth(list, 1.0f);
                list->BeginQuery(occlusion, D3D12_QUERY_TYPE_OCCLUSION, 0);
                quad(list, 0.2f);
                list->EndQuery(occlusion, D3D12_QUERY_TYPE_OCCLUSION, 0);
                list->BeginQuery(occlusion, D3D12_QUERY_TYPE_OCCLUSION, 1);
                quad(list, 0.8f);
                list->EndQuery(occlusion, D3D12_QUERY_TYPE_OCCLUSION, 1);
                frame.clear_depth(list, 1.0f);
                const D3D12_VIEWPORT half_viewport{0, 0, 8, 16, 0, 1};
                const D3D12_RECT half_scissor{0, 0, 8, 16};
                list->RSSetViewports(1, &half_viewport);
                list->RSSetScissorRects(1, &half_scissor);
                list->BeginQuery(occlusion, D3D12_QUERY_TYPE_OCCLUSION, 2);
                quad(list, 0.5f);
                list->EndQuery(occlusion, D3D12_QUERY_TYPE_OCCLUSION, 2);
                frame.bind(list);
                frame.clear_depth(list, 1.0f);
                list->BeginQuery(binary, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0);
                quad(list, 0.5f);
                list->EndQuery(binary, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0);
                list->BeginQuery(binary, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 1);
                quad(list, 0.9f);
                list->EndQuery(binary, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 1);
                frame.clear_depth(list, 1.0f);
                list->BeginQuery(statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
                quad(list, 0.5f);
                list->EndQuery(statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
                list->BeginQuery(statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 1);
                list->SetComputeRootSignature(compute_signature);
                list->SetPipelineState(touch);
                list->SetComputeRootUnorderedAccessView(0, unordered->GetGPUVirtualAddress());
                list->Dispatch(3, 1, 1);
                list->EndQuery(statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 1);
                list->ResolveQueryData(occlusion, D3D12_QUERY_TYPE_OCCLUSION, 0, 3, readback, 0);
                list->ResolveQueryData(binary, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0, 2, readback, 64);
                list->ResolveQueryData(statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, 2, readback, 128);
            },
            error);
        std::vector<std::uint8_t> raw;
        if (!done || !D3d12Tools::map_bytes(readback, 0, 512, raw, error)) {
            problems.add("recording the queries failed: " + error);
            return {};
        }
        std::uint64_t samples[3];
        std::memcpy(samples, raw.data(), sizeof(samples));
        const std::uint64_t expected_samples[3] = {256, 0, 128};
        const char* sample_names[3] = {"a 16x16 triangle in front", "a triangle behind the depth buffer", "an 8x16 viewport"};
        for (int i = 0; i < 3; ++i) {
            if (samples[i] != expected_samples[i]) {
                problems.add(std::string("occlusion query of ") + sample_names[i] + " resolved to " + std::to_string(samples[i]) + ", expected " + std::to_string(expected_samples[i]));
            }
        }
        std::uint64_t flags[2];
        std::memcpy(flags, raw.data() + 64, sizeof(flags));
        if (flags[0] != 1) {
            problems.add("the binary occlusion query of a visible triangle resolved to " + std::to_string(flags[0]) + ", expected 1");
        }
        if (flags[1] != 0) {
            problems.add("the binary occlusion query of a hidden triangle resolved to " + std::to_string(flags[1]) + ", expected 0");
        }
        D3D12_QUERY_DATA_PIPELINE_STATISTICS stats[2];
        std::memcpy(stats, raw.data() + 128, sizeof(stats));
        auto expect = [&](const char* name, std::uint64_t got, std::uint64_t want) {
            if (got != want) {
                problems.add(std::string("pipeline statistic ") + name + " is " + std::to_string(got) + ", expected " + std::to_string(want));
            }
        };
        expect("IAVertices", stats[0].IAVertices, 3);
        expect("IAPrimitives", stats[0].IAPrimitives, 1);
        expect("VSInvocations", stats[0].VSInvocations, 3);
        expect("CInvocations", stats[0].CInvocations, 1);
        expect("CPrimitives", stats[0].CPrimitives, 1);
        expect("GSInvocations", stats[0].GSInvocations, 0);
        expect("CSInvocations of the draw", stats[0].CSInvocations, 0);
        if (stats[0].PSInvocations < 256) {
            problems.add("PSInvocations is " + std::to_string(stats[0].PSInvocations) + " for a triangle covering 256 pixels");
        }
        expect("CSInvocations of Dispatch(3,1,1) with 64 threads", stats[1].CSInvocations, 192);
        return {};
    });
}

Outcome QueryChecks::timestamp_calibration(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::string error;
        D3d12Queue& queue = rig.direct();
        std::uint64_t frequency = 0;
        if (!Kit::succeeds(problems, "GetTimestampFrequency", queue.queue()->GetTimestampFrequency(&frequency))) {
            return {};
        }
        if (frequency == 0) {
            problems.add("GetTimestampFrequency returned 0");
            return {};
        }
        context.measure("d3d12.timestamp_frequency", static_cast<long long>(frequency));
        LARGE_INTEGER qpc_frequency{};
        ::QueryPerformanceFrequency(&qpc_frequency);
        Calibration first;
        Calibration second;
        if (!calibrate(queue, first, problems)) {
            return {};
        }
        ::Sleep(200);
        if (!calibrate(queue, second, problems)) {
            return {};
        }
        const double gpu_seconds = static_cast<double>(second.gpu - first.gpu) / static_cast<double>(frequency);
        const double cpu_seconds = static_cast<double>(second.cpu - first.cpu) / static_cast<double>(qpc_frequency.QuadPart);
        context.measure("d3d12.calibration_gpu_ms", gpu_seconds * 1000.0);
        context.measure("d3d12.calibration_cpu_ms", cpu_seconds * 1000.0);
        if (second.gpu < first.gpu || second.cpu < first.cpu) {
            problems.add("the calibration pairs go backwards");
        }
        if (cpu_seconds < 0.19 || cpu_seconds > 0.5) {
            problems.add("the CPU timestamps of a 200 ms sleep span " + std::to_string(cpu_seconds * 1000.0) + " ms");
        }
        if (std::fabs(gpu_seconds - cpu_seconds) > 0.005) {
            problems.add("GPU ticks over the sleep convert to " + std::to_string(gpu_seconds * 1000.0) + " ms but the CPU stopwatch reads " + std::to_string(cpu_seconds * 1000.0) + " ms");
        }
        ID3D12QueryHeap* heap = query_heap(rig, D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 8);
        ID3D12Resource* upload = D3d12Tools::upload(rig, 4 * 1024 * 1024);
        ID3D12Resource* target = D3d12Tools::device_buffer(rig, 4 * 1024 * 1024, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* readback = D3d12Tools::readback(rig, 64);
        if (heap == nullptr || upload == nullptr || target == nullptr || readback == nullptr) {
            problems.add("creating the timestamp objects failed");
            return {};
        }
        Calibration before;
        Calibration after;
        if (!calibrate(queue, before, problems)) {
            return {};
        }
        if (!queue.run_wait(
                [&](ID3D12GraphicsCommandList* list) {
                    for (UINT i = 0; i < 8; ++i) {
                        list->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, i);
                        list->CopyBufferRegion(target, 0, upload, 0, 4 * 1024 * 1024);
                    }
                    list->ResolveQueryData(heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 8, readback, 0);
                },
                error)) {
            problems.add("recording timestamps failed: " + error);
            return {};
        }
        if (!calibrate(queue, after, problems)) {
            return {};
        }
        std::vector<std::uint8_t> raw;
        if (!D3d12Tools::map_bytes(readback, 0, 64, raw, error)) {
            problems.add(error);
            return {};
        }
        std::uint64_t stamps[8];
        std::memcpy(stamps, raw.data(), sizeof(stamps));
        for (int i = 1; i < 8; ++i) {
            if (stamps[i] < stamps[i - 1]) {
                problems.add("timestamp " + std::to_string(i) + " is below timestamp " + std::to_string(i - 1));
            }
        }
        context.measure("d3d12.timestamp_span_us", static_cast<double>(stamps[7] - stamps[0]) * 1.0e6 / static_cast<double>(frequency));
        const std::uint64_t slack = frequency / 200;
        if (stamps[0] + slack < before.gpu || stamps[7] > after.gpu + slack) {
            problems.add("the list's timestamps are outside the two clock calibrations taken around it (first " + std::to_string(stamps[0]) + ", last " + std::to_string(stamps[7]) +
                         ", calibrations " + std::to_string(before.gpu) + " and " + std::to_string(after.gpu) + ")");
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3{};
        if (SUCCEEDED(D3d12Tools::feature(rig.device(), D3D12_FEATURE_D3D12_OPTIONS3, options3)) && options3.CopyQueueTimestampQueriesSupported) {
            if (!rig.open_queue(D3D12_COMMAND_LIST_TYPE_COPY, error)) {
                problems.add(error);
                return {};
            }
            std::uint64_t copy_frequency = 0;
            Kit::succeeds(problems, "GetTimestampFrequency of the copy queue", rig.copy().queue()->GetTimestampFrequency(&copy_frequency));
            if (copy_frequency == 0) {
                problems.add("the copy queue timestamp frequency is 0 although copy queue timestamp queries are reported");
            }
            ID3D12QueryHeap* copy_heap = query_heap(rig, D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP, 2);
            ID3D12Resource* copy_readback = D3d12Tools::readback(rig, 16);
            if (copy_heap == nullptr || copy_readback == nullptr) {
                problems.add("creating the copy queue timestamp objects failed");
                return {};
            }
            if (!rig.copy().run_wait(
                    [&](ID3D12GraphicsCommandList* list) {
                        list->EndQuery(copy_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
                        list->CopyBufferRegion(target, 0, upload, 0, 4 * 1024 * 1024);
                        list->EndQuery(copy_heap, D3D12_QUERY_TYPE_TIMESTAMP, 1);
                        list->ResolveQueryData(copy_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, copy_readback, 0);
                    },
                    error) ||
                !D3d12Tools::map_bytes(copy_readback, 0, 16, raw, error)) {
                problems.add("copy queue timestamps: " + error);
                return {};
            }
            std::uint64_t pair[2];
            std::memcpy(pair, raw.data(), sizeof(pair));
            if (pair[1] < pair[0]) {
                problems.add("copy queue timestamps decrease across a copy");
            }
        }
        return {};
    });
}

Outcome QueryChecks::present_queue(Context& context) {
    std::string error;
    AvAdapter adapter;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    D3d12Rig rig;
    if (!rig.open(adapter, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    TestWindow window;
    TestWindowConfig config;
    config.x = 60;
    config.y = 60;
    TestWindow::outer_size(config.x, config.y, 256, 256, config.width, config.height);
    if (!window.create(config, error)) {
        return Outcome::fail(error);
    }
    ::SetWindowPos(window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    auto color_of = [](int frame, int rgb[3]) {
        rgb[0] = 40 + (frame * 53) % 200;
        rgb[1] = 40 + (frame * 97) % 200;
        rgb[2] = 40 + (frame * 29) % 200;
    };
    for (UINT count : {2u, 3u, 4u}) {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 256;
        desc.Height = 256;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = count;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Scaling = DXGI_SCALING_STRETCH;
        Com<IDXGISwapChain1> chain1;
        const std::string label = std::to_string(count) + " buffers";
        const HRESULT created = rig.factory->CreateSwapChainForHwnd(rig.direct().queue(), window.hwnd(), &desc, nullptr, nullptr, chain1.put());
        if (FAILED(created)) {
            problems.add(label + ": CreateSwapChainForHwnd on a D3D12 direct queue failed with " + Kit::hr(created));
            continue;
        }
        Com<IDXGISwapChain3> chain3 = chain1.as<IDXGISwapChain3>();
        if (!chain3) {
            problems.add(label + ": IDXGISwapChain3 is not exposed");
            continue;
        }
        IDXGISwapChain3* chain = rig.hold(std::move(chain3));
        chain1.reset();
        ID3D12DescriptorHeap* rtvs = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 4, false);
        const UINT increment = rig.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        std::vector<ID3D12Resource*> buffers;
        auto fetch_buffers = [&]() {
            buffers.clear();
            for (UINT i = 0; i < count; ++i) {
                Com<ID3D12Resource> buffer;
                if (FAILED(chain->GetBuffer(i, __uuidof(ID3D12Resource), buffer.put_void()))) {
                    problems.add(label + ": GetBuffer(" + std::to_string(i) + ") failed");
                    return false;
                }
                ID3D12Resource* held = rig.hold(std::move(buffer));
                rig.device()->CreateRenderTargetView(held, nullptr, D3d12Tools::cpu_at(rtvs, i, increment));
                buffers.push_back(held);
            }
            return true;
        };
        auto present_frame = [&](int frame) -> bool {
            const UINT index = chain->GetCurrentBackBufferIndex();
            if (index >= count) {
                problems.add(label + ": GetCurrentBackBufferIndex returned " + std::to_string(index));
                return false;
            }
            int rgb[3];
            color_of(frame, rgb);
            const float color[4] = {static_cast<float>(rgb[0]) / 255.0f, static_cast<float>(rgb[1]) / 255.0f, static_cast<float>(rgb[2]) / 255.0f, 1.0f};
            std::string local;
            std::uint64_t submitted = 0;
            if (!rig.direct().run(
                    [&](ID3D12GraphicsCommandList* list) {
                        D3d12Tools::move(list, buffers[index], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
                        list->ClearRenderTargetView(D3d12Tools::cpu_at(rtvs, index, increment), color, 0, nullptr);
                        D3d12Tools::move(list, buffers[index], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
                    },
                    submitted, local)) {
                problems.add(label + ": " + local);
                return false;
            }
            const HRESULT presented = chain->Present(1, 0);
            if (FAILED(presented)) {
                problems.add(label + ": Present of frame " + std::to_string(frame) + " returned " + Kit::hr(presented));
                return false;
            }
            POINT point{128, 128};
            ::ClientToScreen(window.hwnd(), &point);
            int seen[3] = {0, 0, 0};
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            bool matched = false;
            while (std::chrono::steady_clock::now() < deadline) {
                if (SwapRig::screen_rgb(point.x, point.y, seen) && std::abs(seen[0] - rgb[0]) <= 3 && std::abs(seen[1] - rgb[1]) <= 3 && std::abs(seen[2] - rgb[2]) <= 3) {
                    matched = true;
                    break;
                }
                ::Sleep(10);
            }
            if (!matched) {
                problems.add(label + ": frame " + std::to_string(frame) + " (" + std::to_string(rgb[0]) + "," + std::to_string(rgb[1]) + "," + std::to_string(rgb[2]) +
                             ") never appeared on screen; the last pixel read was (" + std::to_string(seen[0]) + "," + std::to_string(seen[1]) + "," + std::to_string(seen[2]) + ")");
                return false;
            }
            return true;
        };
        if (fetch_buffers()) {
            for (int frame = 0; frame < 10 && present_frame(frame); ++frame) {
            }
            std::string local;
            if (!rig.idle(local)) {
                problems.add(label + ": " + local);
            }
            for (ID3D12Resource* buffer : buffers) {
                rig.release(buffer);
            }
            buffers.clear();
            static_cast<void>(rig.idle(local));
            const HRESULT resized = chain->ResizeBuffers(0, 320, 200, DXGI_FORMAT_UNKNOWN, 0);
            if (Kit::succeeds(problems, label + ": ResizeBuffers after idle", resized) && fetch_buffers()) {
                const D3D12_RESOURCE_DESC after = buffers[0]->GetDesc();
                if (after.Width != 320 || after.Height != 200) {
                    problems.add(label + ": the resized back buffer is " + std::to_string(after.Width) + "x" + std::to_string(after.Height) + ", expected 320x200");
                }
                static_cast<void>(present_frame(20));
                static_cast<void>(rig.idle(local));
            }
        }
        std::string local;
        static_cast<void>(rig.idle(local));
        for (ID3D12Resource* buffer : buffers) {
            rig.release(buffer);
        }
        static_cast<void>(rig.idle(local));
        rig.release(chain);
        static_cast<void>(rig.idle(local));
    }
    static_cast<void>(context);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d12
