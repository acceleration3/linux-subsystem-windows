#pragma once

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class FakeAgent {
public:
    using Handler = std::function<nlohmann::json(const nlohmann::json& params)>;

    FakeAgent() {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        const int one = 1;
        ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = 0;
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        ::bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ::listen(listener_, 4);
        socklen_t len = sizeof(addr);
        ::getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread{[this] { serve(); }};
    }

    ~FakeAgent() {
        stop_.store(true);
        thread_.join();
        ::close(listener_);
    }

    FakeAgent(const FakeAgent&) = delete;
    FakeAgent& operator=(const FakeAgent&) = delete;

    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

    void on(const std::string& cmd, Handler handler) {
        const std::lock_guard lock{mutex_};
        handlers_[cmd] = std::move(handler);
    }

    [[nodiscard]] std::vector<std::pair<std::string, nlohmann::json>> requests() const {
        const std::lock_guard lock{mutex_};
        return requests_;
    }

    [[nodiscard]] int count(const std::string& cmd) const {
        int n = 0;
        for (const auto& request : requests()) {
            n += request.first == cmd ? 1 : 0;
        }
        return n;
    }

private:
    static bool read_all(int fd, void* out, std::size_t n, const std::atomic<bool>& stop) {
        auto* p = static_cast<std::uint8_t*>(out);
        while (n > 0) {
            pollfd pfd{fd, POLLIN, 0};
            if (::poll(&pfd, 1, 50) <= 0) {
                if (stop.load()) {
                    return false;
                }
                continue;
            }
            const ssize_t got = ::read(fd, p, n);
            if (got <= 0) {
                return false;
            }
            p += got;
            n -= static_cast<std::size_t>(got);
        }
        return true;
    }

    void serve() {
        while (!stop_.load()) {
            pollfd pfd{listener_, POLLIN, 0};
            if (::poll(&pfd, 1, 50) <= 0) {
                continue;
            }
            const int fd = ::accept(listener_, nullptr, nullptr);
            if (fd < 0) {
                continue;
            }
            talk(fd);
            ::close(fd);
        }
    }

    void talk(int fd) {
        for (;;) {
            std::uint8_t header[5];
            if (!read_all(fd, header, sizeof(header), stop_)) {
                return;
            }
            const std::uint32_t len = (static_cast<std::uint32_t>(header[1]) << 24) |
                                      (static_cast<std::uint32_t>(header[2]) << 16) |
                                      (static_cast<std::uint32_t>(header[3]) << 8) | header[4];
            std::string body(len, '\0');
            if (!read_all(fd, body.data(), len, stop_)) {
                return;
            }
            const auto request = nlohmann::json::parse(body, nullptr, false);
            if (!request.is_object()) {
                continue;
            }
            const std::string cmd = request.value("cmd", std::string{});
            const nlohmann::json params = request.contains("params") ? request["params"]
                                                                     : nlohmann::json::object();
            Handler handler;
            {
                const std::lock_guard lock{mutex_};
                requests_.emplace_back(cmd, params);
                const auto it = handlers_.find(cmd);
                if (it != handlers_.end()) {
                    handler = it->second;
                }
            }
            nlohmann::json reply = {{"id", request["id"]}};
            if (handler) {
                reply["result"] = handler(params);
            } else {
                reply["error"] = "unknown command: " + cmd;
            }
            const std::string out = reply.dump();
            std::string frame(5, '\0');
            frame[0] = 1;
            frame[1] = static_cast<char>(out.size() >> 24);
            frame[2] = static_cast<char>(out.size() >> 16);
            frame[3] = static_cast<char>(out.size() >> 8);
            frame[4] = static_cast<char>(out.size());
            frame += out;
            if (::write(fd, frame.data(), frame.size()) < 0) {
                return;
            }
        }
    }

    int listener_ = -1;
    std::uint16_t port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    mutable std::mutex mutex_;
    std::map<std::string, Handler> handlers_;
    std::vector<std::pair<std::string, nlohmann::json>> requests_;
};
