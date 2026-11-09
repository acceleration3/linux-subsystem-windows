#include "core/single_check.hpp"

#include "core/child_protocol.hpp"
#include "core/context.hpp"
#include "core/crash_dump.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>

namespace lsw::suites {

namespace {

bool write_stdout(const std::string& text) {
    HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == nullptr || out == INVALID_HANDLE_VALUE) {
        return false;
    }
    std::size_t done = 0;
    while (done < text.size()) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(text.size() - done, 1u << 16));
        if (::WriteFile(out, text.data() + done, chunk, &written, nullptr) == FALSE || written == 0) {
            return false;
        }
        done += written;
    }
    return true;
}

}  // namespace

SingleCheck::SingleCheck(const Registry& registry, std::filesystem::path artifacts)
    : registry_(registry), artifacts_(std::move(artifacts)) {}

int SingleCheck::run(const std::string& id) const {
    ::SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    Context context(id, artifacts_);
    if (CrashDump::requested()) {
        CrashDump::install(context.artifacts_dir() / "crash.dmp");
    }
    const Suite* owner = nullptr;
    const Check* check = registry_.find_check(id, &owner);
    if (check == nullptr) {
        const std::string text = ChildProtocol::encode(context, Outcome::fail("no such check"));
        std::fputs(text.c_str(), stdout);
        return 2;
    }
    if (!check->exclusive && owner != nullptr && owner->policy() == Policy::Parallel) {
        ::SetEnvironmentVariableW(L"LSW_SUITES_QUIET_DESKTOP", L"1");
    }
    Outcome outcome = Outcome::fail("the check returned nothing");
    try {
        outcome = check->run(context);
    } catch (const std::exception& error) {
        outcome = Outcome::fail(std::string("exception: ") + error.what());
    } catch (...) {
        outcome = Outcome::fail("unknown exception");
    }
    if (outcome.status() != Status::Ok && !context.log_path().empty()) {
        context.artifact(context.log_path());
    }
    if (outcome.status() == Status::Skip && outcome.reason().empty()) {
        outcome = Outcome::fail("skipped without a reason");
    }
    const std::filesystem::path copy = context.artifacts_dir() / "result.txt";
    {
        std::ofstream file(copy, std::ios::binary);
        file << ChildProtocol::encode(context, outcome);
        if (file.good()) {
            context.artifact(copy);
        }
    }
    const std::string text = ChildProtocol::encode(context, outcome);
    if (!write_stdout(text)) {
        std::fprintf(stderr, "lsw-suites: writing the result of %s to standard output failed (error %lu)\n", id.c_str(), ::GetLastError());
        return 3;
    }
    return outcome.status() == Status::Fail ? 1 : 0;
}

}  // namespace lsw::suites
