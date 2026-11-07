#include "support/robust_child.hpp"

#include "core/child_mode.hpp"
#include "core/text.hpp"
#include "support/child_runs.hpp"
#include "support/fresh_process.hpp"
#include "support/gate_window.hpp"

#include <windows.h>

namespace lsw::suites {

std::string RobustChild::role() {
    return ChildMode::variable(L"LSW_SUITES_ROLE");
}

Outcome RobustChild::child(Context& context, const Role& specific) {
    const std::string name = role();
    if (name == "gate-peer") {
        return GateWindow::peer(context);
    }
    if (name == "fresh-probe") {
        return FreshProcess::child(context);
    }
    return specific(context, name);
}

void RobustChild::ready() {
    const std::string name = ChildMode::variable(L"LSW_SUITES_CHILD_READY");
    if (name.empty()) {
        return;
    }
    HANDLE event = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, Text::wide(name).c_str());
    if (event != nullptr) {
        ::SetEvent(event);
        ::CloseHandle(event);
    }
}

void RobustChild::forward(Context& context, const std::string& prefix, const CheckResult& result) {
    const std::vector<std::string>& keys = result.measures.keys();
    const std::vector<Json>& values = result.measures.values();
    for (std::size_t i = 0; i < keys.size() && i < values.size(); ++i) {
        context.measure(prefix + keys[i], values[i]);
    }
}

CheckResult RobustChild::run(const std::string& id, const std::string& role_name,
                             const std::vector<std::pair<std::string, std::string>>& env, int timeout_seconds,
                             int kill_after_ready_ms) {
    static volatile LONG counter = 0;
    const std::string name = "Local\\lsw-suites-child-ready-" + std::to_string(::GetCurrentProcessId()) + "-" +
                             std::to_string(::InterlockedIncrement(&counter));
    HANDLE ready = ::CreateEventW(nullptr, TRUE, FALSE, Text::wide(name).c_str());
    ChildRunOptions options;
    options.timeout_seconds = timeout_seconds;
    options.env = env;
    options.env.emplace_back("LSW_SUITES_ROLE", role_name);
    options.env.emplace_back("LSW_SUITES_CHILD_READY", name);
    if (kill_after_ready_ms >= 0) {
        options.kill_trigger = ready;
        options.kill_after_trigger_ms = kill_after_ready_ms;
    }
    const CheckResult result = ChildRuns::run_self(id, options);
    if (ready != nullptr) {
        ::CloseHandle(ready);
    }
    return result;
}

}  // namespace lsw::suites
