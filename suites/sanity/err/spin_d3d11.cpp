#include "sanity/err/spin_device.hpp"

#include "support/d3d11_tools.hpp"
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
    "RWStructuredBuffer<uint> o : register(u0);\n"
    "[numthreads(1, 1, 1)]\n"
    "void main() {\n"
    "  uint c = 0; uint v = 0;\n"
    "  [loop] do { InterlockedOr(o[0], 0, v); ++c; } while (v == 0);\n"
    "  o[1] = c;\n"
    "}\n";

class SpinD3d11 : public SpinDevice {
public:
    bool open(bool forever, std::string& error) override {
        forever_ = forever;
        AvAdapter adapter;
        if (!TargetAdapter::pick(adapter, error) || !device_.create(adapter.luid, error)) {
            return false;
        }
        shader_ = D3d11Tools::cs(device_, forever ? kForever : kSpin, error);
        buffer_ = D3d11Tools::buffer(device_, kThreads * 4, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, nullptr);
        constants_ = D3d11Tools::buffer(device_, 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, nullptr);
        if (!shader_ || !buffer_ || !constants_) {
            error = error.empty() ? "creating the D3D11 objects failed" : error;
            return false;
        }
        view_ = D3d11Tools::structured_uav(device_, buffer_.get(), kThreads);
        if (!view_) {
            error = "creating the D3D11 UAV failed";
            return false;
        }
        const UINT zero[4] = {0, 0, 0, 0};
        device_.context->ClearUnorderedAccessViewUint(view_.get(), zero);
        return true;
    }

    bool submit(std::uint32_t outer, std::string& error) override {
        const std::uint32_t values[4] = {kInner, outer, 0, 0};
        device_.context->UpdateSubresource(constants_.get(), 0, nullptr, values, 0, 0);
        ID3D11Buffer* cb = constants_.get();
        ID3D11UnorderedAccessView* uav = view_.get();
        device_.context->CSSetShader(shader_.get(), nullptr, 0);
        device_.context->CSSetConstantBuffers(0, 1, &cb);
        device_.context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        device_.context->Dispatch(1, 1, 1);
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

    SpinState poll(int timeout_ms, std::string& detail) override {
        const auto end = GetTickCount64() + static_cast<ULONGLONG>(timeout_ms);
        for (;;) {
            BOOL done = FALSE;
            const HRESULT result = device_.context->GetData(query_.get(), &done, sizeof(done), 0);
            const HRESULT removed = device_.device->GetDeviceRemovedReason();
            if (removed != S_OK) {
                detail = "device removed: " + D3d11Tools::hr(removed);
                return SpinState::Lost;
            }
            if (result == S_OK && done) {
                return SpinState::Done;
            }
            if (FAILED(result)) {
                detail = "GetData returned " + D3d11Tools::hr(result);
                return result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DEVICE_RESET ? SpinState::Lost : SpinState::Error;
            }
            if (static_cast<LONGLONG>(GetTickCount64() - end) >= 0) {
                return SpinState::Pending;
            }
            ::Sleep(5);
        }
    }

    bool read(std::vector<std::uint32_t>& out, std::string& error) override {
        std::vector<std::uint8_t> bytes;
        if (!D3d11Tools::read_buffer(device_, buffer_.get(), bytes, error)) {
            return false;
        }
        out.resize(bytes.size() / 4);
        std::memcpy(out.data(), bytes.data(), out.size() * 4);
        return true;
    }

private:
    bool forever_ = false;
    D3d11Device device_;
    Com<ID3D11ComputeShader> shader_;
    Com<ID3D11Buffer> buffer_;
    Com<ID3D11Buffer> constants_;
    Com<ID3D11UnorderedAccessView> view_;
    Com<ID3D11Query> query_;
};

}  // namespace

std::unique_ptr<SpinDevice> make_spin_d3d11() {
    return std::make_unique<SpinD3d11>();
}

}  // namespace lsw::suites::sanity::err
