#include "support/gate_window.hpp"

#include "core/child_mode.hpp"
#include "core/text.hpp"
#include "support/child_runs.hpp"
#include "support/render_probe.hpp"

#include <algorithm>

namespace lsw::suites {

namespace {

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

void GateWindow::close(Context& context, Problems& problems, const std::string& label, long long limit_us) {
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
    if (worst > limit_us) {
        problems.add("a render and readback in another process took " + std::to_string(worst / 1000) + " ms during " + label + ", longer than " + std::to_string(limit_us / 1000) + " ms");
    }
}

Outcome GateWindow::peer(Context& context) {
    RenderProbe probe;
    std::string error;
    if (!probe.open(error)) {
        return Outcome::fail(error);
    }
    HANDLE ready = open_named(L"LSW_SUITES_READY_EVENT");
    HANDLE stop = open_named(L"LSW_SUITES_STOP_EVENT");
    long long worst = 0;
    long long cycles = 0;
    Outcome outcome = Outcome::ok();
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(100);
    while (std::chrono::steady_clock::now() < limit && (stop == nullptr || ::WaitForSingleObject(stop, 0) != WAIT_OBJECT_0)) {
        const auto begin = std::chrono::steady_clock::now();
        if (!probe.cycle(static_cast<unsigned>(cycles), error)) {
            outcome = Outcome::fail("peer " + error);
            break;
        }
        const long long took = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin).count();
        if (cycles > 0) {
            worst = std::max(worst, took);
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
