#pragma once

#include <chrono>

namespace lsw::agent::session {

// What the service and a session helper say to each other besides the
// manager's own requests, replies and events (which cross unchanged).
//
// These topics never leave the guest: the service consumes them, so
// they are not part of the manager link's vocabulary in
// ``lsw/linkproto/commands.hpp``. Dotted so they can never collide with
// a topic of that vocabulary.
namespace helper_topic {

// Helper -> service, once, as soon as it is connected:
// ``{"pid": N, "session": N}``.
inline constexpr const char* kHello = "lsw.helper.hello";

// Helper -> service, once, when its desktop gate has decided:
// ``{"ready": bool, "waited_s": N}``. ``ready`` false means the gate gave
// up waiting; the service has its own fallback for that.
inline constexpr const char* kDesktop = "lsw.helper.desktop";

// Helper -> service: one of the helper's own log records,
// ``{"level": "INFO", "msg": "..."}``, so the manager sees them in the
// one log stream it already reads.
inline constexpr const char* kLog = "lsw.helper.log";

// Service -> helper: exit now (the service is stopping).
inline constexpr const char* kShutdown = "lsw.helper.shutdown";

}  // namespace helper_topic

// How long a forwarded request may take before the service answers the
// manager with an error itself. Under the manager's own call timeout
// (AgentRpcClient::kDefaultCallTimeout, 10 s), so the manager gets a
// reason instead of a timeout.
inline constexpr std::chrono::milliseconds kHelperCallTimeout{8000};

// How long a freshly launched helper has to dial its pipe.
inline constexpr std::chrono::seconds kHelperConnectTimeout{30};

// How long a forwarded request waits for a helper that is being
// (re)started in the target session before failing.
inline constexpr std::chrono::seconds kHelperStartWait{5};

// A write to the pipe that does not complete in this long means the
// other end has stopped reading; the pipe is dropped.
inline constexpr std::chrono::seconds kPipeWriteTimeout{5};

}  // namespace lsw::agent::session
