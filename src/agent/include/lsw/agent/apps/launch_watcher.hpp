#pragma once

#include "lsw/agent/apps/exited_process.hpp"
#include "lsw/agent/apps/launch_identity.hpp"
#include "lsw/agent/rpc/event_source.hpp"
#include "lsw/agent/win/unique_handle.hpp"

#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lsw::agent::apps {
struct ErrorReport;

class LaunchWatcher : public rpc::EventSource {
public:
    [[nodiscard]] static LaunchWatcher& shared();

    [[nodiscard]] bool attach(void* job, unsigned long key, LaunchIdentity identity);

    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;
    void undeliverable(std::size_t first_undelivered) override;

private:
    struct Tracked {
        win::UniqueHandle process;
        std::wstring image;
        unsigned long parent = 0;
        std::uint64_t created = 0;
        unsigned long key = 0;
    };

    LaunchWatcher() = default;

    void start();
    void run();
    void on_new_process(unsigned long key, unsigned long pid);
    void on_exit_process(unsigned long pid);
    void on_launch_ended(unsigned long key);
    void report_launch_ended(unsigned long key);
    void report_async(ExitedProcess exited);
    void publish(const ErrorReport& report);

    std::once_flag started_;
    win::UniqueHandle port_;
    std::mutex mu_;
    std::unordered_map<unsigned long, LaunchIdentity> identities_;
    std::unordered_map<unsigned long, Tracked> tracked_;
    std::unordered_map<unsigned long, std::uint32_t> child_errors_;
    std::vector<rpc::Event> pending_;
    std::vector<rpc::Event> batch_;
};

}
