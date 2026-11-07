#pragma once

#include "core/text.hpp"
#include "support/d3d11_tools.hpp"
#include "support/problems.hpp"
#include "support/target_adapter.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace lsw::suites::sanity::d3d11 {
class Kit {
public:
    template <typename E>
    [[nodiscard]] static E raw_enum(unsigned value) {
        E result;
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    template <typename U>
    [[nodiscard]] static Com<U> query(IUnknown* object) {
        U* other = nullptr;
        if (object != nullptr) {
            object->QueryInterface(__uuidof(U), reinterpret_cast<void**>(&other));
        }
        return Com<U>(other);
    }

    [[nodiscard]] static bool open(AvAdapter& adapter, D3d11Device& device, std::string& error, UINT flags = 0) {
        return TargetAdapter::pick(adapter, error) && device.create_ex(adapter.luid, nullptr, 0, flags, error);
    }

    [[nodiscard]] static Com<ID3D11PixelShader> color_ps(D3d11Device& device, const char* components, std::string& error) {
        return D3d11Tools::ps(device, std::string("float4 main() : SV_Target { return float4(") + components + "); }", error);
    }

    [[nodiscard]] static std::uint32_t rgba(int r, int g, int b, int a) {
        return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) | (static_cast<std::uint32_t>(b) << 16) |
               (static_cast<std::uint32_t>(a) << 24);
    }

    [[nodiscard]] static bool uniform(const TexelImage& image, std::uint32_t value, std::string& where) {
        for (UINT y = 0; y < image.height; ++y) {
            for (UINT x = 0; x < image.width; ++x) {
                if (image.pixel(x, y) != value) {
                    where = "pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is 0x" + Text::hex(image.pixel(x, y), 8) +
                            ", expected 0x" + Text::hex(value, 8);
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] static bool read_rgba(D3d11Device& device, ID3D11Texture2D* texture, TexelImage& image, std::string& error) {
        return D3d11Tools::read_texture(device, texture, 0, image, error);
    }
};
}  // namespace lsw::suites::sanity::d3d11
