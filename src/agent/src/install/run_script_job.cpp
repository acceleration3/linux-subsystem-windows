#include "lsw/agent/install/run_script_job.hpp"

#include "lsw/agent/install/process_runner.hpp"
#include "lsw/agent/install/script_file.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"

#include <utility>

namespace lsw::agent::install {

RunScriptJob::RunScriptJob(std::string id, std::string name, NotificationQueue& queue,
                           std::string script)
    : InstallJob(std::move(id), std::move(name), queue), script_(std::move(script)) {}

void RunScriptJob::run() {
    const ScriptFile file{id_, L".cmd", script_};
    if (!file.ok()) {
        LSW_AGENT_ERROR(std::string{"installer: script write failed job="} + id_
                        + " err=" + file.error());
        report_done("script", false, 0, file.error());
        return;
    }
    LSW_AGENT_INFO(std::string{"installer: run_script job="} + id_ + " name='" + name_
                   + "' wrote " + std::to_string(script_.size()) + " bytes to "
                   + util::TextEncoding::to_utf8(file.path()) + " — executing");

    report_status("script", "running script");

    const auto outcome = ProcessRunner::run(file.command_line(), kTimeoutMs);
    if (!outcome.completed) {
        report_done("script", false, outcome.exit_code, outcome.error);
        LSW_AGENT_INFO(std::string{"installer: done job="} + id_ + " ok=false");
        return;
    }
    const bool ok = outcome.exit_code == 0;
    LSW_AGENT_INFO(std::string{"installer: done job="} + id_
                   + " ok=" + (ok ? "true" : "false")
                   + " exit=" + std::to_string(outcome.exit_code));
    report_done("script", ok, outcome.exit_code,
                ok ? std::string{}
                   : "script exit code " + std::to_string(outcome.exit_code));
}

}  // namespace lsw::agent::install
