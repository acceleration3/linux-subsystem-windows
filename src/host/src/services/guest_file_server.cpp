#include "lsw/host/services/guest_file_server.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace lsw::host::services {

namespace {

void send_all(int connection, const char* bytes, std::size_t size) {
    while (size > 0) {
        const ssize_t written = ::send(connection, bytes, size, MSG_NOSIGNAL);
        if (written <= 0) {
            return;
        }
        bytes += written;
        size -= static_cast<std::size_t>(written);
    }
}

}  // namespace

GuestFileServer::GuestFileServer(Files files) noexcept : files_(std::move(files)) {}

GuestFileServer::~GuestFileServer() { stop(); }

std::string_view GuestFileServer::requested_name(std::string_view request) {
    // "GET /name HTTP/1.1"
    const auto method_end = request.find(' ');
    if (method_end == std::string_view::npos) {
        return {};
    }
    const auto path_start = method_end + 1;
    const auto path_end = request.find_first_of(" \r\n", path_start);
    if (path_end == std::string_view::npos) {
        return {};
    }
    std::string_view path = request.substr(path_start, path_end - path_start);
    if (const auto query = path.find('?'); query != std::string_view::npos) {
        path = path.substr(0, query);
    }
    if (path.empty() || path.front() != '/') {
        return {};
    }
    return path.substr(1);
}

bool GuestFileServer::start() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    if (listen_fd_ < 0) {
        error_ = std::string{"socket: "} + std::strerror(errno);
        return false;
    }
    int one = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // kernel picks
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        error_ = std::string{"bind: "} + std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    socklen_t alen = sizeof(addr);
    if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &alen) != 0
        || ::listen(listen_fd_, 4) != 0) {
        error_ = "listen failed";
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    port_ = ntohs(addr.sin_port);
    thread_ = std::thread([this] { serve_loop(); });
    return true;
}

void GuestFileServer::serve_loop() {
    while (!stop_.load()) {
        sockaddr_in peer{};
        socklen_t plen = sizeof(peer);
        const int conn = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &plen);
        if (conn < 0) {
            if (stop_.load()) {
                return;
            }
            continue;
        }
        // Read the request head, bounded so a peer that never finishes
        // cannot pin the loop.
        std::string request;
        request.reserve(512);
        char buf[4096];
        for (int i = 0; i < 8; ++i) {
            timeval tv{0, 100 * 1000};  // 100 ms per attempt
            fd_set rd;
            FD_ZERO(&rd);
            FD_SET(conn, &rd);
            if (::select(conn + 1, &rd, nullptr, nullptr, &tv) <= 0) {
                break;
            }
            const ssize_t n = ::recv(conn, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
            request.append(buf, static_cast<std::size_t>(n));
            if (request.find("\r\n\r\n") != std::string::npos) {
                break;
            }
        }
        answer(conn, request);
        ::close(conn);
    }
}

void GuestFileServer::answer(int connection, std::string_view request) {
    const auto file = files_.find(requested_name(request));
    if (file == files_.end()) {
        constexpr std::string_view kNotFound =
            "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send_all(connection, kNotFound.data(), kNotFound.size());
        return;
    }
    const QByteArray& payload = file->second;
    const std::string header = std::string{"HTTP/1.1 200 OK\r\n"}
                               + "Content-Type: application/octet-stream\r\n"
                               + "Content-Length: " + std::to_string(payload.size()) + "\r\n"
                               + "Connection: close\r\n\r\n";
    send_all(connection, header.data(), header.size());
    send_all(connection, payload.constData(), static_cast<std::size_t>(payload.size()));
    ++served_;
}

void GuestFileServer::stop() {
    if (listen_fd_ < 0) {
        return;
    }
    stop_.store(true);
    // Shut the listener down so the blocked accept returns rather than
    // waiting for a connection that will never come.
    ::shutdown(listen_fd_, SHUT_RDWR);
    ::close(listen_fd_);
    listen_fd_ = -1;
    if (thread_.joinable()) {
        thread_.join();
    }
}

}  // namespace lsw::host::services
