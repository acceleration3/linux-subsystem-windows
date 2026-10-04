#pragma once

#include <optional>
#include <string>

namespace lsw::agent::win {

// Reads a .lnk shortcut: what it starts, with which arguments, and from
// where.
//
// Initialises COM (single-threaded, which the shell's objects want) for
// the duration of the call, so a caller on any thread can use it without
// knowing the thread's apartment state.
class ShellLink {
public:
    ShellLink() = delete;

    // How the target path is read.
    enum class PathMode {
        // As stored in the file, ``%VARIABLES%`` unexpanded. What a
        // launch wants: it expands them itself, against its own
        // environment.
        Raw,
        // Resolved, preferring a UNC path for a target on a mapped
        // drive. What a configured entry wants to keep.
        UncPriority,
    };

    struct Target {
        std::wstring path;  // empty when the shortcut names no file path
        std::wstring args;
        std::wstring working_dir;
    };

    // Nullopt when ``lnk_path`` cannot be loaded as a shortcut at all.
    // Each field of the result is empty when the shortcut does not
    // carry it.
    [[nodiscard]] static std::optional<Target> resolve(const std::wstring& lnk_path, PathMode mode);
};

}  // namespace lsw::agent::win
