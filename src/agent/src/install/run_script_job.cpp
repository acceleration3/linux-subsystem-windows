#include "lsw/agent/install/run_script_job.hpp"

#include "lsw/agent/install/process_runner.hpp"
#include "lsw/agent/install/script_file.hpp"
#include "lsw/agent/log.hpp"

#include <utility>

namespace lsw::agent::install {

RunScriptJob::RunScriptJob(std::string id, std::string name, NotificationQueue& queue,
                           std::string script, win::RunAs as)
    : InstallJob(std::move(id), std::move(name), queue, std::move(as)),
      script_(std::move(script)) {}

void RunScriptJob::run() {
    const ScriptFile file{id_, L".cmd", script_};
    if (!file.ok()) {
        LSW_AGENT_ERROR("installer: script write failed job={} err={}", id_, file.error());
        report_done("script", false, 0, file.error());
        return;
    }
    LSW_AGENT_INFO("installer: run_script job={} name='{}' wrote {} bytes to {}, executing", id_,
                   name_, script_.size(), file.path());

    report_status("script", "running script");

    const auto outcome = ProcessRunner::run(file.command_line(), as_, kTimeoutMs);
    if (!outcome.completed) {
        report_done("script", false, outcome.exit_code, outcome.error);
        LSW_AGENT_INFO("installer: done job={} ok=false", id_);
        return;
    }
    const bool ok = outcome.exit_code == 0;
    LSW_AGENT_INFO("installer: done job={} ok={} exit={}", id_, ok, outcome.exit_code);
    report_done("script", ok, outcome.exit_code,
                ok ? std::string{} : exit_message(outcome, "script"));
}

}  // namespace lsw::agent::install
