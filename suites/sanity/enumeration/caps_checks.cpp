#include "sanity/enumeration/caps_checks.hpp"

#include "core/text.hpp"
#include "support/av_adapters.hpp"
#include "support/dyn_library.hpp"
#include "support/problems.hpp"
#include "support/vulkan_api.hpp"

#include <d3d11.h>
#include <d3d12.h>

#include <algorithm>

namespace lsw::suites::sanity::enumeration {

namespace {

constexpr D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
                                         D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                         D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};

std::string level_name(int level) {
    return level < 0 ? "none" : "0x" + Text::hex(static_cast<unsigned>(level), 4);
}

}  // namespace

Outcome CapsChecks::feature_level_vs_api_support(Context& context) {
    std::vector<AvAdapter> adapters;
    std::string error;
    if (!AvAdapters::accel(adapters, error)) {
        return Outcome::fail(error);
    }
    DynLibrary d3d11;
    if (!d3d11.load("d3d11.dll")) {
        return Outcome::fail("d3d11.dll cannot be loaded");
    }
    auto create11 = d3d11.symbol<PFN_D3D11_CREATE_DEVICE>("D3D11CreateDevice");
    if (create11 == nullptr) {
        return Outcome::fail("d3d11.dll has no D3D11CreateDevice");
    }
    DynLibrary d3d12;
    PFN_D3D12_CREATE_DEVICE create12 = nullptr;
    if (d3d12.load("d3d12.dll")) {
        create12 = d3d12.symbol<PFN_D3D12_CREATE_DEVICE>("D3D12CreateDevice");
    }
    VulkanApi vulkan;
    std::vector<VulkanDevice> vk_devices;
    if (!vulkan.load(error) || !vulkan.create_instance(error) || !vulkan.devices(vk_devices, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    for (const AvAdapter& adapter : adapters) {
        const std::string label = adapter.name + " " + adapter.luid_text();
        IUnknown* dxgi_adapter = AvAdapters::open_adapter(adapter.luid);
        if (dxgi_adapter == nullptr) {
            problems.add(label + ": the adapter cannot be reopened by LUID");
            continue;
        }
        int max11 = -1;
        std::vector<int> supported11;
        for (D3D_FEATURE_LEVEL level : kLevels) {
            ID3D11Device* device = nullptr;
            const HRESULT result =
                create11(static_cast<IDXGIAdapter*>(dxgi_adapter), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                         &level, 1, D3D11_SDK_VERSION, &device, nullptr, nullptr);
            const bool ok = SUCCEEDED(result) && device != nullptr;
            if (ok) {
                device->Release();
                supported11.push_back(level);
                max11 = std::max(max11, static_cast<int>(level));
            }
        }
        for (D3D_FEATURE_LEVEL level : kLevels) {
            const bool ok = std::find(supported11.begin(), supported11.end(), level) != supported11.end();
            if (!ok && level < max11) {
                problems.add(label + ": D3D11 supports " + level_name(max11) + " but not the lower " +
                             level_name(level));
            }
        }
        int max12 = -1;
        if (create12 != nullptr) {
            const D3D_FEATURE_LEVEL levels12[] = {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
                                                  D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
            for (D3D_FEATURE_LEVEL level : levels12) {
                if (SUCCEEDED(create12(dxgi_adapter, level, __uuidof(ID3D12Device), nullptr))) {
                    max12 = std::max(max12, static_cast<int>(level));
                } else if (max12 >= 0) {
                    problems.add(label + ": D3D12 supports " + level_name(max12) + " but not the lower " +
                                 level_name(level));
                }
            }
        }
        dxgi_adapter->Release();
        context.measure(adapter.luid_text() + ".d3d11.max", level_name(max11));
        context.measure(adapter.luid_text() + ".d3d12.max", level_name(max12));
        if (max11 < 0) {
            problems.add(label + ": D3D11 creates no device at any feature level");
        }
        if (max12 >= 0 && max11 < std::min(max12, static_cast<int>(D3D_FEATURE_LEVEL_12_1))) {
            problems.add(label + ": D3D12 supports " + level_name(max12) + " but D3D11 only " +
                         level_name(max11));
        }
        if (max11 >= D3D_FEATURE_LEVEL_11_0) {
            bool found = false;
            for (const VulkanDevice& device : vk_devices) {
                found = found || (device.luid_valid && AvAdapters::key(device.luid) == adapter.luid_key());
            }
            if (!found) {
                problems.add(label + ": D3D11 feature level " + level_name(max11) +
                             " but Vulkan has no device with this LUID");
            }
        }
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::enumeration
