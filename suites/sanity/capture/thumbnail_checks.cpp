#include "sanity/capture/thumbnail_checks.hpp"

#include "sanity/capture/kit.hpp"
#include "sanity/present/kit.hpp"
#include "support/frame_image.hpp"
#include "support/frame_probe.hpp"
#include "support/frame_watcher.hpp"
#include "support/problems.hpp"
#include "support/present_path.hpp"
#include "support/window_presenter.hpp"

#include <dwmapi.h>

#include <chrono>
#include <cstdlib>
#include <memory>

namespace lsw::suites::sanity::capture {

namespace {

constexpr int kThumbSize = 64;
constexpr int kMargin = 10;
constexpr int kTolerance = 2;

struct Destination {
    TestWindow window;
    HTHUMBNAIL thumbnail = nullptr;

    [[nodiscard]] bool open(HWND source, std::string& error) {
        TestWindowConfig config;
        config.x = 340;
        config.y = 60;
        TestWindow::outer_size(config.x, config.y, 200, 150, config.width, config.height);
        if (!window.create(config, error)) {
            return false;
        }
        window.paint_with(RGB(30, 30, 34));
        ::SetWindowPos(window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ::InvalidateRect(window.hwnd(), nullptr, TRUE);
        const HRESULT registered = ::DwmRegisterThumbnail(window.hwnd(), source, &thumbnail);
        if (FAILED(registered)) {
            error = "DwmRegisterThumbnail returned 0x" + std::to_string(static_cast<unsigned long>(registered));
            return false;
        }
        DWM_THUMBNAIL_PROPERTIES properties{};
        properties.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
        properties.rcDestination = {kMargin, kMargin, kMargin + kThumbSize, kMargin + kThumbSize};
        properties.opacity = 255;
        properties.fVisible = TRUE;
        properties.fSourceClientAreaOnly = TRUE;
        const HRESULT updated = ::DwmUpdateThumbnailProperties(thumbnail, &properties);
        if (FAILED(updated)) {
            error = "DwmUpdateThumbnailProperties returned 0x" + std::to_string(static_cast<unsigned long>(updated));
            return false;
        }
        return true;
    }

    [[nodiscard]] RECT area() const {
        RECT client = ScreenCapture::client_on_screen(window.hwnd());
        return {client.left + kMargin, client.top + kMargin, client.left + kMargin + kThumbSize, client.top + kMargin + kThumbSize};
    }

    void close() {
        if (thumbnail != nullptr) {
            ::DwmUnregisterThumbnail(thumbnail);
            thumbnail = nullptr;
        }
        window.destroy();
    }
};

bool thumbnail_matches(const Bitmap& image, int frame, std::string& where) {
    const std::vector<std::uint32_t> want = FrameImage::render(frame, kThumbSize, kThumbSize);
    std::size_t bad = 0;
    for (int y = 0; y < kThumbSize; ++y) {
        for (int x = 0; x < kThumbSize; ++x) {
            const bool near_split = std::abs(x - kThumbSize / 2) <= 2 || std::abs(y - kThumbSize / 2) <= 2 || x < 1 || y < 1 || x >= kThumbSize - 1 || y >= kThumbSize - 1;
            if (near_split || !image.valid(x, y)) {
                continue;
            }
            const std::uint32_t got = image.at(x, y);
            const std::uint32_t exp = want[static_cast<std::size_t>(y) * kThumbSize + static_cast<std::size_t>(x)] & 0xFFFFFFu;
            for (int shift = 0; shift < 24; shift += 8) {
                if (std::abs(static_cast<int>((got >> shift) & 255u) - static_cast<int>((exp >> shift) & 255u)) > kTolerance) {
                    if (bad == 0) {
                        where = "pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is " + ScreenCapture::rgb_text(got) + ", expected " + ScreenCapture::rgb_text(exp);
                    }
                    ++bad;
                    break;
                }
            }
        }
    }
    if (bad != 0) {
        where = std::to_string(bad) + " thumbnail pixels differ from the 2x downscale of the presented frame beyond " + std::to_string(kTolerance) + ", first " + where;
    }
    return bad == 0;
}

bool await_thumbnail(const Destination& destination, int frame, std::string& where, Bitmap& last) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
        std::string error;
        if (ScreenCapture::screen(destination.area(), false, last, error) && thumbnail_matches(last, frame, where)) {
            return true;
        }
        ::Sleep(20);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

}  // namespace

Outcome ThumbnailChecks::each_api(Context& context) {
    BOOL composition = FALSE;
    if (FAILED(::DwmIsCompositionEnabled(&composition)) || composition == FALSE) {
        return Outcome::fail("DWM composition is not enabled, so thumbnails cannot be composed");
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
            std::string error;
            if (!presenter->open(60, 60, Kit::kSide, Kit::kSide, error)) {
                problems.add(name + ": " + error);
                presenter->close();
                continue;
            }
            Destination destination;
            if (!destination.open(presenter->hwnd(), error)) {
                problems.add(name + ": " + error);
                destination.close();
                presenter->close();
                continue;
            }
            ++exercised;
            for (const int frame : {9, 20}) {
                if (!presenter->present(frame, error)) {
                    problems.add(name + " frame " + std::to_string(frame) + ": " + error);
                    break;
                }
                std::string where;
                Bitmap last;
                if (!await_thumbnail(destination, frame, where, last)) {
                    problems.add(name + " frame " + std::to_string(frame) + ": " + where);
                    if (last.width > 0) {
                        Kit::keep(context, Kit::sanitize(name) + "_thumb_" + std::to_string(frame), last, 0, 0, kThumbSize, kThumbSize, FrameImage::render(frame, kThumbSize, kThumbSize));
                    }
                }
            }
            destination.close();
            presenter->close();
        }
    }
    if (exercised == 0 && problems.empty()) {
        return Context::skip("no presenter could be opened with the APIs enabled");
    }
    context.measure("presenters_exercised", exercised);
    return problems.outcome();
}

Outcome ThumbnailChecks::updates(Context& context) {
    BOOL composition = FALSE;
    if (FAILED(::DwmIsCompositionEnabled(&composition)) || composition == FALSE) {
        return Outcome::fail("DWM composition is not enabled, so thumbnails cannot be composed");
    }
    std::unique_ptr<PresentPath> path = PresentPath::make("dxgi_flip_sequential");
    std::string error;
    if (present::Kit::open(*path, Kit::kSide, Kit::kSide, error) != present::Opened::Yes) {
        return Outcome::fail(error);
    }
    Destination destination;
    if (!destination.open(path->hwnd(), error)) {
        destination.close();
        path->close();
        return Outcome::fail(error);
    }
    Problems problems;
    FrameWatcher watcher;
    watcher.start(destination.area(), kTolerance);
    ::Sleep(100);
    constexpr int kFrames = 40;
    for (int frame = 0; frame < kFrames; ++frame) {
        const PresentResult result = path->present(frame, 1);
        if (!result.ok) {
            problems.add("frame " + std::to_string(frame) + ": " + result.error);
            break;
        }
    }
    static_cast<void>(path->drain(error));
    const bool final_shown = present::Kit::await_frame_in(destination.area(), kFrames - 1, 3000);
    if (!final_shown) {
        problems.add("the thumbnail never showed the last presented frame " + std::to_string(kFrames - 1));
    }
    const long long minimized_at = QpcClock::now();
    ::ShowWindow(path->hwnd(), SW_MINIMIZE);
    ::Sleep(500);
    for (int frame = 40; frame < 50; ++frame) {
        static_cast<void>(path->present(frame, 1));
    }
    static_cast<void>(path->drain(error));
    ::Sleep(700);
    const std::vector<Sighting> sightings = watcher.stop();
    const SequenceReport report = SequenceJudge::judge(sightings, RefreshRate::period_ms() * 0.75);
    if (report.regressions != 0 || report.torn != 0) {
        problems.add("the thumbnail showed frames out of order (" + SequenceJudge::summary(report) + ")");
    }
    if (report.seen.size() < 3) {
        problems.add("the thumbnail of a window presenting " + std::to_string(kFrames) + " frames showed only " + std::to_string(report.seen.size()) + " distinct frames");
    }
    for (const Sighting& sighting : sightings) {
        if (sighting.qpc > minimized_at && sighting.value >= 40) {
            problems.add("the thumbnail kept updating after its source was minimised and showed frame " + std::to_string(sighting.value));
            break;
        }
    }
    ::ShowWindow(path->hwnd(), SW_RESTORE);
    ::Sleep(500);
    static_cast<void>(path->present(55, 1));
    static_cast<void>(path->drain(error));
    if (!present::Kit::await_frame_in(destination.area(), 55, 4000)) {
        problems.add("the thumbnail did not resume after the source window was restored");
    }
    path->close();
    bool vanished = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!vanished && std::chrono::steady_clock::now() < deadline) {
        vanished = FrameProbe::sample(destination.area(), kTolerance) != 55;
        if (!vanished) {
            ::Sleep(40);
        }
    }
    context.measure("thumbnail_removed_after_destroy", vanished);
    if (!vanished) {
        problems.add("the thumbnail still shows the last frame of a source window that was destroyed");
    }
    DWM_THUMBNAIL_PROPERTIES properties{};
    properties.dwFlags = DWM_TNP_OPACITY;
    properties.opacity = 255;
    const HRESULT after = ::DwmUpdateThumbnailProperties(destination.thumbnail, &properties);
    context.measure("update_after_destroy_hr", Kit::hr(after));
    destination.close();
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::capture
