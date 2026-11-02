#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lsw::host::services {

// A run_script job's end (the agent's setup_done event), as the reader
// thread hands it over to the thread that waits for it.
class JobEnd {
public:
    struct End {
        bool ok = false;
        int exit_code = 0;
        std::string message;
    };

    // Every setup_done is kept until the job's id is known: a script that
    // ends fast reports its end before the reply that names it.
    void on_done(const nlohmann::json& frame) {
        const std::lock_guard lock{mutex_};
        ends_.emplace_back(frame.value("job_id", std::string{}),
                           End{frame.value("ok", false), frame.value("exit_code", 0),
                               frame.value("message", std::string{})});
        changed_.notify_all();
    }

    [[nodiscard]] std::optional<End> wait(const std::string& job,
                                          std::chrono::steady_clock::duration window) {
        std::unique_lock lock{mutex_};
        std::optional<End> found;
        changed_.wait_for(lock, window, [&] {
            for (const auto& [id, end] : ends_) {
                if (id == job) {
                    found = end;
                    return true;
                }
            }
            return false;
        });
        return found;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<std::pair<std::string, End>> ends_;
};

}  // namespace lsw::host::services
