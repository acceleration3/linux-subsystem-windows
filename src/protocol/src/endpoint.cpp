#include "lsw/protocol/endpoint.hpp"

#include "lsw/protocol/unix_endpoint.hpp"

#include <utility>

namespace lsw::protocol {

namespace {

struct ParsedUrl {
    std::string_view scheme;
    std::string_view authority;  // between "://" and the first '/' (or end)
    std::string_view path;       // the part starting at '/'
};

[[nodiscard]] bool parse_url(std::string_view url, ParsedUrl& out) noexcept {
    const auto sep = url.find("://");
    if (sep == std::string_view::npos) {
        return false;
    }
    out.scheme = url.substr(0, sep);
    const auto rest = url.substr(sep + 3);
    const auto slash = rest.find('/');
    if (slash == std::string_view::npos) {
        out.authority = rest;
        out.path = {};
    } else {
        out.authority = rest.substr(0, slash);
        out.path = rest.substr(slash);
    }
    return true;
}

void set_error(std::string* error_out, std::string msg) {
    if (error_out != nullptr) {
        *error_out = std::move(msg);
    }
}

}  // namespace

std::unique_ptr<Transport> open_transport(std::string_view url, std::string* error_out) {
    ParsedUrl p{};
    if (!parse_url(url, p)) {
        set_error(error_out, "endpoint URL must include scheme://");
        return nullptr;
    }
    if (p.scheme == "unix") {
        // For unix:///tmp/foo, ``path`` is "/tmp/foo"; for unix://foo it's empty.
        std::string socket_path{p.path};
        if (socket_path.empty()) {
            socket_path.assign(p.authority);
        }
        if (socket_path.empty()) {
            set_error(error_out, "unix:// endpoint requires a path");
            return nullptr;
        }
        auto t = std::make_unique<UnixSocketTransport>();
        if (const int rc = t->connect(socket_path); rc != 0) {
            set_error(error_out, "connect to " + socket_path + " failed");
            return nullptr;
        }
        return t;
    }
    set_error(error_out, "unsupported transport scheme: " + std::string{p.scheme});
    return nullptr;
}

std::unique_ptr<Listener> open_listener(std::string_view url, std::string* error_out) {
    ParsedUrl p{};
    if (!parse_url(url, p)) {
        set_error(error_out, "endpoint URL must include scheme://");
        return nullptr;
    }
    if (p.scheme == "unix") {
        std::string socket_path{p.path};
        if (socket_path.empty()) {
            socket_path.assign(p.authority);
        }
        if (socket_path.empty()) {
            set_error(error_out, "unix:// endpoint requires a path");
            return nullptr;
        }
        auto l = std::make_unique<UnixListener>();
        if (const int rc = l->bind(socket_path); rc != 0) {
            set_error(error_out, "bind to " + socket_path + " failed");
            return nullptr;
        }
        return l;
    }
    set_error(error_out, "unsupported listener scheme: " + std::string{p.scheme});
    return nullptr;
}

}  // namespace lsw::protocol
