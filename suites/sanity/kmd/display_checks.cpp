#include "sanity/kmd/display_checks.hpp"

#include "core/text.hpp"
#include "sanity/capture/dup_checks.hpp"
#include "sanity/life/kit.hpp"
#include "support/bitmap.hpp"
#include "support/kmt_api.hpp"
#include "support/kmt_extra.hpp"
#include "support/problems.hpp"
#include "support/refresh_rate.hpp"
#include "support/screen_capture.hpp"
#include "support/av_adapters.hpp"
#include "support/target_adapter.hpp"
#include "support/test_window.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>

namespace lsw::suites::sanity::kmd {

namespace {

using Clock = std::chrono::steady_clock;

struct Display {
    KmtApi api;
    KmtExtra extra;
    AvAdapter adapter;
    std::uint32_t handle = 0;
    std::uint32_t source = 0;
    bool hybrid_discrete = false;

    bool open(std::string& error) {
        if (!TargetAdapter::pick(adapter, error) || !api.load(error) || !extra.load(error)) {
            return false;
        }
        hybrid_discrete = adapter.hybrid_discrete;
        if (adapter.hybrid_discrete) {
            std::vector<AvAdapter> all;
            if (!AvAdapters::accel(all, error)) {
                return false;
            }
            for (const AvAdapter& candidate : all) {
                if (candidate.hybrid_integrated || (!candidate.hybrid_discrete && candidate.has_outputs())) {
                    adapter = candidate;
                    break;
                }
            }
        }
        if (api.open_from_luid(adapter.luid, handle) != 0) {
            error = "D3DKMTOpenAdapterFromLuid failed";
            return false;
        }
        return true;
    }

