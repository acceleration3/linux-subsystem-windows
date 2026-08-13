#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace lsw::agent::log {

// Severity levels in ascending order. The level is part of the wire
// format and the file format; never renumber.
enum class Level : int {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

[[nodiscard]] const char* level_name(Level lvl);

// One log record. ``seq`` is monotonically increasing per agent
// process — the manager polls with the highest seq it has seen.
struct Record {
    std::uint64_t seq = 0;
    std::uint64_t ts_ms = 0;  // wall-clock GetSystemTimeAsFileTime, ms since unix epoch
    Level level = Level::Info;
    std::string msg;
};

// Append a record. Writes to ``C:\ProgramData\LSW\agent.log`` and
// pushes onto the in-memory ring (kRingCapacity) for later
// ``collect_since`` calls.
//
// Thread-safe; serialized internally so the file output stays line-
// atomic across threads.
void append(Level level, const std::string& msg);

// Returns every record with ``seq > since``, oldest first. Also
// fills ``next_since`` with the highest seq returned (or ``since`` if
// nothing new). If the ring has wrapped past ``since`` (caller fell
// way behind) the returned records start from the oldest still
// retained — the caller will detect this from the gap.
[[nodiscard]] std::vector<Record> collect_since(std::uint64_t since,
                                                  std::uint64_t& next_since);

// ---- Message formatting -------------------------------------------------
//
// The host side logs with ``LSW_WARN("x={}", x)`` (spdlog). The agent
// cannot use spdlog — there is no mingw package for it, and agent
// records have to land in the ring above so they can be pushed to the
// manager as ``log`` events. So the ring stays, and only the CALL SITE
// converges: the macros below take the same brace-substitution form, and
// the agent stops building messages by string concatenation.
//
// Substitution happens ONLY when arguments are supplied. A single
// argument is logged verbatim — agent messages routinely carry JSON
// dumps, and those are full of braces that must not be read as
// placeholders.

// Single argument: pass through untouched.
[[nodiscard]] inline std::string format(std::string message) { return message; }

namespace detail {

// Append one argument in the spdlog/fmt style: strings as-is,
// everything else through the stream operator.
template <typename T>
void append_arg(std::string& out, const T& value) {
    std::ostringstream oss;
    oss << value;
    out += oss.str();
}

inline void append_arg(std::string& out, const std::string& value) { out += value; }
inline void append_arg(std::string& out, const char* value) {
    out += (value != nullptr) ? value : "(null)";
}
inline void append_arg(std::string& out, bool value) { out += value ? "true" : "false"; }

}  // namespace detail

// Brace substitution. Each ``{}`` in ``fmt`` consumes the next argument;
// surplus arguments are appended in a trailing " | " group rather than
// dropped, because a log line that silently loses data is worse than an
// ugly one. ``{{`` emits a literal brace.
template <typename... Args>
    requires(sizeof...(Args) > 0)
[[nodiscard]] std::string format(std::string_view fmt, const Args&... args) {
    std::string out;
    out.reserve(fmt.size() + (16 * sizeof...(Args)));

    // Collect the arguments as strings first so the walk below is a
    // plain loop rather than a parameter-pack dance.
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
                out += "{}";  // more placeholders than arguments
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

}  // namespace lsw::agent::log

// Convenience macros mirroring lsw/base/log.hpp's shape, so both sides of
// the VM boundary read the same at the call site:
//
//   LSW_AGENT_WARN("bind(:{}) failed wsa_err={}", port, err);
//   LSW_AGENT_INFO(already_built_string);
#define LSW_AGENT_INFO(...) \
    ::lsw::agent::log::append(::lsw::agent::log::Level::Info, ::lsw::agent::log::format(__VA_ARGS__))
#define LSW_AGENT_WARN(...) \
    ::lsw::agent::log::append(::lsw::agent::log::Level::Warn, ::lsw::agent::log::format(__VA_ARGS__))
#define LSW_AGENT_ERROR(...) \
    ::lsw::agent::log::append(::lsw::agent::log::Level::Error, ::lsw::agent::log::format(__VA_ARGS__))
#define LSW_AGENT_DEBUG(...) \
    ::lsw::agent::log::append(::lsw::agent::log::Level::Debug, ::lsw::agent::log::format(__VA_ARGS__))
