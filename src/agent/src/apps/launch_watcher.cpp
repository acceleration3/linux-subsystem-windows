#include "lsw/agent/apps/launch_watcher.hpp"

#include "lsw/agent/apps/error_report.hpp"
#include "lsw/agent/apps/exit_reporter.hpp"
#include "lsw/agent/apps/exit_status.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/process_info.hpp"

#include "lsw/linkproto/commands.hpp"

#include <nlohmann/json.hpp>

#include <iterator>
#include <utility>
#include <windows.h>

namespace lsw::agent::apps {

namespace {
constexpr std::size_t kMaxPendingReports = 64;
constexpr std::size_t kMaxChildErrors = 256;

std::string utf8(const std::wstring& s) { return util::TextEncoding::to_utf8(s); }

std::string file_name_of(const std::string& path) {
    const auto slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

const char* kind_name(ImportResolver::Problem::Kind kind) {
    using Kind = ImportResolver::Problem::Kind;
    switch (kind) {
    case Kind::Missing:
        return "missing";
    case Kind::WrongBitness:
        return "wrong_bitness";
    case Kind::MissingExport:
        return "missing_export";
    case Kind::Invalid:
        return "invalid";
    }
    return "invalid";
}

nlohmann::json problem_json(const ImportResolver::Problem& problem) {
    nlohmann::json json{{"kind", kind_name(problem.kind)},
                        {"module", utf8(problem.module)},
                        {"importer", utf8(problem.importer)}};
    if (problem.kind == ImportResolver::Problem::Kind::MissingExport) {
        json["function"] = utf8(problem.function);
    }
    if (problem.kind == ImportResolver::Problem::Kind::WrongBitness) {
        json["module_bits"] = problem.module_bits;
        json["image_bits"] = problem.image_bits;
    }
    return json;
}

nlohmann::json to_json(const ErrorReport& report) {
    nlohmann::json problems = nlohmann::json::array();
    for (const auto& problem : report.problems) {
        problems.push_back(problem_json(problem));
    }
    return {{"kind", report.kind},
            {"app_id", report.app_id},
            {"launch", report.launch},
            {"pid", report.pid},
            {"parent_pid", report.parent},
            {"image", report.image},
            {"exit_code", report.exit_code},
            {"exit_code_hex", ExitStatus::hex(report.exit_code)},
            {"code_name", report.code_name},
            {"problems", problems},
            {"note", report.note},
            {"event_text", report.event_text},
            {"module", report.module},
            {"offset", report.offset},
            {"module_version", report.module_version},
            {"app_version", report.app_version}};
}

std::string problems_text(const std::vector<ImportResolver::Problem>& problems) {
    using Kind = ImportResolver::Problem::Kind;
    std::string missing;
    std::string other;
    for (const auto& problem : problems) {
        const std::string module = utf8(problem.module);
        std::string phrase;
        switch (problem.kind) {
        case Kind::Missing:
            if (missing.find(module) == std::string::npos) {
                missing += (missing.empty() ? "" : ", ") + module;
            }
            continue;
        case Kind::WrongBitness:
            phrase = module + " is " + std::to_string(problem.module_bits) + "-bit but " +
                     utf8(problem.importer) + " is " + std::to_string(problem.image_bits) + "-bit";
            break;
        case Kind::MissingExport:
            phrase = utf8(problem.function) + " missing from " + module;
            break;
        case Kind::Invalid:
            phrase = module + " is not a valid image";
            break;
        }
        other += (other.empty() ? "" : ", ") + phrase;
    }
    std::string text = missing.empty() ? std::string{} : "missing " + missing;
    if (!other.empty()) {
        text += (text.empty() ? "" : "; ") + other;
    }
    return text;
}

std::string detail_text(const ErrorReport& report) {
    std::string text;
    const auto add = [&text](const std::string& part) {
        if (!part.empty()) {
            text += (text.empty() ? "" : "; ") + part;
        }
    };
    add(problems_text(report.problems));
    add(report.note);
    for (const auto& line : report.event_text) {
        add(line);
    }
    if (!report.module.empty()) {
        add("in " + report.module + (report.offset.empty() ? "" : " at " + report.offset));
    }
    return text;
}

}

LaunchWatcher& LaunchWatcher::shared() {
    static LaunchWatcher* watcher = new LaunchWatcher();
    return *watcher;
}

bool LaunchWatcher::attach(void* job, unsigned long key, LaunchIdentity identity) {
    start();
    if (!port_) {
        return false;
    }
    JOBOBJECT_ASSOCIATE_COMPLETION_PORT association{};
    association.CompletionKey = reinterpret_cast<PVOID>(static_cast<ULONG_PTR>(key));
    association.CompletionPort = port_.get();
    if (::SetInformationJobObject(static_cast<HANDLE>(job), JobObjectAssociateCompletionPortInformation,
                                  &association, sizeof(association)) == 0) {
        LSW_AGENT_WARN("launch_watcher: cannot watch the job of pid {} (err={}); its failures "
                       "will not be reported",
                       key, win::ErrorText::last_error());
        return false;
    }
    const std::lock_guard lock(mu_);
    identities_[key] = std::move(identity);
    return true;
}

void LaunchWatcher::start() {
    std::call_once(started_, [this] {
        port_.reset(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1));
        if (!port_) {
            LSW_AGENT_WARN("launch_watcher: cannot create the completion port (err={})",
                           win::ErrorText::last_error());
            return;
        }
        std::thread([this] { run(); }).detach();
    });
}

void LaunchWatcher::run() {
    for (;;) {
        DWORD message = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED overlapped = nullptr;
        if (::GetQueuedCompletionStatus(port_.get(), &message, &key, &overlapped, INFINITE) == 0 &&
            overlapped == nullptr) {
            LSW_AGENT_WARN("launch_watcher: completion port failed (err={}); stopping",
                           win::ErrorText::last_error());
            return;
        }
        const auto pid = static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(overlapped));
        switch (message) {
        case JOB_OBJECT_MSG_NEW_PROCESS:
            on_new_process(static_cast<unsigned long>(key), pid);
            break;
        case JOB_OBJECT_MSG_EXIT_PROCESS:
        case JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS:
            on_exit_process(pid);
            break;
        case JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO:
            on_launch_ended(static_cast<unsigned long>(key));
            break;
        default:
            break;
        }
    }
}

