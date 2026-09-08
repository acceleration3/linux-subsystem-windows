#pragma once

#include <cstdint>
#include <string>

namespace lsw::agent::win {

// Starts remotewin's guest half beside an application being mirrored.
//
// Its own spawn rather than the one applications go through: that path
// puts a process in a Job Object, stages graphics drivers into its
// directory and starts it suspended, all of which exist for a user's
// application and none of which this is. This is the guest side of the
// manager's own tooling, started the way the agent itself was.
//
// One process per mirrored application: it dials out to the port the
// manager's mirror process is listening on and ends when that ends.
class MirrorProcess {
public:
    MirrorProcess() = delete;

    // ``exe`` is the guest path of remotewin-guest.exe, ``port`` the
    // host port to dial. ``log_path`` is where the mirror writes its own
    // log: a process started with no console has nowhere else to say
    // what went wrong. Returns the process id, or 0 with ``error_out``
    // set.
    [[nodiscard]] static unsigned long start(const std::string& exe, std::uint16_t port,
                                             const std::string& log_path,
                                             std::string* error_out);
};

}  // namespace lsw::agent::win
