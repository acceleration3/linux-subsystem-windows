#include "sanity/capture/wgc_checks.hpp"

#include "sanity/capture/kit.hpp"
#include "sanity/present/kit.hpp"
#include "support/frame_image.hpp"
#include "support/frame_probe.hpp"
#include "support/problems.hpp"
#include "support/target_adapter.hpp"
#include "support/wgc_session.hpp"
#include "support/window_presenter.hpp"

#include <chrono>
#include <memory>

namespace lsw::suites::sanity::capture {

namespace {

constexpr int kSide = Kit::kSide;

bool create_device(D3d11Device& device, std::string& error) {
    AvAdapter adapter;
    return TargetAdapter::pick(adapter, error) && device.create_ex(adapter.luid, nullptr, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT, error);
}

std::string skip_reason(const std::string& detail) {
    return "Windows.Graphics.Capture is not creatable in this " + std::to_string(sizeof(void*) * 8) + "-bit process: " + detail;
}

}  // namespace

Outcome WgcChecks::each_api_equals_presented(Context& context) {
    D3d11Device device;
    std::string error;
    if (!create_device(device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    int exercised = 0;
    for (const std::string& api : WindowPresenter::apis()) {
        for (const bool flip : {false, true}) {
            if (flip && api != "d3d11") {
                continue;
            }
            std::unique_ptr<WindowPresenter> presenter = WindowPresenter::make(api, flip);
            const std::string name = presenter->name();
            if (!presenter->open(60, 60, kSide, kSide, error)) {
                problems.add(name + ": " + error);
                presenter->close();
                continue;
            }
            WgcSession session;
            const WgcOpen opened = session.open(presenter->hwnd(), device, error);
            if (opened == WgcOpen::Unavailable) {
                presenter->close();
                return Context::skip(skip_reason(error));
            }
            if (opened == WgcOpen::Failed) {
                problems.add(name + ": " + error);
                presenter->close();
                continue;
            }
            ++exercised;
            for (const int frame : {9, 21}) {
                if (!presenter->present(frame, error)) {
                    problems.add(name + " frame " + std::to_string(frame) + ": " + error);
                    break;
                }
                const std::vector<std::uint32_t> want = FrameImage::render(frame, kSide, kSide);
                const POINT offset = WgcSession::client_offset(presenter->hwnd());
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                WgcFrame captured;
                WgcFrame last;
                bool matched = false;
                std::string where;
                while (!matched && std::chrono::steady_clock::now() < deadline) {
                    if (!session.latest(captured, 250, error)) {
                        if (!error.empty()) {
                            problems.add(name + ": " + error);
                            break;
                        }
                        continue;
                    }
                    last = captured;
                    matched = Kit::region_matches(captured.image, offset.x, offset.y, kSide, kSide, want, where);
                }
                if (!matched && last.image.width > 0) {
                    POINT found{};
                    if (Kit::find_region(last.image, want, kSide, kSide, found)) {
                        problems.add(name + " frame " + std::to_string(frame) + ": the captured content is exact but sits at (" + std::to_string(found.x) + "," + std::to_string(found.y) +
                                     ") in the " + std::to_string(last.image.width) + "x" + std::to_string(last.image.height) + " capture, not at the client offset (" + std::to_string(offset.x) + "," +
                                     std::to_string(offset.y) + ")");
                    } else {
                        problems.add(name + " frame " + std::to_string(frame) + ": the capture differs from the presented frame as B8G8R8A8 compared as XRGB: " + where);
                        Kit::keep(context, Kit::sanitize(name) + "_wgc_" + std::to_string(frame), last.image, offset.x, offset.y, kSide, kSide, want);
                    }
                } else if (!matched) {
                    problems.add(name + " frame " + std::to_string(frame) + ": no frame was captured within 5 s");
                }
            }
            std::string close_error;
            if (!session.close(close_error)) {
                problems.add(name + ": " + close_error);
            }
            presenter->close();
        }
    }
    if (exercised == 0 && problems.empty()) {
        return Context::skip("no presenter could be opened with the APIs enabled");
    }
    context.measure("presenters_exercised", exercised);
    return problems.outcome();
}

Outcome WgcChecks::frames_in_order(Context& context) {
    D3d11Device device;
    std::string error;
    if (!create_device(device, error)) {
        return Outcome::fail(error);
    }
    std::unique_ptr<PresentPath> path = PresentPath::make("dxgi_flip_sequential");
    if (present::Kit::open(*path, kSide, kSide, error) != present::Opened::Yes) {
        return Outcome::fail(error);
    }
    WgcSession session;
    const WgcOpen opened = session.open(path->hwnd(), device, error);
    if (opened == WgcOpen::Unavailable) {
        path->close();
        return Context::skip(skip_reason(error));
    }
    if (opened == WgcOpen::Failed) {
        path->close();
        return Outcome::fail(error);
    }
    Problems problems;
    const POINT offset = WgcSession::client_offset(path->hwnd());
    std::vector<int> decoded;
    std::vector<long long> times;
    constexpr int kFrames = 48;
    const auto collect = [&](int timeout_ms) {
        WgcFrame captured;
        std::string ignored;
        while (session.next(captured, timeout_ms, ignored)) {
            decoded.push_back(FrameProbe::decode(captured.image, offset.x, offset.y, kSide, kSide));
            times.push_back(captured.system_time);
            timeout_ms = 0;
        }
    };
    for (int frame = 0; frame < kFrames; ++frame) {
        const PresentResult result = path->present(frame, 1);
        if (!result.ok) {
            problems.add("frame " + std::to_string(frame) + ": " + result.error);
            break;
        }
        collect(0);
    }
    static_cast<void>(path->drain(error));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < deadline && (decoded.empty() || decoded.back() != kFrames - 1)) {
        collect(100);
    }
    int highest = -1;
    int distinct = 0;
    int unrecognised = 0;
    for (const int value : decoded) {
        if (value < 0) {
            ++unrecognised;
            continue;
        }
        if (value < highest) {
            problems.add("captured frame number " + std::to_string(value) + " arrived after " + std::to_string(highest));
            break;
        }
        distinct += value != highest ? 1 : 0;
        highest = value;
    }
    for (std::size_t i = 1; i < times.size(); ++i) {
        if (times[i] < times[i - 1]) {
            problems.add("SystemRelativeTime went backwards between captured frames " + std::to_string(i - 1) + " and " + std::to_string(i));
            break;
        }
    }
    context.measure("captured_frames", static_cast<long long>(decoded.size()));
    context.measure("distinct_frame_numbers", distinct);
    context.measure("unrecognised_frames", unrecognised);
    if (highest != kFrames - 1) {
        problems.add("the newest captured frame number is " + std::to_string(highest) + ", the last presented frame is " + std::to_string(kFrames - 1));
    }
    if (distinct < 3) {
        problems.add("only " + std::to_string(distinct) + " distinct frame numbers were captured while " + std::to_string(kFrames) + " frames were presented");
    }
    std::string close_error;
    if (!session.close(close_error)) {
        problems.add(close_error);
    }
    path->close();
    return problems.outcome();
}

Outcome WgcChecks::resize_and_close(Context& context) {
    D3d11Device device;
    std::string error;
    if (!create_device(device, error)) {
        return Outcome::fail(error);
    }
    std::unique_ptr<PresentPath> path = PresentPath::make("dxgi_flip_sequential");
    if (present::Kit::open(*path, kSide, kSide, error) != present::Opened::Yes) {
        return Outcome::fail(error);
    }
    WgcSession session;
    const WgcOpen opened = session.open(path->hwnd(), device, error);
    if (opened == WgcOpen::Unavailable) {
        path->close();
        return Context::skip(skip_reason(error));
    }
    if (opened == WgcOpen::Failed) {
        path->close();
        return Outcome::fail(error);
    }
    Problems problems;
    const auto capture_until = [&](int frame, int width, int height, int content_w, int content_h, WgcFrame& out) {
        const std::vector<std::uint32_t> want = FrameImage::render(frame, width, height);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        WgcFrame captured;
        std::string where;
        while (std::chrono::steady_clock::now() < deadline) {
            std::string ignored;
            if (!session.latest(captured, 250, ignored)) {
                continue;
            }
            out = captured;
            const POINT offset = WgcSession::client_offset(path->hwnd());
            if ((content_w == 0 || captured.content_width == content_w) && (content_h == 0 || captured.content_height == content_h) &&
                Kit::region_matches(captured.image, offset.x, offset.y, width, height, want, where)) {
                return true;
            }
        }
        return false;
    };
    WgcFrame frame;
    if (!path->present(3, 1).ok || !capture_until(3, kSide, kSide, 0, 0, frame)) {
        problems.add("the initial frame was never captured exactly");
    }
    const RECT before = WgcSession::frame_bounds(path->hwnd());
    context.measure("initial_content_size", std::to_string(frame.content_width) + "x" + std::to_string(frame.content_height));
    constexpr int kNewW = 176;
    constexpr int kNewH = 140;
    if (problems.empty()) {
        if (!path->resize(kNewW, kNewH, error)) {
            problems.add("resizing the window: " + error);
        } else {
            static_cast<void>(path->present(4, 1));
            const RECT after = WgcSession::frame_bounds(path->hwnd());
            const int bw = static_cast<int>(after.right - after.left);
            const int bh = static_cast<int>(after.bottom - after.top);
            const int old_w = static_cast<int>(before.right - before.left);
            if (bw == old_w) {
                problems.add("the window frame did not change size on the resize");
            }
            bool resized = false;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!resized && std::chrono::steady_clock::now() < deadline) {
                std::string ignored;
                if (session.latest(frame, 250, ignored) && frame.content_width == bw && frame.content_height == bh) {
                    resized = true;
                }
            }
            if (!resized) {
                problems.add("no frame arrived with the new ContentSize " + std::to_string(bw) + "x" + std::to_string(bh) + " (last " + std::to_string(frame.content_width) + "x" +
                             std::to_string(frame.content_height) + ")");
            } else if (!session.recreate(bw, bh, error)) {
                problems.add(error);
            } else {
                static_cast<void>(path->present(5, 1));
                if (!capture_until(5, kNewW, kNewH, bw, bh, frame)) {
                    problems.add("after Recreate to " + std::to_string(bw) + "x" + std::to_string(bh) + " no frame carried the exact content of the presented frame at " + std::to_string(kNewW) + "x" +
                                 std::to_string(kNewH) + " (last content size " + std::to_string(frame.content_width) + "x" + std::to_string(frame.content_height) + ")");
                }
            }
        }
    }
    path->close();
    if (!session.wait_closed(5000)) {
        problems.add("the capture item's Closed event did not fire within 5 s of destroying the window");
    }
    std::string close_error;
    if (!session.close(close_error)) {
        problems.add(close_error);
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::capture
