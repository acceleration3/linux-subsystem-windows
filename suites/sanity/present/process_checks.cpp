#include "sanity/present/process_checks.hpp"

#include "core/child_mode.hpp"
#include "core/text.hpp"
#include "sanity/present/kit.hpp"
#include "support/child_runs.hpp"

#include <thread>

namespace lsw::suites::sanity::present {

namespace {

constexpr int kWidth = 160;
constexpr int kHeight = 120;
constexpr int kSteps = 12;
constexpr const char* kCheckId = "sanity.present.window.many_processes";

Outcome child(Context& context) {
    const std::string path_id = ChildMode::variable(L"LSW_SUITES_PRESENT_PATH");
    const int x = std::stoi(ChildMode::variable(L"LSW_SUITES_PRESENT_X"));
    const int base = std::stoi(ChildMode::variable(L"LSW_SUITES_PRESENT_BASE"));
    HANDLE ready = ::OpenSemaphoreW(SEMAPHORE_MODIFY_STATE, FALSE, Text::wide(ChildMode::variable(L"LSW_SUITES_PRESENT_READY")).c_str());
    HANDLE go = ::OpenEventW(SYNCHRONIZE, FALSE, Text::wide(ChildMode::variable(L"LSW_SUITES_PRESENT_GO")).c_str());
    if (ready == nullptr || go == nullptr) {
        return Outcome::fail("the start gate cannot be opened");
    }
    std::unique_ptr<PresentPath> path = PresentPath::make(path_id);
    std::string error;
    if (!path || !path->open(x, 60, kWidth, kHeight, error)) {
        ::ReleaseSemaphore(ready, 1, nullptr);
        ::CloseHandle(ready);
        ::CloseHandle(go);
        if (path) {
            path->close();
        }
        return error.rfind("unsupported: ", 0) == 0 ? Context::skip(error.substr(13)) : Outcome::fail(path_id + ": " + error);
    }
    ::ReleaseSemaphore(ready, 1, nullptr);
    Problems problems;
    if (::WaitForSingleObject(go, 60000) != WAIT_OBJECT_0) {
        problems.add("the start signal never came");
    } else {
        for (int step = 0; step < kSteps && problems.empty(); ++step) {
            const int frame = base + step;
            const PresentResult result = path->present(frame, 1);
            if (!result.ok) {
                problems.add("frame " + std::to_string(frame) + ": " + result.error);
                break;
            }
            if (!Kit::await_frame(*path, frame, 4000)) {
                problems.add("this process's frame " + std::to_string(frame) + " never became visible; the window shows " + std::to_string(FrameProbe::sample(path->client())));
            }
            ::Sleep(30);
            if (problems.empty() && FrameProbe::sample(path->client()) != frame) {
                problems.add("this process's window changed away from its frame " + std::to_string(frame) + " without a present, it shows " + std::to_string(FrameProbe::sample(path->client())));
            }
        }
        std::string detail;
        if (problems.empty() && (!path->drain(error) || !Kit::settle(context, "child_" + path_id, *path, FrameImage::render(base + kSteps - 1, kWidth, kHeight), 0, detail))) {
            problems.add("the last frame is not exact: " + (detail.empty() ? error : detail));
        }
    }
    ::CloseHandle(ready);
    ::CloseHandle(go);
    path->close();
    return problems.outcome();
}

}  // namespace

Outcome ProcessChecks::many_processes(Context& context) {
    if (ChildMode::active() && !ChildMode::variable(L"LSW_SUITES_PRESENT_PATH").empty()) {
        return child(context);
    }
    std::vector<std::string> chosen;
    for (const char* id : {"dxgi_flip_sequential", "d3d9_blt", "gl_swapbuffers", "vk_fifo", "d3d12_flip"}) {
        for (const std::string& available : PresentPath::ids()) {
            if (available == id) {
                chosen.push_back(id);
            }
        }
    }
    if (chosen.size() < 2) {
        return Context::skip("fewer than two presentation APIs are enabled; the row needs several processes with different APIs");
    }
    const std::string stem = "Local\\lsw-suites-present-" + std::to_string(::GetCurrentProcessId());
    HANDLE ready = ::CreateSemaphoreW(nullptr, 0, 16, Text::wide(stem + "-ready").c_str());
    HANDLE go = ::CreateEventW(nullptr, TRUE, FALSE, Text::wide(stem + "-go").c_str());
    if (ready == nullptr || go == nullptr) {
        return Outcome::fail("the start gate cannot be created");
    }
    std::vector<CheckResult> results(chosen.size());
    std::vector<std::thread> runners;
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        ChildRunOptions options;
        options.timeout_seconds = 120;
        options.env = {{"LSW_SUITES_PRESENT_PATH", chosen[i]},
                       {"LSW_SUITES_PRESENT_X", std::to_string(20 + static_cast<int>(i) * 200)},
                       {"LSW_SUITES_PRESENT_BASE", std::to_string(static_cast<int>(i) * kSteps)},
                       {"LSW_SUITES_PRESENT_READY", stem + "-ready"},
                       {"LSW_SUITES_PRESENT_GO", stem + "-go"}};
        runners.emplace_back([&results, i, options] { results[i] = ChildRuns::run_self(kCheckId, options); });
    }
    Problems problems;
    std::size_t arrived = 0;
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        if (::WaitForSingleObject(ready, 60000) != WAIT_OBJECT_0) {
            break;
        }
        ++arrived;
    }
    if (arrived != chosen.size()) {
        problems.add("only " + std::to_string(arrived) + " of " + std::to_string(chosen.size()) + " processes opened their windows");
    }
    ::SetEvent(go);
    for (std::thread& runner : runners) {
        runner.join();
    }
    int exercised = 0;
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        if (results[i].status == Status::Skip) {
            context.note(chosen[i] + " process: " + results[i].reason);
            continue;
        }
        ++exercised;
        if (results[i].status != Status::Ok) {
            problems.add(chosen[i] + " process: " + results[i].reason);
        }
    }
    ::CloseHandle(ready);
    ::CloseHandle(go);
    context.measure("processes", exercised);
    if (exercised < 2 && problems.empty()) {
        return Context::skip("fewer than two of the processes could present");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::present
