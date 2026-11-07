#pragma once

#include "support/av_adapters.hpp"
#include "support/com_ptr.hpp"
#include "support/d3d12_queue.hpp"

#include <d3d12.h>
#include <dxgi1_6.h>

#include <string>
#include <vector>

namespace lsw::suites {
class D3d12Rig {
public:
    D3d12Rig() = default;
    D3d12Rig(const D3d12Rig&) = delete;
    D3d12Rig& operator=(const D3d12Rig&) = delete;
    ~D3d12Rig();

    [[nodiscard]] static std::vector<AvAdapter> target_adapters(std::string& error);

    [[nodiscard]] bool open(const AvAdapter& target, D3D_FEATURE_LEVEL level, std::string& error);

    [[nodiscard]] bool open_luid(const LUID& luid, D3D_FEATURE_LEVEL level, std::string& error);

    [[nodiscard]] bool open_warp(D3D_FEATURE_LEVEL level, std::string& error);

    [[nodiscard]] ID3D12Device* device() const { return device_.get(); }

    template <typename T>
    [[nodiscard]] Com<T> as() const {
        return device_.as<T>();
    }

    [[nodiscard]] D3d12Queue& direct() { return direct_; }

    [[nodiscard]] D3d12Queue& compute() { return compute_; }

    [[nodiscard]] D3d12Queue& copy() { return copy_; }

    [[nodiscard]] D3d12Queue& queue(D3D12_COMMAND_LIST_TYPE type);

    [[nodiscard]] bool open_queue(D3D12_COMMAND_LIST_TYPE type, std::string& error);

    [[nodiscard]] bool open_queues(std::string& error);

    template <typename T>
    T* hold(Com<T>&& object) {
        T* raw = object.detach();
        if (raw != nullptr) {
            held_.push_back(raw);
        }
        return raw;
    }

    void release(IUnknown* held);

    [[nodiscard]] bool idle(std::string& error);

    void shutdown();

    [[nodiscard]] HRESULT removed_reason() const { return device_->GetDeviceRemovedReason(); }

    [[nodiscard]] std::string describe() const;

    LUID luid{};
    bool warp = false;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    std::string name;
    Com<IDXGIFactory4> factory;
    Com<IDXGIAdapter1> adapter;

private:
    [[nodiscard]] bool create_device(D3D_FEATURE_LEVEL wanted, std::string& error);

    Com<ID3D12Device> device_;
    D3d12Queue direct_;
    D3d12Queue compute_;
    D3d12Queue copy_;
    std::vector<IUnknown*> held_;
};
}  // namespace lsw::suites
