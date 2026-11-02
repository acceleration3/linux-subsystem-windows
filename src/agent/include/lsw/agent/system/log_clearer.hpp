#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace lsw::agent::system {

class LogClearer {
public:
    [[nodiscard]] nlohmann::json run();

private:
    void sweep(const std::filesystem::path& dir, bool recursive, bool only_logs);
    void clear(const std::filesystem::path& file);
    bool empty(const std::filesystem::path& file, unsigned long& error);
    void skip(const std::filesystem::path& file, unsigned long error);

    int deleted_ = 0;
    int emptied_ = 0;
    std::vector<std::string> skipped_;
};

}
