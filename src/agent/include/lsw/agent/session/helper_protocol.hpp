#pragma once

#include <chrono>

namespace lsw::agent::session {
namespace helper_topic {
inline constexpr const char* kHello = "lsw.helper.hello";

inline constexpr const char* kDesktop = "lsw.helper.desktop";

inline constexpr const char* kLog = "lsw.helper.log";

inline constexpr const char* kShutdown = "lsw.helper.shutdown";

}

namespace helper_cmd {

inline constexpr const char* kSetLogLevel = "lsw.helper.set_log_level";

}

inline constexpr std::chrono::milliseconds kHelperCallTimeout{8000};

inline constexpr std::chrono::seconds kHelperConnectTimeout{30};

inline constexpr std::chrono::seconds kHelperStartWait{5};

inline constexpr std::chrono::seconds kPipeWriteTimeout{5};

}
