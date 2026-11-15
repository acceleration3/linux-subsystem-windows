#include "sanity/sync/sync_device.hpp"

#include "support/d3d11_tools.hpp"
#include "support/target_adapter.hpp"

#include <windows.h>

namespace lsw::suites::sanity::sync {

namespace {

constexpr const char* kStep =
    "cbuffer C : register(b0) { uint op; uint value; uint slot; uint pad; };\n"
    "RWStructuredBuffer<uint> o : register(u0);\n"
    "[numthreads(1, 1, 1)]\n"
    "void main() { if (op == 0) { o[slot] = value; } else { o[slot] += value; } }\n";

class SyncD3d11 : public SyncDevice {
public:
    bool open(std::string& error) override {
        AvAdapter adapter;
        if (!TargetAdapter::pick(adapter, error) || !device_.create(adapter.luid, error)) {
            return false;
        }
        shader_ = D3d11Tools::cs(device_, kStep, error);
        buffer_ = D3d11Tools::buffer(device_, kSlots * 4, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, nullptr);
        constants_ = D3d11Tools::buffer(device_, 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, nullptr);
        if (!shader_ || !buffer_ || !constants_) {
            error = error.empty() ? "creating the D3D11 objects failed" : error;
            return false;
        }
        view_ = D3d11Tools::structured_uav(device_, buffer_.get(), kSlots);
        if (!view_) {
            error = "creating the D3D11 UAV failed";
            return false;
        }
        const UINT zero[4] = {0, 0, 0, 0};
        device_.context->ClearUnorderedAccessViewUint(view_.get(), zero);
        return true;
    }

    bool set(std::uint32_t slot, std::uint32_t value, std::string& error) override { return step(0, slot, value, error); }

    bool add(std::uint32_t slot, std::uint32_t value, std::string& error) override { return step(1, slot, value, error); }

    bool signal(std::string& error) override {
        D3D11_QUERY_DESC desc{};
        desc.Query = D3D11_QUERY_EVENT;
        query_.reset();
        if (FAILED(device_.device->CreateQuery(&desc, query_.put()))) {
            error = "creating the D3D11 event query failed";
            return false;
        }
        device_.context->End(query_.get());
        device_.context->Flush();
        return true;
    }

    SyncState wait(int timeout_ms, std::string& detail) override {
        const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(timeout_ms);
        for (;;) {
            BOOL done = FALSE;
            const HRESULT result = device_.context->GetData(query_.get(), &done, sizeof(done), 0);
            const HRESULT removed = device_.device->GetDeviceRemovedReason();
            if (removed != S_OK) {
                detail = "device removed: " + D3d11Tools::hr(removed);
                return SyncState::Lost;
            }
            if (result == S_OK && done) {
                return SyncState::Done;
            }
            if (FAILED(result)) {
                detail = "GetData returned " + D3d11Tools::hr(result);
                return SyncState::Error;
            }
            if (static_cast<LONGLONG>(GetTickCount64() - end) >= 0) {
                return SyncState::Timeout;
            }
            ::Sleep(1);
        }
    }

    bool finish(std::string& error) override {
        if (!signal(error)) {
            return false;
        }
        std::string detail;
        if (wait(30000, detail) != SyncState::Done) {
            error = "the event query did not complete after Flush: " + detail;
            return false;
        }
        return true;
    }

    bool read(std::uint32_t slot, std::uint32_t& out, std::string& error) override {
        const std::vector<std::uint32_t> words = D3d11Tools::read_words(device_, buffer_.get(), error);
        if (words.size() <= slot) {
            return false;
        }
        out = words[slot];
        return true;
    }

private:
    bool step(std::uint32_t op, std::uint32_t slot, std::uint32_t value, std::string&) {
        const std::uint32_t values[4] = {op, value, slot, 0};
        device_.context->UpdateSubresource(constants_.get(), 0, nullptr, values, 0, 0);
        ID3D11Buffer* cb = constants_.get();
        ID3D11UnorderedAccessView* uav = view_.get();
        device_.context->CSSetShader(shader_.get(), nullptr, 0);
        device_.context->CSSetConstantBuffers(0, 1, &cb);
        device_.context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        device_.context->Dispatch(1, 1, 1);
        return true;
    }

    D3d11Device device_;
    Com<ID3D11ComputeShader> shader_;
    Com<ID3D11Buffer> buffer_;
    Com<ID3D11Buffer> constants_;
    Com<ID3D11UnorderedAccessView> view_;
    Com<ID3D11Query> query_;
};

}  // namespace

std::unique_ptr<SyncDevice> make_sync_d3d11() {
    return std::make_unique<SyncD3d11>();
}

}  // namespace lsw::suites::sanity::sync
