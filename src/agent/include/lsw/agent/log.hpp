#pragma once

#include "lsw/agent/win/rolling_file.hpp"

#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace lsw::agent::log {

enum class Level : int {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
};

[[nodiscard]] const char* level_name(Level lvl);

[[nodiscard]] Level level_from_name(std::string_view name);

namespace detail {
extern std::atomic<int> g_min_level;
}

[[nodiscard]] inline bool enabled(Level level) {
    return static_cast<int>(level) >= detail::g_min_level.load(std::memory_order_relaxed);
}

void set_level(Level level);
[[nodiscard]] Level level();

void set_file(const std::wstring& path, win::RollingFile::Limits limits);

void set_source(std::string source);

void flush(unsigned timeout_ms);

struct Record {
    std::uint64_t seq = 0;
    std::uint64_t ts_ms = 0;
    Level level = Level::Info;
    std::string source;
    std::string msg;
};

void append(Level level, const std::string& msg);

void append_from(Level level, std::string_view source, const std::string& msg,
                 std::uint64_t ts_ms);

[[nodiscard]] std::vector<Record> collect_since(std::uint64_t since, std::uint64_t& next_since);

[[nodiscard]] inline std::string format(std::string message) {
    return message;
}

namespace detail {
void append_wide(std::string& out, std::wstring_view value);

template<typename T>
void append_arg(std::string& out, const T& value) {
    if constexpr (std::is_convertible_v<const T&, const wchar_t*>) {
        const wchar_t* text = value;
        append_wide(out, text != nullptr ? std::wstring_view{text} : std::wstring_view{L"(null)"});
    } else if constexpr (std::is_convertible_v<const T&, std::wstring_view>) {
        append_wide(out, std::wstring_view{value});
    } else {
        std::ostringstream oss;
        oss << value;
        out += oss.str();
    }
}

inline void append_arg(std::string& out, const std::string& value) {
    out += value;
}

inline void append_arg(std::string& out, const char* value) {
    out += (value != nullptr) ? value : "(null)";
}

inline void append_arg(std::string& out, bool value) {
    out += value ? "true" : "false";
}

}

template<typename... Args>
    requires(sizeof...(Args) > 0)
[[nodiscard]] std::string format(std::string_view fmt, const Args&... args) {
    std::string out;
    out.reserve(fmt.size() + (16 * sizeof...(Args)));

    std::vector<std::string> rendered;
    rendered.reserve(sizeof...(Args));
    (..., (rendered.emplace_back(), detail::append_arg(rendered.back(), args)));

    std::size_t next = 0;
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{') {
            out += '{';
            ++i;
            continue;
        }
        if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
            if (next < rendered.size()) {
                out += rendered[next++];
            } else {
                out += "{}";
            }
            ++i;
            continue;
        }
        out += fmt[i];
    }
    for (; next < rendered.size(); ++next) {
        out += (next == 0 ? " | " : ", ");
        out += rendered[next];
    }
    return out;
}

}

#define LSW_AGENT_LOG(level, ...)                                                                  \
    if (!::lsw::agent::log::enabled(level)) {                                                      \
    } else                                                                                         \
        ::lsw::agent::log::append(level, ::lsw::agent::log::format(__VA_ARGS__))
#define LSW_AGENT_TRACE(...) LSW_AGENT_LOG(::lsw::agent::log::Level::Trace, __VA_ARGS__)
#define LSW_AGENT_DEBUG(...) LSW_AGENT_LOG(::lsw::agent::log::Level::Debug, __VA_ARGS__)
#define LSW_AGENT_INFO(...)  LSW_AGENT_LOG(::lsw::agent::log::Level::Info, __VA_ARGS__)
#define LSW_AGENT_WARN(...)  LSW_AGENT_LOG(::lsw::agent::log::Level::Warn, __VA_ARGS__)
#define LSW_AGENT_ERROR(...) LSW_AGENT_LOG(::lsw::agent::log::Level::Error, __VA_ARGS__)
