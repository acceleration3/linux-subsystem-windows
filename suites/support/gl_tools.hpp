#pragma once

#include "core/context.hpp"
#include "support/gl_api.hpp"
#include "support/problems.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
class GlTools {
public:
    [[nodiscard]] static std::uint32_t rgba(int r, int g, int b, int a) {
        return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) |
               (static_cast<std::uint32_t>(b) << 16) | (static_cast<std::uint32_t>(a) << 24);
    }

    [[nodiscard]] static bool same_image(Context& context, const std::string& name, const std::vector<std::uint8_t>& expected,
                                         const std::vector<std::uint8_t>& actual, int width, int height, int tolerance,
                                         std::string& detail);

    [[nodiscard]] static bool wait_fence(const GlApi& api, unsigned long long timeout_ns, unsigned& result);

    static void expect_error(Problems& problems, const GlApi& api, const std::string& what, unsigned wanted);

    static void expect_clean(Problems& problems, const GlApi& api, const std::string& what);

    [[nodiscard]] static std::string buffer_flags(unsigned flags);

    [[nodiscard]] static std::string hex(unsigned value) { return "0x" + to_hex(value); }

    [[nodiscard]] static std::string wait_name(unsigned result);

private:
    [[nodiscard]] static std::string to_hex(unsigned value);
};
}  // namespace lsw::suites
