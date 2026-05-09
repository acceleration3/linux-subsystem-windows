#pragma once

#include "lsw/protocol/listener.hpp"
#include "lsw/protocol/transport.hpp"

#include <string>

namespace lsw::protocol {

// Listener backed by an ``AF_UNIX`` ``SOCK_STREAM`` socket on the local
// filesystem. The socket file is unlinked on bind (to recover from a
// previous abnormal exit) and again on close.
class UnixListener final : public Listener {
public:
    UnixListener() noexcept = default;

    explicit UnixListener(std::string path) noexcept : path_(std::move(path)) {}

    ~UnixListener() override;

    UnixListener(const UnixListener&) = delete;
    UnixListener& operator=(const UnixListener&) = delete;
    UnixListener(UnixListener&&) = delete;
    UnixListener& operator=(UnixListener&&) = delete;

    // Bind+listen on ``path``. If ``path`` is empty, uses the value passed
    // to the constructor. Returns 0 on success, -errno on failure.
    [[nodiscard]] int bind();
    [[nodiscard]] int bind(const std::string& path);

    [[nodiscard]] AcceptedFd accept() override;
    void close() noexcept override;
    [[nodiscard]] std::string description() const override;

    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    [[nodiscard]] bool is_listening() const noexcept { return listen_fd_ >= 0; }

private:
    std::string path_;
    int listen_fd_ = -1;
};

// Client-side ``Transport`` over an ``AF_UNIX`` socket. Synchronous
// request/reply on a single fd: ``send_sync`` writes and blocks reading
// the reply. Reply tokens are monotonic per-instance; the simple in-order
// semantics is fine until Phase 5 splits the cmd/reply streams.
class UnixSocketTransport final : public Transport {
public:
    UnixSocketTransport() noexcept = default;
    explicit UnixSocketTransport(int fd) noexcept;
    ~UnixSocketTransport() override;

    UnixSocketTransport(const UnixSocketTransport&) = delete;
    UnixSocketTransport& operator=(const UnixSocketTransport&) = delete;
    UnixSocketTransport(UnixSocketTransport&& other) noexcept;
    UnixSocketTransport& operator=(UnixSocketTransport&& other) noexcept;

    // Connect to ``path``. Returns 0 on success, -errno on failure.
    [[nodiscard]] int connect(const std::string& path);

    [[nodiscard]] bool is_open() const noexcept;
    void close() noexcept;

    [[nodiscard]] TransportResult send_sync(std::uint32_t cmd_id,
                                            std::span<const std::uint8_t> payload,
                                            Buffer* reply_out) override;

    [[nodiscard]] TransportResult send_async(std::uint32_t cmd_id,
                                             std::span<const std::uint8_t> payload) override;

private:
    int fd_ = -1;
    std::uint64_t next_token_ = 1;
};

}  // namespace lsw::protocol
