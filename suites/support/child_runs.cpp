#include "support/child_runs.hpp"

#include "core/child_mode.hpp"
#include "core/exe_identity.hpp"
#include "core/text.hpp"
#include "core/worker.hpp"

#include <windows.h>

namespace lsw::suites {

namespace {

std::filesystem::path scratch_root() {
    return std::filesystem::temp_directory_path() / "lsw-suites-child";
}

}  // namespace

std::filesystem::path ChildRuns::own_exe() {
    wchar_t buffer[MAX_PATH * 4];
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH * 4);
    return std::filesystem::path(std::wstring(buffer, length));
}

std::filesystem::path ChildRuns::sibling_exe() {
    const int other = ExeIdentity::kBitness == 64 ? 32 : 64;
    return own_exe().parent_path() / ("lsw-suites" + std::to_string(other) + ".exe");
}

CheckResult ChildRuns::run_self(const std::string& id, const ChildRunOptions& options) {
    Worker worker(scratch_root());
    worker.set_child_mode();
    if (!options.cwd.empty()) {
        worker.set_cwd(options.cwd);
    }
    if (options.restricted) {
        worker.set_restricted_token();
    }
    for (const auto& [name, value] : options.env) {
        worker.add_env(Text::wide(name), Text::wide(value));
    }
    return worker.run(id, options.timeout_seconds);
}

bool ChildRuns::run_sibling(const std::string& id, const ChildRunOptions& options,
                            CheckResult& result, std::string& error) {
    const std::filesystem::path exe = sibling_exe();
    std::error_code code;
    if (!std::filesystem::exists(exe, code)) {
        error = exe.string() + " is not next to this executable";
        return false;
    }
    Worker worker(scratch_root());
    worker.set_exe(exe);
    worker.set_child_mode();
    for (const auto& [name, value] : options.env) {
        worker.add_env(Text::wide(name), Text::wide(value));
    }
    result = worker.run(id, options.timeout_seconds);
    if (result.measures.size() == 0) {
        error = std::string(exe.filename().string()) + " reported no measures for " + id + ": " +
                Outcome::name(result.status) + " " + result.reason;
        return false;
    }
    return true;
}

std::string ChildRuns::text(const CheckResult& result, const std::string& key) {
    const Json* value = result.measures.find(key);
    return value != nullptr && value->kind() == Json::Kind::String ? value->as_string() : std::string();
}

long long ChildRuns::number(const CheckResult& result, const std::string& key) {
    const Json* value = result.measures.find(key);
    return value != nullptr ? static_cast<long long>(value->as_int()) : -1;
}

bool ChildRuns::has(const CheckResult& result, const std::string& key) {
    return result.measures.find(key) != nullptr;
}

}  // namespace lsw::suites
