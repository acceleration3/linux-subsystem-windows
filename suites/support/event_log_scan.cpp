#include "support/event_log_scan.hpp"

#include "core/text.hpp"
#include "support/dyn_library.hpp"

#include <winevt.h>

namespace lsw::suites {

EventLogHits EventLogScan::recent(const std::wstring& channel, const std::vector<int>& event_ids,
                                  long long window_ms) {
    EventLogHits hits;
    DynLibrary library;
    if (!library.load("wevtapi.dll")) {
        return hits;
    }
    auto query = library.symbol<decltype(&EvtQuery)>("EvtQuery");
    auto next = library.symbol<decltype(&EvtNext)>("EvtNext");
    auto render = library.symbol<decltype(&EvtRender)>("EvtRender");
    auto close = library.symbol<decltype(&EvtClose)>("EvtClose");
    if (query == nullptr || next == nullptr || render == nullptr || close == nullptr) {
        return hits;
    }
    std::wstring ids;
    for (std::size_t i = 0; i < event_ids.size(); ++i) {
        ids += (i == 0 ? L"EventID=" : L" or EventID=") + std::to_wstring(event_ids[i]);
    }
    const std::wstring xpath = L"*[System[(" + ids + L") and TimeCreated[timediff(@SystemTime) <= " +
                               std::to_wstring(window_ms) + L"]]]";
    EVT_HANDLE results =
        query(nullptr, channel.c_str(), xpath.c_str(), EvtQueryChannelPath | EvtQueryForwardDirection);
    if (results == nullptr) {
        return hits;
    }
    hits.channel_available = true;
    for (;;) {
        EVT_HANDLE event = nullptr;
        DWORD returned = 0;
        if (next(results, 1, &event, 2000, 0, &returned) == FALSE || returned == 0) {
            break;
        }
        DWORD used = 0;
        DWORD properties = 0;
        render(nullptr, event, EvtRenderEventXml, 0, nullptr, &used, &properties);
        std::vector<wchar_t> xml(used / sizeof(wchar_t) + 1, L'\0');
        if (render(nullptr, event, EvtRenderEventXml, used, xml.data(), &used, &properties) != FALSE) {
            std::string text = Text::utf8(xml.data());
            hits.events.push_back(text.substr(0, 600));
        } else {
            hits.events.push_back("event (xml unreadable)");
        }
        close(event);
    }
    close(results);
    return hits;
}

}  // namespace lsw::suites
