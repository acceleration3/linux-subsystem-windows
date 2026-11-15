#pragma once

#include "support/capture_sample.hpp"
#include "support/d3d11_device.hpp"
#include "support/present_path.hpp"
#include "support/wgc_session.hpp"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace lsw::suites {
class WgcCollector {
public:
    WgcCollector() = default;
    WgcCollector(const WgcCollector&) = delete;
    WgcCollector& operator=(const WgcCollector&) = delete;
    ~WgcCollector();

    [[nodiscard]] WgcOpen open(const PresentPath& path, std::string& error);

    void start();

    [[nodiscard]] std::vector<CaptureSample> stop();

    [[nodiscard]] bool close(std::string& error);

    [[nodiscard]] const std::string& failure() const { return failure_; }

private:
    void run();

    D3d11Device device_;
    WgcSession session_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    HWND window_ = nullptr;
    POINT offset_{};
    int width_ = 0;
    int height_ = 0;
    bool opened_ = false;
    std::vector<CaptureSample> samples_;
    std::string failure_;
};
}  // namespace lsw::suites
