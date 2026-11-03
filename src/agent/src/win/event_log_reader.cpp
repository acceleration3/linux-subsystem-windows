#include "lsw/agent/win/event_log_reader.hpp"

#include "lsw/agent/win/unique_handle.hpp"

#include <windows.h>
#include <winevt.h>

namespace lsw::agent::win {

namespace {
struct EvtHandleTraits {
    using Handle = EVT_HANDLE;

    static Handle empty() noexcept { return nullptr; }

    static bool valid(Handle h) noexcept { return h != nullptr; }

    static void close(Handle h) noexcept { ::EvtClose(h); }
};

using UniqueEvt = UniqueResource<EvtHandleTraits>;

std::wstring build_xpath(const EventLogReader::Query& query) {
    std::wstring ids;
    for (const unsigned id : query.ids) {
        ids += (ids.empty() ? L"" : L" or ") + std::wstring(L"EventID=") + std::to_wstring(id);
    }
    std::wstring system = L"System[";
    std::wstring joined;
    const auto append = [&](const std::wstring& clause) {
        joined += (joined.empty() ? L"" : L" and ") + clause;
    };
    if (!query.provider.empty()) {
        append(L"Provider[@Name='" + query.provider + L"']");
    }
    if (!ids.empty()) {
        append(L"(" + ids + L")");
    }
    if (query.max_age_ms != 0) {
        append(L"TimeCreated[timediff(@SystemTime) <= " + std::to_wstring(query.max_age_ms) + L"]");
    }
    return L"*[" + system + joined + L"]]";
}

std::wstring decode(std::wstring s) {
    static const std::pair<const wchar_t*, const wchar_t*> entities[] = {
        {L"&lt;", L"<"}, {L"&gt;", L">"}, {L"&quot;", L"\""}, {L"&apos;", L"'"}, {L"&amp;", L"&"}};
    for (const auto& [from, to] : entities) {
        const std::wstring needle = from;
        for (auto at = s.find(needle); at != std::wstring::npos; at = s.find(needle, at + 1)) {
            s.replace(at, needle.size(), to);
        }
    }
    return s;
}

std::wstring attribute(const std::wstring& tag, const std::wstring& name) {
    const std::wstring marker = name + L"=";
    const auto at = tag.find(marker);
    if (at == std::wstring::npos || at + marker.size() >= tag.size()) {
        return {};
    }
    const wchar_t quote = tag[at + marker.size()];
    const auto end = tag.find(quote, at + marker.size() + 1);
    if (end == std::wstring::npos) {
        return {};
    }
    return decode(tag.substr(at + marker.size() + 1, end - at - marker.size() - 1));
}

std::wstring element_text(const std::wstring& xml, const std::wstring& name) {
    const auto open = xml.find(L"<" + name);
    if (open == std::wstring::npos) {
        return {};
    }
    const auto close = xml.find(L'>', open);
    const auto end = xml.find(L"</" + name + L">", open);
    if (close == std::wstring::npos || end == std::wstring::npos || end < close) {
        return {};
    }
    return decode(xml.substr(close + 1, end - close - 1));
}

void parse_xml(const std::wstring& xml, EventLogReader::Event& event) {
    event.id = static_cast<unsigned>(std::wcstoul(element_text(xml, L"EventID").c_str(), nullptr, 10));
    const auto provider = xml.find(L"<Provider");
    if (provider != std::wstring::npos) {
        event.provider = attribute(xml.substr(provider, xml.find(L'>', provider) - provider), L"Name");
    }
    const auto section = xml.find(L"<EventData");
    if (section == std::wstring::npos) {
        return;
    }
    for (auto at = xml.find(L"<Data", section); at != std::wstring::npos;
         at = xml.find(L"<Data", at + 1)) {
        const auto tag_end = xml.find(L'>', at);
        if (tag_end == std::wstring::npos) {
            break;
        }
        const std::wstring tag = xml.substr(at, tag_end - at);
        if (tag.size() > 5 && tag[5] != L' ' && tag[5] != L'/') {
            continue;
        }
        std::wstring value;
        if (tag.empty() || tag.back() != L'/') {
            const auto end = xml.find(L"</Data>", tag_end);
            if (end == std::wstring::npos) {
                break;
            }
            value = decode(xml.substr(tag_end + 1, end - tag_end - 1));
        }
        if (const std::wstring name = attribute(tag, L"Name"); !name.empty()) {
            event.data[name] = value;
        }
        event.values.push_back(value);
    }
}

std::wstring render(EVT_HANDLE event) {
    DWORD used = 0;
    DWORD count = 0;
    ::EvtRender(nullptr, event, EvtRenderEventXml, 0, nullptr, &used, &count);
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return {};
    }
    std::wstring xml(used / sizeof(wchar_t) + 1, L'\0');
    if (::EvtRender(nullptr, event, EvtRenderEventXml, static_cast<DWORD>(xml.size() * sizeof(wchar_t)),
                    xml.data(), &used, &count) == FALSE) {
        return {};
    }
    xml.resize(wcslen(xml.c_str()));
    return xml;
}

std::wstring format_message(EVT_HANDLE event, const std::wstring& provider) {
    const UniqueEvt metadata{::EvtOpenPublisherMetadata(nullptr, provider.c_str(), nullptr, 0, 0)};
    if (!metadata) {
        return {};
    }
    DWORD used = 0;
    ::EvtFormatMessage(metadata.get(), event, 0, 0, nullptr, EvtFormatMessageEvent, 0, nullptr, &used);
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return {};
    }
    std::wstring text(used + 1, L'\0');
    if (::EvtFormatMessage(metadata.get(), event, 0, 0, nullptr, EvtFormatMessageEvent,
                           static_cast<DWORD>(text.size()), text.data(), &used) == FALSE) {
        return {};
    }
    text.resize(wcslen(text.c_str()));
    return text;
}

}

std::vector<EventLogReader::Event> EventLogReader::newest_first(const Query& query) {
    std::vector<Event> out;
    const UniqueEvt results{::EvtQuery(nullptr, query.channel.c_str(), build_xpath(query).c_str(),
                                       EvtQueryChannelPath | EvtQueryReverseDirection)};
    if (!results) {
        return out;
    }
    EVT_HANDLE batch[16] = {};
    while (out.size() < query.limit) {
        DWORD returned = 0;
        if (::EvtNext(results.get(), 16, batch, 1000, 0, &returned) == FALSE) {
            break;
        }
        for (DWORD i = 0; i < returned; ++i) {
            const UniqueEvt handle{batch[i]};
            if (out.size() >= query.limit) {
                continue;
            }
            Event event;
            parse_xml(render(handle.get()), event);
            if (query.with_message) {
                event.message = format_message(handle.get(), event.provider);
            }
            out.push_back(std::move(event));
        }
    }
    return out;
}

}
