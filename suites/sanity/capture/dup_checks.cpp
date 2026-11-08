#include "sanity/capture/dup_checks.hpp"

#include "sanity/capture/kit.hpp"
#include "sanity/present/kit.hpp"
#include "support/duplication_session.hpp"
#include "support/problems.hpp"

#include <algorithm>
#include <chrono>
#include <memory>

namespace lsw::suites::sanity::capture {

namespace {

constexpr int kWidth = 160;
constexpr int kHeight = 120;

bool finding_or_timeout(HRESULT status) {
    return status == DXGI_ERROR_WAIT_TIMEOUT;
}

void covered_changes(const DuplicationFrame& frame, const Bitmap& previous, POINT& first, long long& changed, long long& uncovered) {
    const int w = frame.image.width;
    const int h = frame.image.height;
    std::vector<char> mask(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    const auto mark = [&](const RECT& r) {
        for (int y = std::max<int>(0, r.top); y < std::min<int>(h, r.bottom); ++y) {
            for (int x = std::max<int>(0, r.left); x < std::min<int>(w, r.right); ++x) {
                mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = 1;
            }
        }
    };
    for (const RECT& r : frame.dirty) {
        mark(r);
    }
    for (const DXGI_OUTDUPL_MOVE_RECT& move : frame.moves) {
        mark(move.DestinationRect);
    }
    changed = 0;
    uncovered = 0;
    first = {-1, -1};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            if ((frame.image.pixels[i] & 0xFFFFFFu) != (previous.pixels[i] & 0xFFFFFFu)) {
                ++changed;
                if (mask[i] == 0) {
                    if (uncovered == 0) {
                        first = {x, y};
                    }
                    ++uncovered;
                }
            }
        }
    }
}

}  // namespace

