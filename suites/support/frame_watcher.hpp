#pragma once

#include <windows.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace lsw::suites {
struct Sighting {
    long long qpc = 0;
    int value = 0;
    double gap_ms = 0.0;
};

class FrameWatcher {
public:
    FrameWatcher() = default;
    FrameWatcher(const FrameWatcher&) = delete;
    FrameWatcher& operator=(const FrameWatcher&) = delete;
    ~FrameWatcher() { static_cast<void>(stop()); }

    void start(const RECT& area, int tolerance = 0);

    void set_area(const RECT& area);

    [[nodiscard]] std::vector<Sighting> stop();

    [[nodiscard]] int current() const { return current_.load(); }

    [[nodiscard]] double max_gap_ms() const { return max_gap_.load(); }

private:
    void run();

    std::thread thread_;
    std::mutex mutex_;
    RECT area_{};
    std::atomic<bool> running_{false};
    int tolerance_ = 0;
    std::atomic<int> current_{-3};
    std::atomic<double> max_gap_{0.0};
    std::vector<Sighting> sightings_;
};
}  // namespace lsw::suites
