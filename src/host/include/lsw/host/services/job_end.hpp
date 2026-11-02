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
class JobEnd {
public:
    struct End {
        bool ok = false;
        int exit_code = 0;
        std::string message;
    };

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

}
