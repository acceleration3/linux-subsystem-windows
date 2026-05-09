#pragma once

#include <cstdint>
#include <string>
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

}  // namespace lsw::agent::log

// Thin convenience macros to mirror lsw/log.hpp shape — agent code
// can stay short. No formatting; pass an already-built string.
#define LSW_AGENT_INFO(msg)  ::lsw::agent::log::append(::lsw::agent::log::Level::Info, (msg))
#define LSW_AGENT_WARN(msg)  ::lsw::agent::log::append(::lsw::agent::log::Level::Warn, (msg))
#define LSW_AGENT_ERROR(msg) ::lsw::agent::log::append(::lsw::agent::log::Level::Error, (msg))
#define LSW_AGENT_DEBUG(msg) ::lsw::agent::log::append(::lsw::agent::log::Level::Debug, (msg))
