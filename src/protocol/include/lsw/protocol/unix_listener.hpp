#pragma once

#include "lsw/protocol/listener.hpp"

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

    [[nodiscard]] std::unique_ptr<FrameChannel> accept() override;
    void close() noexcept override;
    [[nodiscard]] std::string description() const override;

    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    [[nodiscard]] bool is_listening() const noexcept { return listen_fd_ >= 0; }

private:
    std::string path_;
    int listen_fd_ = -1;
};

}  // namespace lsw::protocol
