#pragma once

#include "lsw/agent/install/install_job.hpp"

namespace lsw::agent::install {

// Run an inline batch script in the guest.
//
// Bounded by a hard timeout so a wedged child can never stall the whole
// setup phase. Legitimate setup scripts here finish in seconds to
// minutes; the ceiling only fires on a true hang.
class RunScriptJob final : public InstallJob {
public:
    static constexpr unsigned long kTimeoutMs = 30UL * 60UL * 1000UL;

    RunScriptJob(std::string id, std::string name, NotificationQueue& queue,
                 std::string script);

    void run() override;

private:
    std::string script_;
};

}  // namespace lsw::agent::install
