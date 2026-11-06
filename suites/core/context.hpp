#pragma once

#include "core/json.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace lsw::suites {
class Context {
public:
    Context(std::string id, std::filesystem::path artifacts_root);

    [[nodiscard]] const std::string& id() const { return id_; }

    [[nodiscard]] int bitness() const { return static_cast<int>(sizeof(void*) * 8); }

    [[nodiscard]] std::filesystem::path artifacts_dir();

    void log(const std::string& line);

    void note(const std::string& line);

    void measure(const std::string& key, Json value);

    void artifact(const std::filesystem::path& path);

    [[nodiscard]] const Json& measures() const { return measures_; }

    [[nodiscard]] const std::vector<std::string>& artifacts() const { return artifacts_; }

    [[nodiscard]] const std::vector<std::string>& notes() const { return notes_; }

    [[nodiscard]] const std::filesystem::path& log_path() const { return log_path_; }

private:
    std::string id_;
    std::filesystem::path root_;
    std::filesystem::path log_path_;
    Json measures_ = Json::object();
    std::vector<std::string> artifacts_;
    std::vector<std::string> notes_;
};
}  // namespace lsw::suites