Outcome DupChecks::desktop_duplication_content(Context& context) {
    std::unique_ptr<PresentPath> path = PresentPath::make("dxgi_flip_sequential");
    std::string error;
    if (present::Kit::open(*path, kWidth, kHeight, error) != present::Opened::Yes) {
        return Outcome::fail(error);
    }
    DuplicationSession dup;
    switch (dup.open(path->client(), error)) {
        case DuplicationOpen::Unavailable:
            path->close();
            return Context::skip("output duplication is not available: " + error);
        case DuplicationOpen::Failed:
            path->close();
            return Outcome::fail(error);
        case DuplicationOpen::Ready:
            break;
    }
    context.measure("duplicate_output1", dup.used_duplicate_output1());
    Problems problems;
    DuplicationFrame frame;
    for (int i = 0; i < 10; ++i) {
        if (dup.acquire(200, frame, error) == DXGI_ERROR_WAIT_TIMEOUT) {
            break;
        }
    }
    const RECT output = dup.output_rect();
    int acquired = 0;
    int nonzero = 0;
    long long last_present_time = 0;
    long long newest_time = 0;
    int last_decoded = -1;
    const double period = RefreshRate::period_ms();
    for (const int target : {5, 6, 7, 8, 30}) {
        const long long called = QpcClock::now();
        const PresentResult result = path->present(target, 1);
        if (!result.ok) {
            problems.add("frame " + std::to_string(target) + ": " + result.error);
            break;
        }
        static_cast<void>(path->drain(error));
        const RECT client = path->client();
        const int ox = static_cast<int>(client.left - output.left);
        const int oy = static_cast<int>(client.top - output.top);
        const std::vector<std::uint32_t> want = FrameImage::render(target, kWidth, kHeight);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        bool matched = false;
        std::string where;
        int decoded = FrameProbe::kNone;
        while (!matched && std::chrono::steady_clock::now() < deadline) {
            error.clear();
            const HRESULT status = dup.acquire(300, frame, error);
            if (finding_or_timeout(status)) {
                continue;
            }
            if (FAILED(status)) {
                problems.add("AcquireNextFrame returned " + Kit::hr(status) + (error.empty() ? "" : " (" + error + ")"));
                break;
            }
            if (!error.empty()) {
                problems.add(error);
                break;
            }
            ++acquired;
            nonzero += frame.all_zero ? 0 : 1;
            const long long present_time = frame.info.LastPresentTime.QuadPart;
            if (present_time != 0) {
                if (frame.info.AccumulatedFrames == 0) {
                    problems.add("a frame with LastPresentTime " + std::to_string(present_time) + " reports AccumulatedFrames 0");
                }
                if (present_time < newest_time) {
                    problems.add("LastPresentTime went backwards from " + std::to_string(newest_time) + " to " + std::to_string(present_time));
                }
                if (present_time > QpcClock::now()) {
                    problems.add("LastPresentTime " + std::to_string(present_time) + " is in the future");
                }
                newest_time = std::max(newest_time, present_time);
                last_present_time = present_time;
            }
            decoded = FrameProbe::decode(frame.image, ox, oy, kWidth, kHeight);
            if (decoded >= 0) {
                if (decoded < last_decoded) {
                    problems.add("frame " + std::to_string(decoded) + " was duplicated after frame " + std::to_string(last_decoded));
                }
                last_decoded = std::max(last_decoded, decoded);
            }
            if (decoded == target) {
                matched = true;
                if (!Kit::region_matches(frame.image, ox, oy, kWidth, kHeight, want, where)) {
                    problems.add("the duplicated frame " + std::to_string(target) + " is not bit-exact in B8G8R8A8 compared as XRGB: " + where);
                    Kit::keep(context, "dup_frame_" + std::to_string(target), frame.image, ox, oy, kWidth, kHeight, want);
                }
                if (present_time != 0 && QpcClock::ms(called - present_time) > period + 1.0) {
                    problems.add("the frame showing present " + std::to_string(target) + " has LastPresentTime " + std::to_string(QpcClock::ms(called - present_time)) + " ms before Present was called");
                }
            }
        }
        if (!problems.empty() && !matched) {
            break;
        }
        if (!matched) {
            if (acquired > 0 && nonzero == 0) {
                problems.add(DuplicationSession::zero_frame_finding(acquired, last_present_time));
            } else {
                problems.add("no duplicated frame showed presented frame " + std::to_string(target) + " within 6 s; the last region decodes as " + FrameProbe::name(decoded) + " " + std::to_string(decoded) +
                             " (" + std::to_string(acquired) + " frames acquired, " + std::to_string(nonzero) + " non-zero)");
            }
            break;
        }
    }
    context.measure("acquired_frames", acquired);
    context.measure("nonzero_frames", nonzero);
    context.measure("last_present_time", last_present_time);
    if (problems.empty()) {
        int timeouts = 0;
        int busy = 0;
        for (int attempt = 0; attempt < 12 && timeouts == 0; ++attempt) {
            const HRESULT status = dup.acquire(500, frame, error);
            if (status == DXGI_ERROR_WAIT_TIMEOUT) {
                ++timeouts;
            } else if (SUCCEEDED(status)) {
                ++busy;
            } else {
                problems.add("AcquireNextFrame while idle returned " + Kit::hr(status));
                break;
            }
        }
        context.measure("frames_before_idle_timeout", busy);
        if (problems.empty() && timeouts == 0) {
            problems.add("AcquireNextFrame never returned DXGI_ERROR_WAIT_TIMEOUT in 6 s although the desktop was not changed (" + std::to_string(busy) + " frames kept arriving)");
        }
    }
    path->close();
    return problems.outcome();
}

