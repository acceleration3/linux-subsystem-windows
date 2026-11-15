#pragma once

#include <windows.h>
#include <d3d10_1.h>
#include <dxgi.h>

#include <string>

namespace lsw::suites {
class D3d10Device {
public:
    D3d10Device() = default;
    D3d10Device(const D3d10Device&) = delete;
    D3d10Device& operator=(const D3d10Device&) = delete;
    ~D3d10Device();

    [[nodiscard]] static bool library_available(bool with_1);

    [[nodiscard]] static HRESULT raw_create(IDXGIAdapter* adapter, D3D10_DRIVER_TYPE type, HMODULE software, UINT flags,
                                            ID3D10Device** out);

    [[nodiscard]] static HRESULT raw_create1(IDXGIAdapter* adapter, D3D10_DRIVER_TYPE type, HMODULE software, UINT flags,
                                             D3D10_FEATURE_LEVEL1 level, ID3D10Device1** out);

    [[nodiscard]] bool create(const LUID& luid, UINT flags, std::string& error);

    [[nodiscard]] bool create1(const LUID& luid, UINT flags, D3D10_FEATURE_LEVEL1 level, std::string& error);

    [[nodiscard]] bool create_warp(UINT flags, std::string& error);

    [[nodiscard]] bool create_warp1(UINT flags, D3D10_FEATURE_LEVEL1 level, std::string& error);

    void destroy();

    [[nodiscard]] bool wait_idle(std::string& error);

    ID3D10Device* device = nullptr;
    ID3D10Device1* device1 = nullptr;
    HRESULT result = S_OK;
};
}  // namespace lsw::suites
