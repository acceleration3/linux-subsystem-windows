#pragma once

#include "lsw/server/handle_table.hpp"

#include <cstdint>
#include <string>

namespace lsw::server {

// Per-connection state shared by handlers in a DispatchChain. Each accepted
// client gets its own Session; lifetime is the connection's lifetime.
//
// Owning the Session out here (instead of pinning everything inside the
// generated ``ServerDispatch`` impl) keeps the per-connection bookkeeping
// — handshake details, surface remap table, future caps negotiation — out
// of the generated dispatch class.
class Session {
public:
    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;
    ~Session() = default;

    // Identity & handshake.
    [[nodiscard]] std::uint64_t context_id() const noexcept { return context_id_; }

    void set_context_id(std::uint64_t v) noexcept { context_id_ = v; }

    [[nodiscard]] const std::string& app_name() const noexcept { return app_name_; }

    void set_app_name(std::string v) noexcept { app_name_ = std::move(v); }

    [[nodiscard]] std::uint32_t client_pid() const noexcept { return client_pid_; }

    void set_client_pid(std::uint32_t v) noexcept { client_pid_ = v; }

    [[nodiscard]] bool handshake_done() const noexcept { return handshake_done_; }

    void mark_handshake_done() noexcept { handshake_done_ = true; }

    // Remap tables for synthetic handles (surfaces today; externals later).
    [[nodiscard]] HandleTable& handles() noexcept { return handles_; }

    [[nodiscard]] const HandleTable& handles() const noexcept { return handles_; }

private:
    std::uint64_t context_id_ = 0;
    std::string app_name_;
    std::uint32_t client_pid_ = 0;
    bool handshake_done_ = false;
    HandleTable handles_;
};

}  // namespace lsw::server