Outcome DupChecks::dirty_rects_cover_changes(Context& context) {
    TestWindow window;
    TestWindowConfig config;
    config.x = 60;
    config.y = 60;
    constexpr int kClientW = 220;
    constexpr int kClientH = 160;
    TestWindow::outer_size(config.x, config.y, kClientW, kClientH, config.width, config.height);
    std::string error;
    if (!window.create(config, error)) {
        return Outcome::fail(error);
    }
    ::SetWindowPos(window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    const auto paint = [&](const RECT& r, COLORREF color) {
        HDC dc = ::GetDC(window.hwnd());
        HBRUSH brush = ::CreateSolidBrush(color);
        ::FillRect(dc, &r, brush);
        ::DeleteObject(brush);
        ::GdiFlush();
        ::ReleaseDC(window.hwnd(), dc);
    };
    paint({0, 0, kClientW, kClientH}, RGB(40, 44, 48));
    ::Sleep(300);
    DuplicationSession dup;
    switch (dup.open(ScreenCapture::client_on_screen(window.hwnd()), error)) {
        case DuplicationOpen::Unavailable:
            return Context::skip("output duplication is not available: " + error);
        case DuplicationOpen::Failed:
            return Outcome::fail(error);
        case DuplicationOpen::Ready:
            break;
    }
    Problems problems;
    DuplicationFrame frame;
    Bitmap previous;
    int baseline_frames = 0;
    const auto baseline_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (previous.pixels.empty() && std::chrono::steady_clock::now() < baseline_deadline) {
        const HRESULT status = dup.acquire(300, frame, error);
        if (SUCCEEDED(status) && !frame.image.pixels.empty()) {
            ++baseline_frames;
            if (frame.all_zero) {
                return Outcome::fail(DuplicationSession::zero_frame_finding(baseline_frames, frame.info.LastPresentTime.QuadPart));
            }
            previous = frame.image;
        } else if (FAILED(status) && status != DXGI_ERROR_WAIT_TIMEOUT) {
            return Outcome::fail("AcquireNextFrame returned " + Kit::hr(status));
        }
    }
    if (previous.pixels.empty()) {
        return Outcome::fail("no baseline desktop frame was delivered within 5 s");
    }
    long long frames_checked = 0;
    long long changes_seen = 0;
    long long rects_seen = 0;
    const auto settle_and_check = [&](const std::string& what, const RECT& expect_changed, std::uint32_t expect_colour, bool moved) {
        const RECT client = ScreenCapture::client_on_screen(window.hwnd());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool seen = false;
        while (!seen && std::chrono::steady_clock::now() < deadline) {
            error.clear();
            const HRESULT status = dup.acquire(400, frame, error);
            if (status == DXGI_ERROR_WAIT_TIMEOUT) {
                continue;
            }
            if (FAILED(status) || !error.empty()) {
                problems.add(what + ": AcquireNextFrame returned " + Kit::hr(status) + " " + error);
                return;
            }
            if (frame.all_zero) {
                problems.add(DuplicationSession::zero_frame_finding(static_cast<int>(frames_checked) + 1, frame.info.LastPresentTime.QuadPart));
                return;
            }
            ++frames_checked;
            rects_seen += static_cast<long long>(frame.dirty.size() + frame.moves.size());
            POINT first{};
            long long changed = 0;
            long long uncovered = 0;
            covered_changes(frame, previous, first, changed, uncovered);
            changes_seen += changed;
            if (uncovered != 0) {
                problems.add(what + ": " + std::to_string(uncovered) + " of " + std::to_string(changed) + " changed pixels are outside every dirty and move rectangle (" + std::to_string(frame.dirty.size()) +
                             " dirty, " + std::to_string(frame.moves.size()) + " move), first at (" + std::to_string(first.x) + "," + std::to_string(first.y) + ")");
                Kit::keep(context, Kit::sanitize(what) + "_uncovered", frame.image, 0, 0, frame.image.width, frame.image.height, std::vector<std::uint32_t>(previous.pixels.begin(), previous.pixels.end()));
                return;
            }
            previous = frame.image;
            const int cx = static_cast<int>(client.left - dup.output_rect().left + (expect_changed.left + expect_changed.right) / 2);
            const int cy = static_cast<int>(client.top - dup.output_rect().top + (expect_changed.top + expect_changed.bottom) / 2);
            if (frame.image.valid(cx, cy) && (moved || frame.image.at(cx, cy) == expect_colour)) {
                seen = true;
            }
        }
        if (!seen) {
            Bitmap shot;
            std::string shot_error;
            std::string on_screen = "the screen could not be read";
            const int px = client.left + (expect_changed.left + expect_changed.right) / 2;
            const int py = client.top + (expect_changed.top + expect_changed.bottom) / 2;
            if (ScreenCapture::screen({px, py, px + 1, py + 1}, false, shot, shot_error) && shot.width > 0) {
                on_screen = "a GDI read of the screen shows " + std::to_string(shot.at(0, 0) >> 16 & 255) + "," + std::to_string(shot.at(0, 0) >> 8 & 255) + "," + std::to_string(shot.at(0, 0) & 255) + " where " + std::to_string(expect_colour >> 16 & 255) + "," + std::to_string(expect_colour >> 8 & 255) + "," + std::to_string(expect_colour & 255) + " was painted";
            }
            problems.add(what + ": the change never reached the duplicated desktop; " + on_screen + "; the last frame had " + std::to_string(frame.dirty.size()) + " dirty and " + std::to_string(frame.moves.size()) + " move rectangles");
        }
    };
    for (int i = 0; i < 12 && problems.empty(); ++i) {
        const int x = 8 + (i * 37) % 120;
        const int y = 8 + (i * 23) % 80;
        const int w = 20 + (i * 11) % 60;
        const int h = 12 + (i * 7) % 40;
        const RECT r{x, y, std::min(kClientW, x + w), std::min(kClientH, y + h)};
        const COLORREF colour = RGB(30 + i * 17 % 200, 200 - i * 13 % 150, 60 + i * 29 % 180);
        paint(r, colour);
        const std::uint32_t xrgb = (static_cast<std::uint32_t>(GetRValue(colour)) << 16) | (static_cast<std::uint32_t>(GetGValue(colour)) << 8) | GetBValue(colour);
        settle_and_check("paint " + std::to_string(i), r, xrgb, false);
    }
    for (int i = 0; i < 3 && problems.empty(); ++i) {
        window.move(60 + 24 * (i + 1), 60 + 12 * (i + 1));
        settle_and_check("window move " + std::to_string(i), {0, 0, 4, 4}, 0, true);
        ::Sleep(150);
    }
    context.measure("frames_checked", frames_checked);
    context.measure("changed_pixels", changes_seen);
    context.measure("rects_reported", rects_seen);
    if (problems.empty() && changes_seen == 0) {
        problems.add("no pixel of the duplicated desktop changed although the window was repainted twelve times");
    }
    return problems.outcome();
}

Outcome DupChecks::pointer_info(Context& context) {
    TestWindow window;
    TestWindowConfig config;
    config.x = 60;
    config.y = 60;
    TestWindow::outer_size(config.x, config.y, 240, 180, config.width, config.height);
    std::string error;
    if (!window.create(config, error)) {
        return Outcome::fail(error);
    }
    ::SetWindowPos(window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    const RECT client = ScreenCapture::client_on_screen(window.hwnd());
    POINT original{};
    ::GetCursorPos(&original);
    ::SetCursorPos(static_cast<int>(client.left) + 40, static_cast<int>(client.top) + 30);
    ::Sleep(200);
    POINT cursor{};
    POINT hotspot{};
    HCURSOR handle = nullptr;
    if (!ScreenCapture::cursor_state(cursor, hotspot, handle)) {
        ::SetCursorPos(original.x, original.y);
        return Context::skip("the session has no visible cursor, so there is no pointer to duplicate");
    }
    DuplicationSession dup;
    switch (dup.open(client, error)) {
        case DuplicationOpen::Unavailable:
            ::SetCursorPos(original.x, original.y);
            return Context::skip("output duplication is not available: " + error);
        case DuplicationOpen::Failed:
            ::SetCursorPos(original.x, original.y);
            return Outcome::fail(error);
        case DuplicationOpen::Ready:
            break;
    }
    Problems problems;
    const RECT output = dup.output_rect();
    struct Step {
        const char* name;
        LPCWSTR cursor;
        int dx;
        int dy;
    };
    int shapes_seen = 0;
    DuplicationFrame frame;
    std::vector<std::vector<std::uint8_t>> shape_bytes;
    for (const Step& step : {Step{"arrow", MAKEINTRESOURCEW(32512), 50, 40}, Step{"cross", MAKEINTRESOURCEW(32515), 90, 60}, Step{"ibeam", MAKEINTRESOURCEW(32513), 130, 90}}) {
        HCURSOR wanted = ::LoadCursorW(nullptr, step.cursor);
        ::SetClassLongPtrW(window.hwnd(), GCLP_HCURSOR, reinterpret_cast<LONG_PTR>(wanted));
        ::SetCursorPos(static_cast<int>(client.left) + step.dx - 3, static_cast<int>(client.top) + step.dy - 3);
        ::Sleep(120);
        ::SetCursorPos(static_cast<int>(client.left) + step.dx, static_cast<int>(client.top) + step.dy);
        ::Sleep(250);
        POINT now{};
        POINT now_hotspot{};
        HCURSOR now_handle = nullptr;
        if (!ScreenCapture::cursor_state(now, now_hotspot, now_handle)) {
            problems.add(std::string(step.name) + ": the cursor is not visible");
            continue;
        }
        if (now_handle != wanted) {
            context.note(std::string(step.name) + ": the cursor under the pointer is not the window class cursor, the shape check uses the handle the system reports");
        }
        bool position_ok = false;
        bool shape_ok = false;
        std::string position_detail;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while ((!position_ok || !shape_ok) && std::chrono::steady_clock::now() < deadline) {
            error.clear();
            const HRESULT status = dup.acquire(400, frame, error);
            if (status == DXGI_ERROR_WAIT_TIMEOUT) {
                ::SetCursorPos(static_cast<int>(client.left) + step.dx + 1, static_cast<int>(client.top) + step.dy);
                ::Sleep(40);
                ::SetCursorPos(static_cast<int>(client.left) + step.dx, static_cast<int>(client.top) + step.dy);
                continue;
            }
            if (FAILED(status)) {
                problems.add(std::string(step.name) + ": AcquireNextFrame returned " + Kit::hr(status));
                break;
            }
            if (frame.info.LastMouseUpdateTime.QuadPart != 0 && frame.info.PointerPosition.Visible != FALSE) {
                POINT real{};
                ::GetCursorPos(&real);
                const POINT reported = frame.info.PointerPosition.Position;
                const POINT hot{real.x - output.left, real.y - output.top};
                const POINT corner{hot.x - now_hotspot.x, hot.y - now_hotspot.y};
                if ((reported.x == hot.x && reported.y == hot.y) || (reported.x == corner.x && reported.y == corner.y)) {
                    position_ok = true;
                    context.measure(std::string(step.name) + ".position_is_hotspot", reported.x == hot.x && reported.y == hot.y);
                } else {
                    position_detail = "reported (" + std::to_string(reported.x) + "," + std::to_string(reported.y) + ") against GetCursorPos (" + std::to_string(hot.x) + "," + std::to_string(hot.y) +
                                      ") or its image corner (" + std::to_string(corner.x) + "," + std::to_string(corner.y) + ")";
                }
            }
            if (frame.has_shape && !shape_ok) {
                ICONINFO icon{};
                if (::GetIconInfo(now_handle, &icon) != FALSE) {
                    BITMAP bitmap{};
                    const bool colour = icon.hbmColor != nullptr;
                    ::GetObjectW(colour ? icon.hbmColor : icon.hbmMask, sizeof(bitmap), &bitmap);
                    const UINT want_width = static_cast<UINT>(bitmap.bmWidth);
                    const UINT want_height = static_cast<UINT>(bitmap.bmHeight);
                    const bool type_ok = colour ? (frame.shape.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR || frame.shape.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR)
                                                : frame.shape.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME;
                    if (frame.shape.Width != want_width || frame.shape.Height != want_height || !type_ok || static_cast<DWORD>(frame.shape.HotSpot.x) != icon.xHotspot ||
                        static_cast<DWORD>(frame.shape.HotSpot.y) != icon.yHotspot) {
                        problems.add(std::string(step.name) + ": the duplicated shape is type " + std::to_string(frame.shape.Type) + " " + std::to_string(frame.shape.Width) + "x" + std::to_string(frame.shape.Height) +
                                     " hotspot (" + std::to_string(frame.shape.HotSpot.x) + "," + std::to_string(frame.shape.HotSpot.y) + "), the cursor is " + (colour ? "colour " : "monochrome ") +
                                     std::to_string(want_width) + "x" + std::to_string(want_height) + " hotspot (" + std::to_string(icon.xHotspot) + "," + std::to_string(icon.yHotspot) + ")");
                    }
                    if (icon.hbmMask != nullptr) {
                        ::DeleteObject(icon.hbmMask);
                    }
                    if (icon.hbmColor != nullptr) {
                        ::DeleteObject(icon.hbmColor);
                    }
                    shape_ok = true;
                    ++shapes_seen;
                    shape_bytes.push_back(frame.shape_bytes);
                }
            }
        }
        if (!position_ok) {
            problems.add(std::string(step.name) + ": the duplicated pointer position never matched the cursor: " + (position_detail.empty() ? "no mouse update with a visible pointer arrived" : position_detail));
        }
        if (!shape_ok) {
            problems.add(std::string(step.name) + ": no pointer shape was reported after the cursor changed");
        }
    }
    if (shape_bytes.size() == 3 && shape_bytes[0] == shape_bytes[1] && shape_bytes[1] == shape_bytes[2]) {
        problems.add("the arrow, cross and I-beam cursors were reported with identical shape data");
    }
    context.measure("shapes_seen", shapes_seen);
    ::SetCursorPos(original.x, original.y);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::capture
