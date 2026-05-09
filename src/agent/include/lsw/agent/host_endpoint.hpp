#pragma once

#include <atomic>
#include <string>

namespace lsw::agent {

// HV_SOCKET listener bound to the LSW agent service GUID. Accepts one
// host (manager) connection at a time and handles tiny JSON-over-line
// commands. The render-server uses a separate service GUID so the two
// don't collide.
//
// Wire shape (one request per line, one reply per line):
//   {"cmd":"system_info"}
//   {"cmd":"list_apps"}
//   {"cmd":"launch","app_id":"..."}
//   {"cmd":"list_windows"}
class HostEndpoint {
public:
    HostEndpoint();
    ~HostEndpoint();

    HostEndpoint(const HostEndpoint&) = delete;
    HostEndpoint& operator=(const HostEndpoint&) = delete;

    // Blocks accepting connections until ``stop_flag`` is set. ``run``
    // is the only entry — fully self-contained, owns its socket fd.
    void run(std::atomic<bool>& stop_flag);

private:
    [[nodiscard]] bool open_listen();
    void handle_connection(intptr_t accepted_fd);
    [[nodiscard]] std::string handle_command(const std::string& json_line);

    intptr_t listen_fd_ = -1;  // SOCKET, but kept as intptr_t to avoid pulling winsock here
};

}  // namespace lsw::agent
