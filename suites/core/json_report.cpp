#include "core/json_report.hpp"

#include "core/exe_identity.hpp"

#include <windows.h>

#include <cstdlib>
#include <fstream>

namespace lsw::suites {

JsonReport::JsonReport(std::string exe_name, std::string started)
    : exe_(std::move(exe_name)), started_(std::move(started)) {}

Json JsonReport::check_json(const CheckResult& result) {
    Json tags = Json::array();
    for (const std::string& tag : result.tags) {
        tags.push(tag);
    }
    Json artifacts = Json::array();
    for (const std::string& artifact : result.artifacts) {
        artifacts.push(artifact);
    }
    Json notes = Json::array();
    for (const std::string& note : result.notes) {
        notes.push(note);
    }
    Json check = Json::object();
    check.set("id", result.id);
    check.set("suite", result.suite);
    check.set("tags", std::move(tags));
    check.set("status", Outcome::name(result.status));
    check.set("reason", result.reason);
    check.set("ms", result.ms);
    check.set("pid", result.pid);
    check.set("started", result.started);
    check.set("artifacts", std::move(artifacts));
    check.set("notes", std::move(notes));
    check.set("measures", result.measures);
    check.set("output", result.output);
    return check;
}

bool JsonReport::write(const std::filesystem::path& path,
                       const std::vector<CheckResult>& results, bool complete) const {
    Json checks = Json::array();
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    for (const CheckResult& result : results) {
        checks.push(check_json(result));
        if (result.status == Status::Ok) {
            ++passed;
        } else if (result.status == Status::Skip) {
            ++skipped;
        } else {
            ++failed;
        }
    }
    Json summary = Json::object();
    summary.set("passed", passed);
    summary.set("failed", failed);
    summary.set("skipped", skipped);
    Json root = Json::object();
    root.set("exe", exe_);
    root.set("bitness", ExeIdentity::kBitness);
    Json adapter = Json::object();
    const char* under_test = std::getenv("LSW_RENDER_DUT");
    if (under_test != nullptr && std::string(under_test) == "warp") {
        adapter.set("device_under_test", "WARP");
    }
    root.set("adapter", std::move(adapter));
    root.set("driver", Json::object());
    root.set("started", started_);
    root.set("complete", complete);
    root.set("summary", std::move(summary));
    root.set("checks", std::move(checks));
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary);
        file << root.dump(2) << '\n';
        if (!file.good()) {
            return false;
        }
    }
    return ::MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

}  // namespace lsw::suites
