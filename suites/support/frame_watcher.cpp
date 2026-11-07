#include "support/frame_watcher.hpp"

#include "support/frame_probe.hpp"
#include "support/qpc_clock.hpp"

namespace lsw::suites {

void FrameWatcher::start(const RECT& area, int tolerance) {
    static_cast<void>(stop());
    tolerance_ = tolerance;
    area_ = area;
    sightings_.clear();
    current_ = -3;
    max_gap_ = 0.0;
    running_ = true;
    thread_ = std::thread([this] { run(); });
}

void FrameWatcher::set_area(const RECT& area) {
    std::lock_guard lock(mutex_);
    area_ = area;
}

std::vector<Sighting> FrameWatcher::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    return std::move(sightings_);
}

void FrameWatcher::run() {
    long long previous = QpcClock::now();
    int last = -3;
    while (running_) {
        RECT area;
        {
            std::lock_guard lock(mutex_);
            area = area_;
        }
        const int value = FrameProbe::sample(area, tolerance_);
        const long long now = QpcClock::now();
        const double gap = QpcClock::ms(now - previous);
        previous = now;
        if (gap > max_gap_.load()) {
            max_gap_ = gap;
        }
        if (value != last) {
            sightings_.push_back({now, value, gap});
            last = value;
            current_ = value;
        }
        ::Sleep(1);
    }
}

}  // namespace lsw::suites
