#include "support/gate_window.hpp"

#include "core/child_mode.hpp"
#include "core/text.hpp"
#include "support/child_runs.hpp"
#include "support/d3d11_tools.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>
#include <cstdlib>

namespace lsw::suites {

namespace {

constexpr UINT kSide = 256;

HANDLE open_named(const wchar_t* variable) {
    const std::string name = ChildMode::variable(variable);
    return name.empty() ? nullptr : ::OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, Text::wide(name).c_str());
}

}  // namespace

GateWindow::GateWindow(std::string check_id) : id_(std::move(check_id)) {}

GateWindow::~GateWindow() {
    stop();
    if (ready_ != nullptr) {
        ::CloseHandle(ready_);
    }
    if (stop_ != nullptr) {
        ::CloseHandle(stop_);
    }
}

void GateWindow::open(Problems& problems) {
    const std::string suffix = std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(::GetTickCount64());
    const std::string ready_name = "Local\\lsw-suites-gate-ready-" + suffix;
    const std::string stop_name = "Local\\lsw-suites-gate-stop-" + suffix;
    ready_ = ::CreateEventW(nullptr, TRUE, FALSE, Text::wide(ready_name).c_str());
    stop_ = ::CreateEventW(nullptr, TRUE, FALSE, Text::wide(stop_name).c_str());
    if (ready_ == nullptr || stop_ == nullptr) {
        problems.add("the peer process events could not be created");
        return;
    }
    ChildRunOptions options;
    options.timeout_seconds = 120;
    options.env = {{"LSW_SUITES_ROLE", "gate-peer"}, {"LSW_SUITES_READY_EVENT", ready_name}, {"LSW_SUITES_STOP_EVENT", stop_name}};
    const std::string id = id_;
    thread_ = std::thread([this, id, options] { result_ = ChildRuns::run_self(id, options); });
    started_ = true;
    if (::WaitForSingleObject(ready_, 60000) != WAIT_OBJECT_0) {
        problems.add("the peer process never completed its first render and readback");
    }
}

void GateWindow::begin() {
    begin_ = std::chrono::steady_clock::now();
}

long long GateWindow::elapsed_ms() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin_).count();
}

void GateWindow::sleep_until(int milliseconds) const {
    const long long remaining = milliseconds - elapsed_ms();
    if (remaining > 0) {
        ::Sleep(static_cast<DWORD>(remaining));
    }
}

void GateWindow::stop() {
    if (stop_ != nullptr) {
        ::SetEvent(stop_);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void GateWindow::close(Context& context, Problems& problems, const std::string& label) {
    stop();
    if (!started_) {
        return;
    }
    started_ = false;
    if (result_.status != Status::Ok) {
        problems.add("the peer process rendering during " + label + " ended " + Outcome::name(result_.status) + ": " + result_.reason);
        return;
    }
    const long long worst = ChildRuns::number(result_, "peer.worst_us");
    const long long cycles = ChildRuns::number(result_, "peer.cycles");
    context.measure(label + ".peer.worst_us", worst);
    context.measure(label + ".peer.cycles", cycles);
    if (cycles < 3) {
        problems.add("the peer process completed only " + std::to_string(cycles) + " render and readback cycles during " + label);
    }
    if (worst > kProbeLimitUs) {
        problems.add("a render and readback in another process took " + std::to_string(worst / 1000) + " ms during " + label + ", longer than " + std::to_string(kProbeLimitUs / 1000) + " ms");
    }
}

Outcome GateWindow::peer(Context& context) {
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    Com<ID3D11Texture2D> target = D3d11Tools::target(device, kSide, kSide);
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target.get());
    if (!target || !view) {
        return Outcome::fail("creating the peer render target failed");
    }
    HANDLE ready = open_named(L"LSW_SUITES_READY_EVENT");
    HANDLE stop = open_named(L"LSW_SUITES_STOP_EVENT");
    long long worst = 0;
    long long cycles = 0;
    Outcome outcome = Outcome::ok();
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(100);
    while (std::chrono::steady_clock::now() < limit && (stop == nullptr || ::WaitForSingleObject(stop, 0) != WAIT_OBJECT_0)) {
        const unsigned step = static_cast<unsigned>(cycles % 251);
        const float r = static_cast<float>(step) / 255.0f;
        const float g = static_cast<float>(255 - step) / 255.0f;
        const auto begin = std::chrono::steady_clock::now();
        D3d11Tools::clear(device, view.get(), r, g, 0.5f, 1.0f);
        std::vector<std::uint8_t> pixels;
        if (!D3d11Tools::read_pixels(device, target.get(), pixels, error)) {
            outcome = Outcome::fail("peer readback failed: " + error);
            break;
        }
        const long long took = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin).count();
        if (cycles > 0) {
            worst = std::max(worst, took);
        }
        const std::size_t middle = (static_cast<std::size_t>(kSide / 2) * kSide + kSide / 2) * 4;
        const int expected_r = static_cast<int>(r * 255.0f + 0.5f);
        const int expected_g = static_cast<int>(g * 255.0f + 0.5f);
        if (pixels.size() < middle + 4 || std::abs(pixels[middle] - expected_r) > 1 || std::abs(pixels[middle + 1] - expected_g) > 1 ||
            std::abs(pixels[middle + 2] - 128) > 1) {
            outcome = Outcome::fail("peer readback returned wrong colour in cycle " + std::to_string(cycles));
            break;
        }
        if (++cycles == 1 && ready != nullptr) {
            ::SetEvent(ready);
        }
        ::Sleep(25);
    }
    if (ready != nullptr) {
        ::CloseHandle(ready);
    }
    if (stop != nullptr) {
        ::CloseHandle(stop);
    }
    context.measure("peer.worst_us", worst);
    context.measure("peer.cycles", cycles);
    return outcome;
}

}  // namespace lsw::suites
