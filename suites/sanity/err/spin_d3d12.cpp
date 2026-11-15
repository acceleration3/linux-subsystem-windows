#include "sanity/err/spin_device.hpp"

#include "support/d3d12_root.hpp"
#include "support/d3d12_tools.hpp"
#include "support/target_adapter.hpp"

#include <cstring>

namespace lsw::suites::sanity::err {

namespace {

constexpr const char* kSpin =
    "cbuffer C : register(b0) { uint inner; uint outer; };\n"
    "RWStructuredBuffer<uint> o : register(u0);\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) {\n"
    "  uint x = id.x * 2654435761u + 12345u;\n"
    "  [loop] for (uint a = 0; a < outer; ++a) {\n"
    "    [loop] for (uint k = 0; k < inner; ++k) { x = x * 1664525u + 1013904223u; }\n"
    "  }\n"
    "  o[id.x] = x;\n"
    "}\n";

constexpr const char* kForever =
    "cbuffer C : register(b0) { uint inner; uint outer; };\n"
    "RWStructuredBuffer<uint> o : register(u0);\n"
    "[numthreads(1, 1, 1)]\n"
    "void main() {\n"
    "  uint c = 0; uint v = 0;\n"
    "  [loop] do { InterlockedOr(o[0], 0, v); ++c; } while (v == 0);\n"
    "  o[1] = c + inner + outer;\n"
    "}\n";

class SpinD3d12 : public SpinDevice {
public:
    bool open(bool forever, std::string& error) override {
        AvAdapter adapter;
        if (!TargetAdapter::pick(adapter, error) || !rig_.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
            return false;
        }
        D3d12Root root;
        root.constants(2, 0).uav(0);
        signature_ = root.build(rig_, error);
        D3d12Tools::Bytes code;
        if (signature_ == nullptr || !D3d12Tools::compile(forever ? kForever : kSpin, "main", "cs_5_1", code, error)) {
            return false;
        }
        state_ = D3d12Tools::compute_pso(rig_, signature_, code, error);
        buffer_ = D3d12Tools::device_buffer(rig_, kThreads * 4, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (state_ == nullptr || buffer_ == nullptr) {
            error = error.empty() ? "creating the D3D12 objects failed" : error;
            return false;
        }
        const std::vector<std::uint32_t> zeros(kThreads, 0);
        if (!D3d12Tools::write_buffer(rig_, buffer_, D3D12_RESOURCE_STATE_COPY_DEST, 0, zeros.data(), zeros.size() * 4, error)) {
            return false;
        }
        buffer_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
        return true;
    }

    bool submit(std::uint32_t outer, std::string& error) override {
        const std::uint32_t values[2] = {kInner, outer};
        D3d12Queue& queue = rig_.direct();
        const D3D12_RESOURCE_STATES before = buffer_state_;
        const bool recorded = queue.run(
            [&](ID3D12GraphicsCommandList* list) {
                D3d12Tools::move(list, buffer_, before, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                list->SetComputeRootSignature(signature_);
                list->SetPipelineState(state_);
                list->SetComputeRoot32BitConstants(0, 2, values, 0);
                list->SetComputeRootUnorderedAccessView(1, buffer_->GetGPUVirtualAddress());
                list->Dispatch(1, 1, 1);
                D3d12Tools::move(list, buffer_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
            },
            value_, error);
        buffer_state_ = D3D12_RESOURCE_STATE_COMMON;
        return recorded;
    }

    SpinState poll(int timeout_ms, std::string& detail) override {
        D3d12Queue& queue = rig_.direct();
        const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(timeout_ms);
        for (;;) {
            const HRESULT removed = rig_.removed_reason();
            if (removed != S_OK) {
                detail = "device removed: " + D3d12Tools::hr(removed);
                return SpinState::Lost;
            }
            if (queue.reached(value_)) {
                return SpinState::Done;
            }
            if (static_cast<LONGLONG>(GetTickCount64() - end) >= 0) {
                return SpinState::Pending;
            }
            ::Sleep(5);
        }
    }

    SpinState wait_native(int timeout_ms, std::string& detail) override {
        D3d12Queue& queue = rig_.direct();
        if (!queue.reached(value_)) {
            HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (event == nullptr || FAILED(queue.fence()->SetEventOnCompletion(value_, event))) {
                detail = "SetEventOnCompletion failed";
                return SpinState::Error;
            }
            ::WaitForSingleObject(event, static_cast<DWORD>(timeout_ms));
            ::CloseHandle(event);
        }
        const HRESULT removed = rig_.removed_reason();
        if (removed != S_OK) {
            detail = "device removed: " + D3d12Tools::hr(removed);
            return SpinState::Lost;
        }
        return queue.reached(value_) ? SpinState::Done : SpinState::Pending;
    }

    bool read(std::vector<std::uint32_t>& out, std::string& error) override {
        return D3d12Tools::read_words(rig_, buffer_, D3D12_RESOURCE_STATE_COMMON, kThreads, out, error);
    }

private:
    D3d12Rig rig_;
    ID3D12RootSignature* signature_ = nullptr;
    ID3D12PipelineState* state_ = nullptr;
    ID3D12Resource* buffer_ = nullptr;
    D3D12_RESOURCE_STATES buffer_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    std::uint64_t value_ = 0;
};

}  // namespace

std::unique_ptr<SpinDevice> make_spin_d3d12() {
    return std::make_unique<SpinD3d12>();
}

}  // namespace lsw::suites::sanity::err
