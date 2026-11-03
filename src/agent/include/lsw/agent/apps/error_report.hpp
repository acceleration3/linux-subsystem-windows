#pragma once

#include "lsw/agent/apps/import_resolver.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::agent::apps {
struct ErrorReport {
    std::string kind;
    std::string app_id;
    unsigned long launch = 0;
    unsigned long pid = 0;
    unsigned long parent = 0;
    std::string image;
    std::uint32_t exit_code = 0;
    std::string code_name;
    std::vector<ImportResolver::Problem> problems;
    std::string note;
    std::vector<std::string> event_text;
    std::string module;
    std::string offset;
    std::string module_version;
    std::string app_version;
};

}
