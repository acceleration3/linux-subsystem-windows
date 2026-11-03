#pragma once

#include "lsw/agent/apps/error_report.hpp"
#include "lsw/agent/apps/exited_process.hpp"
#include "lsw/agent/apps/import_resolver.hpp"

namespace lsw::agent::apps {
class ExitReporter {
public:
    [[nodiscard]] ErrorReport report(const ExitedProcess& exited) const;

private:
    void explain_load_failure(const ExitedProcess& exited, ErrorReport& report) const;
    void explain_side_by_side(const ExitedProcess& exited, ErrorReport& report) const;
    void explain_crash(const ExitedProcess& exited, ErrorReport& report) const;
};

}
