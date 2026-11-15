#pragma once

#include "core/check_result.hpp"
#include "core/json.hpp"
#include "core/status_transport.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace lsw::suites {
class StatusReporter {
public:
    struct Identity {
        std::string run;
        std::string part;
        std::string invocation;
        std::string label;
        std::string exe;
        int bitness = 0;
        std::vector<std::string> suites;
        Json adapter = Json::object();
        std::string started;
        std::string phase;
        int jobs = 0;
    };

    static constexpr int kFlushMs = 2000;
    static constexpr std::size_t kQueueLimit = 4096;

    StatusReporter() = default;
    StatusReporter(const StatusReporter&) = delete;
    StatusReporter& operator=(const StatusReporter&) = delete;
    ~StatusReporter();

    void open(std::unique_ptr<StatusTransport> transport, Identity identity,
              std::vector<std::string> planned);

    void check_started(const std::string& id, std::uint32_t pid);

    void check_finished(const CheckResult& result);

    void close(bool complete);

private:
    class Channel;

    std::shared_ptr<Channel> channel_;
    std::thread thread_;
};
}  // namespace lsw::suites
