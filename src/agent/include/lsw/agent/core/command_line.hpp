#pragma once

#include <string>
#include <vector>

namespace lsw::agent::core {

// How the one agent executable was asked to run.
//
//   lsw-agent.exe --service              the Windows service (SCM entry)
//   lsw-agent.exe --session --pipe NAME  a session helper, started by the
//                                        service in a user's session
//   lsw-agent.exe                        the service's work in the
//                                        foreground, for debugging
//
// Parsed without Win32 so the rules are testable: an unknown argument is
// an error rather than silently meaning "foreground", because a typo in
// the service's binPath would otherwise start a second agent that is not
// a service at all.
struct CommandLine {
    enum class Mode { Foreground, Service, SessionHelper };

    Mode mode = Mode::Foreground;
    std::wstring pipe;   // SessionHelper only
    std::wstring error;  // non-empty when the arguments made no sense

    [[nodiscard]] static CommandLine parse(const std::vector<std::wstring>& args);
};

}  // namespace lsw::agent::core