void LaunchWatcher::on_new_process(unsigned long key, unsigned long pid) {
    Tracked tracked;
    tracked.process.reset(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
    if (!tracked.process) {
        LSW_AGENT_DEBUG("launch_watcher: pid {} of launch {} ended before it could be opened (err={})",
                        pid, key, win::ErrorText::last_error());
        return;
    }
    tracked.image = win::ProcessInfo::image_path(tracked.process.get());
    tracked.parent = win::ProcessInfo::parent_pid(tracked.process.get()).value_or(0);
    tracked.created = win::ProcessInfo::creation_time(tracked.process.get()).value_or(0);
    tracked.key = key;
    const std::lock_guard lock(mu_);
    tracked_[pid] = std::move(tracked);
}

void LaunchWatcher::on_exit_process(unsigned long pid) {
    Tracked tracked;
    LaunchIdentity identity;
    {
        const std::lock_guard lock(mu_);
        const auto it = tracked_.find(pid);
        if (it == tracked_.end()) {
            return;
        }
        tracked = std::move(it->second);
        tracked_.erase(it);
        if (const auto id = identities_.find(tracked.key); id != identities_.end()) {
            identity = id->second;
        }
    }
    DWORD code = 0;
    const bool got_code = ::GetExitCodeProcess(tracked.process.get(), &code) != 0;
    LSW_AGENT_DEBUG("launch_watcher: pid {} of launch {} exited with {}", pid, tracked.key,
                    ExitStatus::hex(code));
    if (!got_code || !ExitStatus::is_error(code)) {
        return;
    }
    {
        const std::lock_guard lock(mu_);
        const auto child = child_errors_.find(pid);
        if (child != child_errors_.end()) {
            const bool propagated = child->second == code;
            child_errors_.erase(child);
            if (propagated) {
                LSW_AGENT_DEBUG("launch_watcher: pid {} exited with the code of its child; "
                                "reported with the child",
                                pid);
                return;
            }
        }
        if (tracked.parent != 0) {
            if (child_errors_.size() >= kMaxChildErrors) {
                child_errors_.clear();
            }
            child_errors_[tracked.parent] = code;
        }
    }
    ExitedProcess exited;
    exited.key = tracked.key;
    exited.pid = pid;
    exited.parent = tracked.parent;
    exited.created = tracked.created;
    exited.exit_code = code;
    exited.image = pid == tracked.key && !identity.image.empty() ? identity.image : tracked.image;
    exited.app_id = std::move(identity.app_id);
    exited.working_dir = std::move(identity.working_dir);
    exited.path_env = std::move(identity.path_env);
    report_async(std::move(exited));
}

void LaunchWatcher::on_launch_ended(unsigned long key) {
    LSW_AGENT_DEBUG("launch_watcher: launch {} has no process left", key);
    const std::lock_guard lock(mu_);
    std::string app_id;
    if (const auto id = identities_.find(key); id != identities_.end()) {
        app_id = id->second.app_id;
    }
    if (pending_.size() >= kMaxPendingReports) {
        pending_.erase(pending_.begin());
    }
    pending_.emplace_back(linkproto::topic::kLaunchEnded,
                          nlohmann::json{{"pid", key}, {"app_id", app_id}});
}

void LaunchWatcher::report_async(ExitedProcess exited) {
    std::thread([this, exited = std::move(exited)] {
        publish(ExitReporter{}.report(exited));
    }).detach();
}

void LaunchWatcher::publish(const ErrorReport& report) {
    const std::string detail = detail_text(report);
    LSW_AGENT_WARN("app_error: {} pid {} {} ({}){}{}", file_name_of(report.image), report.pid,
                   ExitStatus::hex(report.exit_code), report.code_name,
                   detail.empty() ? "" : report.kind == "crash" ? " " : ": ", detail);
    const std::lock_guard lock(mu_);
    if (pending_.size() >= kMaxPendingReports) {
        pending_.erase(pending_.begin());
    }
    pending_.emplace_back(linkproto::topic::kAppError, to_json(report));
}

std::vector<rpc::Event> LaunchWatcher::collect_pending() {
    const std::lock_guard lock(mu_);
    batch_ = std::exchange(pending_, {});
    return batch_;
}

void LaunchWatcher::undeliverable(std::size_t first_undelivered) {
    const std::lock_guard lock(mu_);
    if (first_undelivered < batch_.size()) {
        pending_.insert(pending_.begin(),
                        std::make_move_iterator(batch_.begin() +
                                                static_cast<std::ptrdiff_t>(first_undelivered)),
                        std::make_move_iterator(batch_.end()));
    }
    batch_.clear();
}

}
