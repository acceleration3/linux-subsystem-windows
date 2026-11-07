#pragma once

#include "core/text.hpp"
#include "support/d3d10_rig.hpp"
#include "support/d3d10_tools.hpp"
#include "support/problems.hpp"
#include "support/target_adapter.hpp"

#include <string>

namespace lsw::suites::sanity::d3d10 {
class Kit {
public:
    [[nodiscard]] static bool open(AvAdapter& adapter, D3d10Device& device, bool with_1, std::string& error) {
        if (!TargetAdapter::pick(adapter, error)) {
            return false;
        }
        return with_1 ? device.create1(adapter.luid, 0, D3D10_FEATURE_LEVEL_10_1, error) : device.create(adapter.luid, 0, error);
    }

    [[nodiscard]] static const char* name(bool with_1) { return with_1 ? "D3D10.1" : "D3D10"; }

    [[nodiscard]] static std::string hr(HRESULT result) { return D3d10Tools::hr(result); }

    [[nodiscard]] static bool documented_creation_failure(HRESULT result) {
        return result == E_INVALIDARG || result == DXGI_ERROR_INVALID_CALL || result == DXGI_ERROR_UNSUPPORTED || result == DXGI_ERROR_NOT_FOUND ||
               result == E_FAIL || result == DXGI_ERROR_SDK_COMPONENT_MISSING || result == E_NOINTERFACE;
    }

    template <typename U>
    [[nodiscard]] static Com<U> query(IUnknown* object) {
        U* other = nullptr;
        if (object != nullptr) {
            object->QueryInterface(__uuidof(U), reinterpret_cast<void**>(&other));
        }
        return Com<U>(other);
    }

    [[nodiscard]] static std::uint32_t pixel_at(const TexelImage& image, UINT x, UINT y) { return image.pixel(x, y); }

    [[nodiscard]] static bool uniform(const TexelImage& image, std::uint32_t value, std::string& where) {
        for (UINT y = 0; y < image.height; ++y) {
            for (UINT x = 0; x < image.width; ++x) {
                if (image.pixel(x, y) != value) {
                    where = "pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is 0x" + Text::hex(image.pixel(x, y), 8) + ", expected 0x" + Text::hex(value, 8);
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] static std::uint32_t frame_color(int k) {
        return D3d10Tools::bgra((k * 37 + 11) & 255, (k * 91 + 5) & 255, (k * 53 + 200) & 255, 255);
    }

    static void frame_floats(int k, float out[4]) {
        const std::uint32_t c = frame_color(k);
        out[0] = static_cast<float>((c >> 16) & 255u) / 255.0f;
        out[1] = static_cast<float>((c >> 8) & 255u) / 255.0f;
        out[2] = static_cast<float>(c & 255u) / 255.0f;
        out[3] = 1.0f;
    }
};
}  // namespace lsw::suites::sanity::d3d10
