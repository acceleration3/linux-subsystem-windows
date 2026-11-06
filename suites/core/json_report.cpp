#include "core/json_report.hpp"

#include "core/exe_identity.hpp"

#include <fstream>

namespace lsw::suites {

JsonReport::JsonReport(std::string exe_name, std::string started)
    : exe_(std::move(exe_name)), started_(std::move(started)) {}

bool JsonReport::write(const std::filesystem::path& path,
                       const std::vector<CheckResult>& results) const {
    Json checks = Json::array();
    for (const CheckResult& result : results) {
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
        checks.push(std::move(check));
    }
    Json root = Json::object();
    root.set("exe", exe_);
    root.set("bitness", ExeIdentity::kBitness);
    root.set("adapter", Json::object());
    root.set("driver", Json::object());
    root.set("started", started_);
    root.set("checks", std::move(checks));
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    std::ofstream file(path, std::ios::binary);
    file << root.dump(2) << '\n';
    return file.good();
}

}  // namespace lsw::suites
