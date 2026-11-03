#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace lsw::agent::win {
class EventLogReader {
public:
    EventLogReader() = delete;

    struct Event {
        unsigned id = 0;
        std::wstring provider;
        std::map<std::wstring, std::wstring> data;
        std::vector<std::wstring> values;
        std::wstring message;
    };

    struct Query {
        std::wstring channel;
        std::wstring provider;
        std::vector<unsigned> ids;
        unsigned max_age_ms = 0;
        bool with_message = false;
        std::size_t limit = 32;
    };

    [[nodiscard]] static std::vector<Event> newest_first(const Query& query);
};

}
