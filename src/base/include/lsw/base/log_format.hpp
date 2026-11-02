#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lsw::base {
class LogFormat {
public:
    LogFormat() = delete;

    [[nodiscard]] static const char* level_name(int level) noexcept;

    [[nodiscard]] static int level_from_name(std::string_view name, int fallback) noexcept;

    static void append_line(std::string& out, std::int64_t ts_ms, int level,
                            std::string_view source, std::string_view text);
};

}
