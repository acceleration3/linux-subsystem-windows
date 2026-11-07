#pragma once

#include <string>
#include <vector>

namespace lsw::suites {
struct EventLogHits {
    bool channel_available = false;
    std::vector<std::string> events;
};

class EventLogScan {
public:
    [[nodiscard]] static EventLogHits recent(const std::wstring& channel,
                                             const std::vector<int>& event_ids, long long window_ms);
};
}  // namespace lsw::suites
