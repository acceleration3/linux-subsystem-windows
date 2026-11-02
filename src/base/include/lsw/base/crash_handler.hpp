#pragma once

#include <string>

namespace lsw::base {
class CrashHandler {
public:
    CrashHandler() = delete;

    static void install(const std::string& logs_dir, const std::string& version);

    static const char* write_report(const char* reason, int signal) noexcept;
};

}
