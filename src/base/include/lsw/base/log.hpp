#pragma once

#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif

#include <atomic>
#include <spdlog/spdlog.h>

namespace lsw::base {
inline std::atomic<int> g_log_threshold{spdlog::level::info};

[[nodiscard]] inline bool level_enabled(spdlog::level::level_enum level) noexcept {
    return static_cast<int>(level) >= g_log_threshold.load(std::memory_order_relaxed);
}

}

#define LSW_LOG_AT(level, macro, ...)                         \
    do {                                                      \
        if (::lsw::base::level_enabled(level)) {              \
            macro(__VA_ARGS__);                               \
        }                                                     \
    } while (0)

#define LSW_TRACE(...) LSW_LOG_AT(::spdlog::level::trace, SPDLOG_TRACE, __VA_ARGS__)
#define LSW_DEBUG(...) LSW_LOG_AT(::spdlog::level::debug, SPDLOG_DEBUG, __VA_ARGS__)
#define LSW_INFO(...) LSW_LOG_AT(::spdlog::level::info, SPDLOG_INFO, __VA_ARGS__)
#define LSW_WARN(...) LSW_LOG_AT(::spdlog::level::warn, SPDLOG_WARN, __VA_ARGS__)
#define LSW_ERROR(...) LSW_LOG_AT(::spdlog::level::err, SPDLOG_ERROR, __VA_ARGS__)
