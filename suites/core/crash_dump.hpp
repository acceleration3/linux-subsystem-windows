#pragma once

#include <filesystem>

namespace lsw::suites {
class CrashDump {
public:
    [[nodiscard]] static bool requested();

    static void install(const std::filesystem::path& file);
};
}  // namespace lsw::suites
