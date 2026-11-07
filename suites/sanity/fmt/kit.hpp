#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"
#include "support/api_policy.hpp"
#include "support/artifact_writer.hpp"
#include "support/d3d11_device.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_rig.hpp"
#include "support/format_codec.hpp"
#include "support/format_table.hpp"
#include "support/problems.hpp"
#include "support/target_adapter.hpp"

#include <string>
#include <vector>

namespace lsw::suites::sanity::fmt {
class Kit {
public:
    static constexpr UINT kSampleBits = D3D11_FORMAT_SUPPORT_SHADER_LOAD | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;

    [[nodiscard]] static bool enabled(const char* api) { return !ApiPolicy::is_skipped(api); }

    [[nodiscard]] static bool open_d3d11(AvAdapter& adapter, D3d11Device& device, std::string& error, UINT flags = 0) {
        return TargetAdapter::pick(adapter, error) && device.create_ex(adapter.luid, nullptr, 0, flags, error);
    }

    [[nodiscard]] static bool open_d3d12(D3d12Rig& rig, std::string& error) {
        AvAdapter adapter;
        return TargetAdapter::pick(adapter, error) && rig.open(adapter, D3D_FEATURE_LEVEL_11_0, error);
    }

    [[nodiscard]] static Outcome finish(const std::vector<std::string>& skips, const Problems& problems, bool exercised) {
        if (!problems.empty()) {
            return problems.outcome();
        }
        if (!exercised) {
            std::string reason;
            for (const std::string& item : skips) {
                reason += (reason.empty() ? "" : "; ") + item;
            }
            return Context::skip(reason.empty() ? "no API of this row is enabled" : reason);
        }
        return Outcome::ok();
    }

    [[nodiscard]] static std::string number_name(int number) {
        if (FormatTable::find(static_cast<DXGI_FORMAT>(number)) != nullptr) {
            return FormatTable::name(static_cast<DXGI_FORMAT>(number));
        }
        const struct {
            int number;
            const char* name;
        } extra[] = {{1, "R32G32B32A32_TYPELESS"}, {5, "R32G32B32_TYPELESS"}, {9, "R16G16B16A16_TYPELESS"}, {15, "R32G32_TYPELESS"},
                     {19, "R32G8X24_TYPELESS"}, {20, "D32_FLOAT_S8X24_UINT"}, {21, "R32_FLOAT_X8X24_TYPELESS"}, {22, "X32_TYPELESS_G8X24_UINT"},
                     {23, "R10G10B10A2_TYPELESS"}, {27, "R8G8B8A8_TYPELESS"}, {33, "R16G16_TYPELESS"}, {39, "R32_TYPELESS"}, {40, "D32_FLOAT"},
                     {44, "R24G8_TYPELESS"}, {45, "D24_UNORM_S8_UINT"}, {46, "R24_UNORM_X8_TYPELESS"}, {47, "X24_TYPELESS_G8_UINT"},
                     {48, "R8G8_TYPELESS"}, {53, "R16_TYPELESS"}, {55, "D16_UNORM"}, {60, "R8_TYPELESS"}, {87, "B8G8R8A8_UNORM"},
                     {90, "B8G8R8A8_TYPELESS"}, {91, "B8G8R8A8_UNORM_SRGB"}, {89, "R10G10B10_XR_BIAS_A2_UNORM"}, {103, "NV12"},
                     {104, "P010"}, {107, "YUY2"}, {100, "AYUV"}};
        for (const auto& item : extra) {
            if (item.number == number) {
                return item.name;
            }
        }
        return "format " + std::to_string(number);
    }

    [[nodiscard]] static std::string hex_bytes(const std::vector<std::uint8_t>& data, std::size_t offset, std::size_t count) {
        static const char* digits = "0123456789abcdef";
        std::string out;
        for (std::size_t i = offset; i < offset + count && i < data.size(); ++i) {
            out.push_back(digits[data[i] >> 4]);
            out.push_back(digits[data[i] & 15]);
            out.push_back(' ');
        }
        return out;
    }

    [[nodiscard]] static std::size_t first_difference(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
        const std::size_t common = a.size() < b.size() ? a.size() : b.size();
        for (std::size_t i = 0; i < common; ++i) {
            if (a[i] != b[i]) {
                return i;
            }
        }
        return a.size() == b.size() ? static_cast<std::size_t>(-1) : common;
    }
};
}  // namespace lsw::suites::sanity::fmt
