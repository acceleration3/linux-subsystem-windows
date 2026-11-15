#include "sanity/sync/sync_device.hpp"

#include "support/d3d12_root.hpp"
#include "support/d3d12_tools.hpp"
#include "support/target_adapter.hpp"

namespace lsw::suites::sanity::sync {

namespace {

constexpr const char* kStep =
    "cbuffer C : register(b0) { uint op; uint value; uint slot; uint pad; };\n"
    "RWStructuredBuffer<uint> o : register(u0);\n"
    "[numthreads(1, 1, 1)]\n"
    "void main() { if (op == 0) { o[slot] = value; } else { o[slot] += value; } }\n";

class SyncD3d12 : public SyncDevice {
public:
    bool open(std::string& error) override {
        AvAdapter adapter;
        if (!TargetAdapter::pick(adapter, error) || !rig_.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
            return false;
        }
        D3d12Root root;
        root.constants(4, 0).uav(0);
        signature_ = root.build(rig_, error);
        D3d12Tools::Bytes code;
        if (signature_ == nullptr || !D3d12Tools::compile(kStep, "main", "cs_5_1", code, error)) {
            return false;
        }
        state_ = D3d12Tools::compute_pso(rig_, signature_, code, error);
        buffer_ = D3d12Tools::device_buffer(rig_, kSlots * 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (state_ == nullptr || buffer_ == nullptr) {
            error = error.empty() ? "creating the D3D12 objects failed" : error;
            return false;
        }
        const std::vector<std::uint32_t> zeros(kSlots, 0);
        return D3d12Tools::write_buffer(rig_, buffer_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, zeros.data(), zeros.size() * 4, error);
    }

    bool set(std::uint32_t slot, std::uint32_t value, std::string& error) override { return step(0, slot, value, error); }

    bool add(std::uint32_t slot, std::uint32_t value, std::string& error) override { return step(1, slot, value, error); }

    bool signal(std::string&) override {
        value_ = rig_.direct().signal();
        return true;
    }

    SyncState wait(int timeout_ms, std::string& detail) override {
        D3d12Queue& queue = rig_.direct();
        for (int attempt = 0; attempt < 2; ++attempt) {
            const HRESULT removed = rig_.removed_reason();
            if (removed != S_OK) {
                detail = "device removed: " + D3d12Tools::hr(removed);
                return SyncState::Lost;
            }
            if (queue.reached(value_)) {
                return SyncState::Done;
            }
            if (attempt == 1 || timeout_ms <= 0) {
                break;
            }
            HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (event == nullptr || FAILED(queue.fence()->SetEventOnCompletion(value_, event))) {
                detail = "SetEventOnCompletion failed";
                if (event != nullptr) {
                    ::CloseHandle(event);
                }
                return SyncState::Error;
            }
            ::WaitForSingleObject(event, static_cast<DWORD>(timeout_ms));
            ::CloseHandle(event);
        }
        return SyncState::Timeout;
    }

    bool finish(std::string& error) override { return rig_.direct().idle(error); }

    bool read(std::uint32_t slot, std::uint32_t& out, std::string& error) override {
        std::vector<std::uint32_t> words;
        if (!D3d12Tools::read_words(rig_, buffer_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kSlots, words, error) || words.size() <= slot) {
            return false;
        }
        out = words[slot];
        return true;
    }

private:
    bool step(std::uint32_t op, std::uint32_t slot, std::uint32_t value, std::string& error) {
        const std::uint32_t values[4] = {op, value, slot, 0};
        return rig_.direct().run(
            [&](ID3D12GraphicsCommandList* list) {
                list->SetComputeRootSignature(signature_);
                list->SetPipelineState(state_);
                list->SetComputeRoot32BitConstants(0, 4, values, 0);
                list->SetComputeRootUnorderedAccessView(1, buffer_->GetGPUVirtualAddress());
                list->Dispatch(1, 1, 1);
                D3d12Tools::uav_wait(list, buffer_);
            },
            value_, error);
    }

    D3d12Rig rig_;
    ID3D12RootSignature* signature_ = nullptr;
    ID3D12PipelineState* state_ = nullptr;
    ID3D12Resource* buffer_ = nullptr;
    std::uint64_t value_ = 0;
};

}  // namespace

std::unique_ptr<SyncDevice> make_sync_d3d12() {
    return std::make_unique<SyncD3d12>();
}

}  // namespace lsw::suites::sanity::sync
