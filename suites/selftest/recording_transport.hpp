#pragma once

#include "core/status_transport.hpp"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace lsw::suites::selftest {
class RecordingTransport final : public StatusTransport {
public:
    struct Log {
        std::mutex mutex;
        std::condition_variable arrived;
        std::vector<Json> batches;
        std::vector<int> answers;
        int block_ms = 0;

        [[nodiscard]] bool wait_for(std::size_t count, int milliseconds) {
            std::unique_lock<std::mutex> lock(mutex);
            return arrived.wait_for(lock, std::chrono::milliseconds(milliseconds), [&] { return batches.size() >= count; });
        }

        [[nodiscard]] std::vector<Json> all() {
            const std::lock_guard<std::mutex> lock(mutex);
            return batches;
        }
    };

    explicit RecordingTransport(std::shared_ptr<Log> log) : log_(std::move(log)) {}

    [[nodiscard]] int post(const Json& batch) override {
        int answer = 200;
        int block_ms = 0;
        {
            const std::lock_guard<std::mutex> lock(log_->mutex);
            if (log_->batches.size() < log_->answers.size()) {
                answer = log_->answers[log_->batches.size()];
            }
            log_->batches.push_back(batch);
            block_ms = log_->block_ms;
        }
        log_->arrived.notify_all();
        if (block_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(block_ms));
        }
        return answer;
    }

private:
    std::shared_ptr<Log> log_;
};
}  // namespace lsw::suites::selftest
