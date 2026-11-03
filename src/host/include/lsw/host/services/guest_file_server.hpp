#pragma once

#include <QByteArray>

#include <atomic>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace lsw::host::services {
class GuestFileServer {
public:
    using Files = std::map<std::string, QByteArray, std::less<>>;

    struct Progress {
        std::string step;
        std::string status;
    };

    using ProgressSink = std::function<void(const Progress&)>;

    explicit GuestFileServer(Files files) noexcept;
    ~GuestFileServer();

    GuestFileServer(const GuestFileServer&) = delete;
    GuestFileServer& operator=(const GuestFileServer&) = delete;

    void on_progress(ProgressSink sink) { sink_ = std::move(sink); }

    [[nodiscard]] bool start();

    void stop();

    [[nodiscard]] unsigned short port() const noexcept { return port_; }

    [[nodiscard]] int downloads_served() const noexcept { return served_.load(); }

    [[nodiscard]] const std::string& error() const noexcept { return error_; }

    [[nodiscard]] static std::string_view requested_name(std::string_view request);

    [[nodiscard]] static std::optional<Progress> progress_of(std::string_view request);

private:
    void serve_loop();
    void answer(int connection, std::string_view request);

    Files files_;
    ProgressSink sink_;
    int listen_fd_ = -1;
    unsigned short port_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<int> served_{0};
    std::thread thread_;
    std::string error_;
};

}