    ~Display() {
        if (handle != 0) {
            static_cast<void>(api.close(handle));
        }
    }
};

double ms_since(Clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

bool measure_cadence(Display& display, int count, double& mean, double& worst, std::string& error) {
    std::int32_t status = display.extra.wait_vblank(display.handle, display.source);
    if (status != 0) {
        error = "D3DKMTWaitForVerticalBlankEvent returned 0x" + Text::hex(static_cast<std::uint32_t>(status), 8);
        return false;
    }
    const auto begin = Clock::now();
    auto last = begin;
    worst = 0;
    for (int i = 0; i < count; ++i) {
        status = display.extra.wait_vblank(display.handle, display.source);
        if (status != 0) {
            error = "a vblank wait returned 0x" + Text::hex(static_cast<std::uint32_t>(status), 8);
            return false;
        }
        const auto now = Clock::now();
        worst = std::max(worst, std::chrono::duration<double, std::milli>(now - last).count());
        last = now;
    }
    mean = ms_since(begin) / count;
    return true;
}

HCURSOR make_mono(int size, bool invert) {
    const int bytes = size * size / 8;
    std::vector<std::uint8_t> and_mask(static_cast<std::size_t>(bytes), 0xff);
    std::vector<std::uint8_t> xor_mask(static_cast<std::size_t>(bytes), 0x00);
    for (int y = size / 4; y < size * 3 / 4; ++y) {
        for (int x = size / 4; x < size * 3 / 4; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * size + x) / 8;
            and_mask[index] &= static_cast<std::uint8_t>(~(0x80 >> (x % 8)));
            if (invert) {
                xor_mask[index] |= static_cast<std::uint8_t>(0x80 >> (x % 8));
            }
        }
    }
    return ::CreateCursor(::GetModuleHandleW(nullptr), 0, 0, size, size, and_mask.data(), xor_mask.data());
}

HCURSOR make_color(int size, bool alpha) {
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = size;
    header.bV5Height = -size;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = ::GetDC(nullptr);
    HBITMAP color = ::CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
    ::ReleaseDC(nullptr, dc);
    HBITMAP mask = ::CreateBitmap(size, size, 1, 1, nullptr);
    if (color == nullptr || mask == nullptr || bits == nullptr) {
        return nullptr;
    }
    auto* pixels = static_cast<std::uint32_t*>(bits);
    for (int i = 0; i < size * size; ++i) {
        pixels[i] = alpha ? 0x80FF3060u : 0xFF3060FFu;
    }
    ICONINFO info{FALSE, static_cast<DWORD>(size / 2), static_cast<DWORD>(size / 2), mask, color};
    HICON icon = ::CreateIconIndirect(&info);
    ::DeleteObject(color);
    ::DeleteObject(mask);
    return reinterpret_cast<HCURSOR>(icon);
}

}  // namespace

Outcome DisplayChecks::vblank_wait_cadence(Context& context) {
    Display display;
    std::string error;
    if (!display.open(error)) {
        return Outcome::fail(error);
    }
    double mean = 0;
    double worst = 0;
    if (!measure_cadence(display, 60, mean, worst, error)) {
        return Outcome::fail(error);
    }
    const double period = RefreshRate::period_ms();
    context.measure("mean_ms", mean);
    context.measure("worst_ms", worst);
    context.measure("period_ms", period);
    Problems problems;
    if (std::fabs(mean - period) > period * 0.15) {
        problems.add("the mean vblank wait is " + std::to_string(mean) + " ms against the mode's " + std::to_string(period) + " ms");
    }
    if (worst > period * 5) {
        problems.add("one vblank wait took " + std::to_string(worst) + " ms, over five periods");
    }
    return problems.outcome();
}

Outcome DisplayChecks::scanline_monotonic(Context& context) {
    Display display;
    std::string error;
    if (!display.open(error)) {
        return Outcome::fail(error);
    }
    DEVMODEW mode{};
    static_cast<void>(life::Kit::current_mode(mode));
    const std::uint32_t limit = mode.dmPelsHeight + mode.dmPelsHeight / 2 + 64;
    Problems problems;
    std::uint32_t previous = 0;
    bool was_vblank = false;
    int frames = 0;
    int samples = 0;
    std::uint32_t highest = 0;
    const auto begin = Clock::now();
    while (ms_since(begin) < 1500 && problems.empty()) {
        bool vblank = false;
        std::uint32_t line = 0;
        const std::int32_t status = display.extra.scan_line(display.handle, display.source, vblank, line);
        if (status != 0) {
            return Outcome::fail("D3DKMTGetScanLine returned 0x" + Text::hex(static_cast<std::uint32_t>(status), 8));
        }
        ++samples;
        highest = std::max(highest, line);
        if (line > limit) {
            problems.add("scan line " + std::to_string(line) + " is beyond the active height " + std::to_string(mode.dmPelsHeight) + " plus blanking");
        }
        if (!vblank && !was_vblank && line < previous && previous - line > 4) {
            problems.add("the scan line went backwards from " + std::to_string(previous) + " to " + std::to_string(line) + " without a vblank flag");
        }
        if (was_vblank && !vblank) {
            ++frames;
        }
        was_vblank = vblank;
        previous = line;
    }
    context.measure("samples", samples);
    context.measure("frames_seen", frames);
    context.measure("highest_line", highest);
    return problems.outcome();
}

Outcome DisplayChecks::vblank_vs_dwm_timing(Context& context) {
    Display display;
    std::string error;
    if (!display.open(error)) {
        return Outcome::fail(error);
    }
    double mean = 0;
    double worst = 0;
    if (!measure_cadence(display, 90, mean, worst, error)) {
        return Outcome::fail(error);
    }
    const double dwm_hz = RefreshRate::hz();
    const double measured_hz = 1000.0 / mean;
    context.measure("dwm_hz", dwm_hz);
    context.measure("vblank_hz", measured_hz);
    if (std::fabs(measured_hz - dwm_hz) > dwm_hz * 0.05) {
        return Outcome::fail("the vblank cadence is " + std::to_string(measured_hz) + " Hz, DWM reports " + std::to_string(dwm_hz) + " Hz");
    }
    return Outcome::ok();
}

Outcome DisplayChecks::source_visible_while_composed(Context& context) {
    Display display;
    std::string error;
    if (!display.open(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    const std::int32_t owner = display.extra.exclusive_ownership(display.handle, display.source);
    context.measure("exclusive_ownership_status", Text::hex(static_cast<std::uint32_t>(owner), 8));
    if (owner != 0) {
        problems.add("D3DKMTCheckVidPnExclusiveOwnership reports an exclusive owner (0x" + Text::hex(static_cast<std::uint32_t>(owner), 8) + ") while the desktop is composed");
    }
    bool attached = false;
    for (const AvOutput& output : display.adapter.outputs) {
        attached = attached || output.attached;
    }
    if (!attached && display.hybrid_discrete) {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        for (DWORD index = 0; ::EnumDisplayDevicesW(nullptr, index, &device, 0) != FALSE; ++index) {
            attached = attached || (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0;
        }
    }
    if (!attached) {
        problems.add("no output of the adapter is attached to the desktop");
    }
    TestWindow window;
    TestWindowConfig config;
    config.popup = true;
    config.topmost = true;
    if (window.create(config, error)) {
        window.paint_with(RGB(30, 150, 60));
    }
    Bitmap shot;
    RECT desktop{0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
    ::Sleep(300);
    if (!ScreenCapture::screen(desktop, false, shot, error)) {
        problems.add("capturing the desktop failed: " + error);
    } else {
        std::set<std::uint32_t> colours;
        for (int y = 0; y < shot.height; y += 8) {
            for (int x = 0; x < shot.width; x += 8) {
                colours.insert(shot.at(x, y));
            }
        }
        if (colours.size() < 2 || (colours.size() == 1 && *colours.begin() == 0)) {
            problems.add("the captured desktop is uniform");
        }
    }
    return problems.outcome();
}

Outcome DisplayChecks::mode_set_restore(Context& context) {
    DEVMODEW original{};
    if (!life::Kit::current_mode(original)) {
        return Outcome::fail("the current display mode cannot be read");
    }
    RECT before{0, 0, ::GetSystemMetrics(SM_CXVIRTUALSCREEN), ::GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    std::set<std::pair<DWORD, DWORD>> seen;
    Problems problems;
    int tried = 0;
    for (const DEVMODEW& mode : life::Kit::modes()) {
        if (!seen.insert({mode.dmPelsWidth, mode.dmPelsHeight}).second || tried >= 12) {
            continue;
        }
        ++tried;
        const LONG result = life::Kit::set_mode(mode);
        if (result != DISP_CHANGE_SUCCESSFUL) {
            problems.add("setting " + std::to_string(mode.dmPelsWidth) + "x" + std::to_string(mode.dmPelsHeight) + " returned " + std::to_string(result));
            continue;
        }
        ::Sleep(300);
        if (::GetSystemMetrics(SM_CXSCREEN) != static_cast<int>(mode.dmPelsWidth) || ::GetSystemMetrics(SM_CYSCREEN) != static_cast<int>(mode.dmPelsHeight)) {
            problems.add("GetSystemMetrics does not report " + std::to_string(mode.dmPelsWidth) + "x" + std::to_string(mode.dmPelsHeight) + " after setting it");
        }
        life::Kit::restore_mode();
        ::Sleep(300);
        DEVMODEW now{};
        RECT after{0, 0, ::GetSystemMetrics(SM_CXVIRTUALSCREEN), ::GetSystemMetrics(SM_CYVIRTUALSCREEN)};
        if (!life::Kit::current_mode(now) || now.dmPelsWidth != original.dmPelsWidth || now.dmPelsHeight != original.dmPelsHeight || now.dmDisplayFrequency != original.dmDisplayFrequency ||
            after.right != before.right || after.bottom != before.bottom) {
            problems.add("the original mode and desktop layout were not restored after " + std::to_string(mode.dmPelsWidth) + "x" + std::to_string(mode.dmPelsHeight));
        }
    }
    context.measure("modes_tried", tried);
    return problems.outcome();
}

Outcome DisplayChecks::mode_list_all_settable(Context& context) {
    Problems problems;
    int tested = 0;
    const std::vector<DEVMODEW> modes = life::Kit::modes();
    const std::size_t step = std::max<std::size_t>(1, modes.size() / 20);
    for (std::size_t i = 0; i < modes.size(); i += step) {
        DEVMODEW mode = modes[i];
        mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_BITSPERPEL;
        const LONG result = ::ChangeDisplaySettingsExW(nullptr, &mode, nullptr, CDS_TEST, nullptr);
        ++tested;
        if (result != DISP_CHANGE_SUCCESSFUL) {
            problems.add("CDS_TEST rejected the enumerated mode " + std::to_string(mode.dmPelsWidth) + "x" + std::to_string(mode.dmPelsHeight) + "@" + std::to_string(mode.dmDisplayFrequency) + " with " + std::to_string(result));
        }
    }
    DEVMODEW bad{};
    bad.dmSize = sizeof(bad);
    bad.dmPelsWidth = 123;
    bad.dmPelsHeight = 45;
    bad.dmBitsPerPel = 32;
    bad.dmDisplayFrequency = 61;
    bad.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_BITSPERPEL;
    const LONG refused = ::ChangeDisplaySettingsExW(nullptr, &bad, nullptr, CDS_TEST, nullptr);
    if (refused != DISP_CHANGE_BADMODE) {
        problems.add("CDS_TEST of 123x45 returned " + std::to_string(refused) + " instead of DISP_CHANGE_BADMODE");
    }
    context.measure("tested", tested);
    return problems.outcome();
}

Outcome DisplayChecks::cursor_shapes(Context& context) {
    TestWindow window;
    TestWindowConfig config;
    config.x = 100;
    config.y = 100;
    config.width = 300;
    config.height = 300;
    config.topmost = true;
    std::string error;
    if (!window.create(config, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    RECT area{};
    ::GetWindowRect(window.hwnd(), &area);
    for (int size : {32, 48, 64}) {
        struct Shape {
            const char* name;
            HCURSOR cursor;
        } shapes[] = {{"monochrome", make_mono(size, false)}, {"xor monochrome", make_mono(size, true)}, {"colour", make_color(size, false)}, {"alpha-blended", make_color(size, true)}};
        for (const Shape& shape : shapes) {
            if (shape.cursor == nullptr) {
                problems.add(std::string("creating a ") + std::to_string(size) + " pixel " + shape.name + " cursor failed");
                continue;
            }
            ::SetClassLongPtrW(window.hwnd(), GCLP_HCURSOR, reinterpret_cast<LONG_PTR>(shape.cursor));
            const POINT centre{(area.left + area.right) / 2, (area.top + area.bottom) / 2};
            ::SetCursorPos(centre.x, centre.y);
            POINT now{};
            ::GetCursorPos(&now);
            const bool inside = now.x >= area.left && now.x < area.right && now.y >= area.top && now.y < area.bottom;
            bool matched = false;
            for (int i = 0; i < 20 && !matched; ++i) {
                ::SetCursor(shape.cursor);
                ::Sleep(25);
                matched = ::GetCursor() == shape.cursor;
            }
            if (!matched) {
                problems.add(std::string("GetCursor does not return the ") + std::to_string(size) + " pixel " + shape.name + " cursor the thread set");
            }
            if (inside) {
                CURSORINFO info{};
                info.cbSize = sizeof(info);
                const bool shown = ::GetCursorInfo(&info) != FALSE && info.hCursor == shape.cursor && (info.flags & CURSOR_SHOWING) != 0;
                context.measure(std::string("kmd.cursor_shape.") + std::to_string(size) + "." + shape.name + ".reported_by_getcursorinfo", shown);
            } else {
                context.measure(std::string("kmd.cursor_shape.") + std::to_string(size) + "." + shape.name + ".pointer_outside_window", true);
            }
            ::SetClassLongPtrW(window.hwnd(), GCLP_HCURSOR, reinterpret_cast<LONG_PTR>(::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512))));
            ::DestroyCursor(shape.cursor);
        }
    }
    return problems.outcome();
}

Outcome DisplayChecks::cursor_position_roundtrip(Context&) {
    const int left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int right = left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1;
    const int bottom = top + ::GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1;
    POINT saved{};
    ::GetCursorPos(&saved);
    Problems problems;
    struct Case {
        int x;
        int y;
    };
    const Case cases[] = {{left, top}, {right, bottom}, {(left + right) / 2, (top + bottom) / 2}, {left + 17, bottom - 3}, {-100, -100}, {right + 500, bottom + 500}, {-1, (top + bottom) / 2}, {left - 40, top + 9}};
    for (const Case& item : cases) {
        ::SetCursorPos(item.x, item.y);
        POINT now{};
        ::GetCursorPos(&now);
        const int want_x = std::clamp(item.x, left, right);
        const int want_y = std::clamp(item.y, top, bottom);
        if (now.x != want_x || now.y != want_y) {
            problems.add("SetCursorPos(" + std::to_string(item.x) + "," + std::to_string(item.y) + ") reads back " + std::to_string(now.x) + "," + std::to_string(now.y) + ", expected " + std::to_string(want_x) + "," + std::to_string(want_y));
        }
    }
    ::SetCursorPos(saved.x, saved.y);
    return problems.outcome();
}

Outcome DisplayChecks::gamma_ramp_roundtrip(Context&) {
    HDC dc = ::GetDC(nullptr);
    WORD original[3][256];
    if (!::GetDeviceGammaRamp(dc, original)) {
        ::ReleaseDC(nullptr, dc);
        return Context::skip("the output does not report a gamma ramp");
    }
    WORD ramp[3][256];
    for (int channel = 0; channel < 3; ++channel) {
        for (int i = 0; i < 256; ++i) {
            ramp[channel][i] = static_cast<WORD>(i * 256 * 9 / 10);
        }
    }
    Problems problems;
    if (!::SetDeviceGammaRamp(dc, ramp)) {
        ::ReleaseDC(nullptr, dc);
        return Context::skip("the output does not accept a gamma ramp");
    }
    WORD back[3][256];
    if (!::GetDeviceGammaRamp(dc, back)) {
        problems.add("GetDeviceGammaRamp failed after setting");
    } else {
        for (int channel = 0; channel < 3; ++channel) {
            for (int i = 0; i < 256; ++i) {
                if (std::abs(static_cast<int>(back[channel][i]) - static_cast<int>(ramp[channel][i])) > 257) {
                    problems.add("channel " + std::to_string(channel) + " entry " + std::to_string(i) + " reads " + std::to_string(back[channel][i]) + ", set " + std::to_string(ramp[channel][i]));
                    channel = 3;
                    break;
                }
            }
        }
    }
    WORD reversed[3][256];
    for (int channel = 0; channel < 3; ++channel) {
        for (int i = 0; i < 256; ++i) {
            reversed[channel][i] = static_cast<WORD>((255 - i) * 256);
        }
    }
    if (::SetDeviceGammaRamp(dc, reversed)) {
        problems.add("a strictly decreasing gamma ramp was accepted");
    }
    ::SetDeviceGammaRamp(dc, original);
    ::ReleaseDC(nullptr, dc);
    return problems.outcome();
}

Outcome DisplayChecks::output_duplication_frame(Context& context) {
    return capture::DupChecks::desktop_duplication_content(context);
}

}  // namespace lsw::suites::sanity::kmd
