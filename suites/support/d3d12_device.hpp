#pragma once

#include <windows.h>
#include <d3d12.h>

#include <mutex>
#include <string>

namespace lsw::suites {
class D3d12Device {
public:
    D3d12Device() = default;
    D3d12Device(const D3d12Device&) = delete;
    D3d12Device& operator=(const D3d12Device&) = delete;
    ~D3d12Device();

    [[nodiscard]] bool create(const LUID& luid, std::string& error);

    [[nodiscard]] ID3D12CommandQueue* make_queue(std::string& error);

    ID3D12Device* device = nullptr;
    std::mutex mutex;
};
}  // namespace lsw::suites
