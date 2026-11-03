#include "lsw/agent/apps/exit_reporter.hpp"

#include "lsw/agent/apps/exit_status.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/util/wide_string.hpp"
#include "lsw/agent/win/event_log_reader.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <windows.h>

namespace lsw::agent::apps {

namespace {
using win::EventLogReader;

constexpr unsigned kApplicationErrorEventId = 1000;
constexpr unsigned kSideBySideEventIds[] = {33, 35, 63, 80};
constexpr wchar_t kApplicationErrorProvider[] = L"Application Error";
constexpr wchar_t kSideBySideProvider[] = L"SideBySide";
constexpr wchar_t kApplicationChannel[] = L"Application";
constexpr wchar_t kSystemChannel[] = L"System";
constexpr wchar_t kDevicePathPrefix[] = L"\\Device\\";
constexpr wchar_t kGlobalRoot[] = L"\\\\?\\GLOBALROOT";
constexpr unsigned kCrashEventAgeMs = 60000;
constexpr unsigned kSideBySideEventAgeMs = 15000;
constexpr auto kEventWait = std::chrono::seconds(5);
constexpr auto kEventPoll = std::chrono::milliseconds(250);

enum CrashField : std::size_t {
    FieldAppVersion = 1,
    FieldModuleName = 3,
    FieldModuleVersion = 4,
    FieldFaultingOffset = 7,
    FieldProcessId = 8,
};

std::string utf8(const std::wstring& s) { return util::TextEncoding::to_utf8(s); }

std::string file_name_of(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    return utf8(slash == std::wstring::npos ? path : path.substr(slash + 1));
}

std::wstring field(const EventLogReader::Event& event, const wchar_t* name, std::size_t index) {
    if (const auto it = event.data.find(name); it != event.data.end()) {
        return it->second;
    }
    return index < event.values.size() ? event.values[index] : std::wstring{};
}

template<class Find>
auto wait_for(Find find) {
    const auto deadline = std::chrono::steady_clock::now() + kEventWait;
    auto found = find();
    while (!found && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(kEventPoll);
        found = find();
    }
    return found;
}

std::wstring openable(const std::wstring& image) {
    return image.starts_with(kDevicePathPrefix) ? kGlobalRoot + image : image;
}

}

ErrorReport ExitReporter::report(const ExitedProcess& exited) const {
    ErrorReport report;
    report.app_id = exited.app_id;
    report.launch = exited.key;
    report.pid = exited.pid;
    report.parent = exited.parent;
    report.image = utf8(exited.image);
    report.exit_code = exited.exit_code;
    report.code_name = ExitStatus::name(exited.exit_code);
    report.kind = ExitStatus::is_load_failure(exited.exit_code) ? "startup" : "crash";

    if (ExitStatus::is_side_by_side(exited.exit_code)) {
        explain_side_by_side(exited, report);
    } else if (ExitStatus::is_load_failure(exited.exit_code)) {
        explain_load_failure(exited, report);
    } else {
        explain_crash(exited, report);
    }
    return report;
}

void ExitReporter::explain_load_failure(const ExitedProcess& exited, ErrorReport& report) const {
    ImportResolver resolver(exited.working_dir, exited.path_env);
    report.problems = resolver.check(openable(exited.image));
    if (report.problems.empty()) {
        report.note = "the executable's imports all resolve";
    }
}

void ExitReporter::explain_side_by_side(const ExitedProcess& exited, ErrorReport& report) const {
    const std::wstring image_name = util::WideString::lower(util::TextEncoding::to_wide(
        file_name_of(exited.image)));
    const auto find = [&]() -> std::optional<EventLogReader::Event> {
        for (const wchar_t* channel : {kSystemChannel, kApplicationChannel}) {
            EventLogReader::Query query;
            query.channel = channel;
            query.provider = kSideBySideProvider;
            query.ids.assign(std::begin(kSideBySideEventIds), std::end(kSideBySideEventIds));
            query.max_age_ms = kSideBySideEventAgeMs;
            query.with_message = true;
            for (auto& event : EventLogReader::newest_first(query)) {
                std::wstring text = event.message;
                for (const auto& value : event.values) {
                    text += L" " + value;
                }
                if (util::WideString::lower(text).find(image_name) != std::wstring::npos) {
                    return std::move(event);
                }
            }
        }
        return std::nullopt;
    };
    const auto event = wait_for(find);
    if (!event) {
        report.note = "no side-by-side event was found in the System or Application log";
        return;
    }
    std::wstring text = event->message;
    if (text.empty()) {
        for (const auto& value : event->values) {
            text += (text.empty() ? L"" : L" ") + value;
        }
    }
    std::vector<std::wstring> lines;
    for (std::size_t at = 0; at < text.size();) {
        auto end = text.find_first_of(L"\r\n", at);
        if (end == std::wstring::npos) {
            end = text.size();
        }
        if (end > at) {
            lines.push_back(text.substr(at, end - at));
        }
        at = end + 1;
    }
    for (const auto& line : lines) {
        const std::string narrow = utf8(line);
        if (std::find(report.event_text.begin(), report.event_text.end(), narrow) ==
            report.event_text.end()) {
            report.event_text.push_back(narrow);
        }
    }
}

void ExitReporter::explain_crash(const ExitedProcess& exited, ErrorReport& report) const {
    const auto find = [&]() -> std::optional<EventLogReader::Event> {
        EventLogReader::Query query;
        query.channel = kApplicationChannel;
        query.provider = kApplicationErrorProvider;
        query.ids = {kApplicationErrorEventId};
        query.max_age_ms = kCrashEventAgeMs;
        for (auto& event : EventLogReader::newest_first(query)) {
            const std::wstring id = field(event, L"ProcessId", FieldProcessId);
            if (!id.empty() && std::wcstoul(id.c_str(), nullptr, 0) == exited.pid) {
                return std::move(event);
            }
        }
        return std::nullopt;
    };
    const auto event = wait_for(find);
    if (!event) {
        return;
    }
    report.module = utf8(field(*event, L"ModuleName", FieldModuleName));
    report.offset = utf8(field(*event, L"FaultingOffset", FieldFaultingOffset));
    if (!report.offset.empty() && !report.offset.starts_with("0x")) {
        report.offset = "0x" + report.offset;
    }
    report.module_version = utf8(field(*event, L"ModuleVersion", FieldModuleVersion));
    report.app_version = utf8(field(*event, L"AppVersion", FieldAppVersion));
}

}
