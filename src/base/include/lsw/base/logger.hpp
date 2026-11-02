#pragma once

#include "lsw/base/rolling_file.hpp"

#include <cstdint>
#include <functional>
#include <spdlog/common.h>
#include <string>
#include <string_view>

namespace lsw::base {
class Logger {
public:
    Logger() = delete;

    using Observer = std::function<void(int level, std::string_view source, std::string_view text,
                                        std::int64_t ts_ms)>;

    static void init(const char* component) noexcept;

    static void attach(const std::filesystem::path& file, RollingFile::Limits limits,
                       Observer observer);
    static void detach() noexcept;

    static void set_level(spdlog::level::level_enum level) noexcept;
    [[nodiscard]] static spdlog::level::level_enum level() noexcept;
    [[nodiscard]] static bool level_overridden() noexcept;

    static void write(spdlog::level::level_enum level, std::string_view source,
                      std::string_view text);

    static void flush_repeats();
};

}
