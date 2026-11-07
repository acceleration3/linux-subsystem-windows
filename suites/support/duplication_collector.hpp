#pragma once

#include "support/capture_sample.hpp"
#include "support/duplication_session.hpp"
#include "support/present_path.hpp"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace lsw::suites {
class DuplicationCollector {
public:
    DuplicationCollector() = default;
    DuplicationCollector(const DuplicationCollector&) = delete;
    DuplicationCollector& operator=(const DuplicationCollector&) = delete;
    ~DuplicationCollector() { static_cast<void>(stop()); }

    [[nodiscard]] DuplicationOpen open(const PresentPath& path, std::string& error);

    void start();

    [[nodiscard]] std::vector<CaptureSample> stop();

    [[nodiscard]] const std::string& failure() const { return failure_; }

    [[nodiscard]] int pointer_only() const { return pointer_only_; }

private:
    void run();

    DuplicationSession session_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    int width_ = 0;
    int height_ = 0;
    int pointer_only_ = 0;
    std::vector<CaptureSample> samples_;
    std::string failure_;
};
}  // namespace lsw::suites
