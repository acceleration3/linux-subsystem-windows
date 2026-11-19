#pragma once

#include <chrono>

namespace lsw::linkproto::heartbeat {
inline constexpr const char* kEvent = "heartbeat";
inline constexpr std::chrono::seconds kInterval{5};
inline constexpr std::chrono::seconds kSilenceLimit{20};
inline constexpr std::chrono::seconds kFirstBeatLimit{3};

}  // namespace lsw::linkproto::heartbeat
