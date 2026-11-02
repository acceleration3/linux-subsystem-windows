#pragma once

#include <optional>
#include <string>

namespace lsw::agent::win {
class ShellLink {
public:
    ShellLink() = delete;

    enum class PathMode {
        Raw,
        UncPriority,
    };

    struct Target {
        std::wstring path;
        std::wstring args;
        std::wstring working_dir;
    };

    [[nodiscard]] static std::optional<Target> resolve(const std::wstring& lnk_path, PathMode mode);
};

}
