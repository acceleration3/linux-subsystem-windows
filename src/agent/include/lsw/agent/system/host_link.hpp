#pragma once

#include "lsw/agent/system/host_link_plan.hpp"
#include "lsw/agent/win/unique_handle.hpp"

#include <thread>
#include <vector>

namespace lsw::agent::system {

class HostLink {
public:
    HostLink();
    ~HostLink();

    HostLink(const HostLink&) = delete;
    HostLink& operator=(const HostLink&) = delete;

    void start();
    void stop();

private:
    void run();
    void check(RepairBackoff& backoff);

    win::UniqueHandle stop_;
    win::UniqueHandle wake_;
    std::vector<HANDLE> notifications_;
    std::thread thread_;
    std::vector<HostLinkRepair> last_needed_;
    bool reported_ = false;
};

}  // namespace lsw::agent::system
