#include "support/wgc_collector.hpp"

#include "support/frame_probe.hpp"
#include "support/qpc_clock.hpp"
#include "support/target_adapter.hpp"

namespace lsw::suites {

namespace {

constexpr int kPollMs = 20;
constexpr double kTicksPerMs = 10000.0;

}  // namespace

WgcCollector::~WgcCollector() {
    static_cast<void>(stop());
    std::string ignored;
    static_cast<void>(close(ignored));
}

WgcOpen WgcCollector::open(const PresentPath& path, std::string& error) {
    AvAdapter adapter;
    if (!TargetAdapter::pick(adapter, error) || !device_.create_ex(adapter.luid, nullptr, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT, error)) {
        return WgcOpen::Failed;
    }
    window_ = path.hwnd();
    width_ = path.width();
    height_ = path.height();
    const WgcOpen state = session_.open(window_, device_, error);
    if (state == WgcOpen::Ready) {
        offset_ = WgcSession::client_offset(window_);
        opened_ = true;
    }
    return state;
}

void WgcCollector::start() {
    samples_.clear();
    running_ = true;
    thread_ = std::thread([this] { run(); });
}

std::vector<CaptureSample> WgcCollector::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    return std::move(samples_);
}

bool WgcCollector::close(std::string& error) {
    if (!opened_) {
        return true;
    }
    opened_ = false;
    return session_.close(error);
}

void WgcCollector::run() {
    WgcFrame frame;
    std::string error;
    while (running_) {
        if (!session_.next(frame, kPollMs, error)) {
            if (!error.empty()) {
                failure_ = error;
                return;
            }
            continue;
        }
        CaptureSample sample;
        sample.arrival = QpcClock::now();
        sample.time_ms = static_cast<double>(frame.system_time) / kTicksPerMs;
        sample.raw = FrameProbe::decode(frame.image, offset_.x, offset_.y, width_, height_);
        samples_.push_back(sample);
    }
}

}  // namespace lsw::suites
