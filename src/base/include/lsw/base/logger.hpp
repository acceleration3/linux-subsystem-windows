#pragma once

namespace lsw::base {

// Owns the process-wide spdlog configuration. Exactly one concern:
// deciding where log records go and at what level.
//
// Not instantiable — the sink set it configures is process-global by
// construction (spdlog's default logger), so an instance would be a lie.
class Logger {
public:
    Logger() = delete;

    // One-shot setup. ``component`` is a short tag (``icd``, ``render``,
    // ``manager``) that prefixes every line so combined logs stay
    // readable. Reads ``$LSW_LOG`` for the level (``trace``, ``debug``,
    // ``info``, ``warn``, ``error``); default is ``info``. Reads
    // ``$LSW_LOG_FILE`` for an optional file sink (in addition to
    // stderr). Safe to call repeatedly — only the first call has effect.
    static void init(const char* component) noexcept;
};

}  // namespace lsw::base
