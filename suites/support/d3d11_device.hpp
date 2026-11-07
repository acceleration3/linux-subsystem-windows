#pragma once

#include <windows.h>
#include <d3d11.h>

#include <mutex>
#include <string>

namespace lsw::suites {
class D3d11Device {
public:
    D3d11Device() = default;
    D3d11Device(const D3d11Device&) = delete;
    D3d11Device& operator=(const D3d11Device&) = delete;
    ~D3d11Device();

    [[nodiscard]] bool create(const LUID& luid, std::string& error);

    [[nodiscard]] bool create_ex(const LUID& luid, const D3D_FEATURE_LEVEL* levels, UINT count, UINT flags,
                                 std::string& error);

    [[nodiscard]] bool create_warp(const D3D_FEATURE_LEVEL* levels, UINT count, UINT flags, std::string& error);

    void destroy();

    [[nodiscard]] bool wait_idle(std::string& error);

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_9_1;
    HRESULT result = S_OK;
    std::mutex mutex;
};
}  // namespace lsw::suites
