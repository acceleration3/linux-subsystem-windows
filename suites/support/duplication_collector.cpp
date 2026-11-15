#include "support/duplication_collector.hpp"

#include "support/frame_probe.hpp"
#include "support/qpc_clock.hpp"

namespace lsw::suites {

DuplicationOpen DuplicationCollector::open(const PresentPath& path, std::string& error) {
    width_ = path.width();
    height_ = path.height();
    const DuplicationOpen state = session_.open(path.client(), error);
    if (state == DuplicationOpen::Ready) {
        const RECT client = path.client();
        const RECT output = session_.output_rect();
        session_.crop_to({client.left - output.left, client.top - output.top, client.left - output.left + width_, client.top - output.top + height_});
    }
    return state;
}

void DuplicationCollector::start() {
    samples_.clear();
    pointer_only_ = 0;
    running_ = true;
    thread_ = std::thread([this] { run(); });
}

std::vector<CaptureSample> DuplicationCollector::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    return std::move(samples_);
}

void DuplicationCollector::run() {
    DuplicationFrame frame;
    std::string error;
    constexpr UINT kPollMs = 20;
    while (running_) {
        error.clear();
        const HRESULT status = session_.acquire(kPollMs, frame, error);
        if (status == DXGI_ERROR_WAIT_TIMEOUT) {
            continue;
        }
        if (FAILED(status) || !error.empty()) {
            failure_ = error.empty() ? "AcquireNextFrame returned " + std::to_string(static_cast<unsigned long>(status)) : error;
            return;
        }
        if (frame.info.LastPresentTime.QuadPart == 0) {
            ++pointer_only_;
            continue;
        }
        CaptureSample sample;
        sample.arrival = QpcClock::now();
        sample.time_ms = QpcClock::ms(frame.info.LastPresentTime.QuadPart);
        sample.accumulated = frame.info.AccumulatedFrames;
        sample.raw = FrameProbe::decode(frame.image, 0, 0, width_, height_);
        samples_.push_back(sample);
    }
}

}  // namespace lsw::suites
