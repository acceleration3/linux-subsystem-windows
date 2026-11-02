#include "lsw/host/services/agent_log_levels.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/log_verbosity.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/services/status_hub.hpp"

#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::services {

AgentLogLevels::AgentLogLevels(StatusHub& status, link::AgentRpcClient& rpc,
                               config::ManagerSettings& settings,
                               config::LogVerbosity& verbosity, QObject* parent)
    : QObject(parent), status_(status), rpc_(rpc), settings_(settings) {
    connect(&status_, &StatusHub::changed, this, [this](StatusHub::Area area) {
        if (area == StatusHub::Area::Vm) {
            on_status();
        }
    });
    connect(&verbosity, &config::LogVerbosity::changed, this, [this] { push(); });
}

void AgentLogLevels::on_status() {
    const bool online = status_.vm().agent_online();
    if (online && !online_) {
        push();
    }
    online_ = online;
}

void AgentLogLevels::push() {
    using Part = config::LogVerbosity::Part;
    if (!status_.vm().agent_online()) {
        return;
    }
    rpc_.request_detached(
        linkproto::cmd::kSetLogLevel,
        {{"agent", config::LogVerbosity::effective(settings_, Part::Agent).toStdString()},
         {"guest_mirrors",
          config::LogVerbosity::effective(settings_, Part::GuestMirrors).toStdString()}});
}

}
