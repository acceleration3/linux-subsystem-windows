#include "lsw/protocol/endpoint.hpp"

#include "lsw/protocol/channel_transport.hpp"
#include "lsw/protocol/endpoint_platform.hpp"

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

// ``unix:///tmp/foo`` gives path "/tmp/foo"; ``unix://foo`` gives an
// empty path and authority "foo". Both spellings are in use, and both
// mean the same filesystem path.
[[nodiscard]] std::string url_target(const ParsedUrl& p) {
    std::string target{p.path};
    if (target.empty()) {
        target.assign(p.authority);
    }
    return target;
}

}  // namespace

std::unique_ptr<Transport> open_transport(std::string_view url, std::string* error_out) {
    ParsedUrl p{};
    if (!parse_url(url, p)) {
        set_error(error_out, "endpoint URL must include scheme://");
        return nullptr;
    }
    const std::string target = url_target(p);
    if (target.empty()) {
        set_error(error_out, std::string{p.scheme} + ":// endpoint requires a target");
        return nullptr;
    }

    // Every scheme ends in the same place: a FrameChannel wrapped in a
    // ChannelTransport. That is B3 and B4 together — the difference
    // between running the proxy over a socket, over local shared memory
    // and over a guest's IVSHMEM BAR is which channel gets constructed,
    // and nothing above this function can tell.
    auto channel = platform_open_channel(p.scheme, target, error_out);
    if (channel == nullptr) {
        return nullptr;
    }
    auto t = std::make_unique<ChannelTransport>();
    if (const int rc = t->adopt(std::move(channel)); rc != 0) {
        set_error(error_out, "channel was not usable");
        return nullptr;
    }
    return t;
}

std::unique_ptr<Listener> open_listener(std::string_view url, std::string* error_out) {
    ParsedUrl p{};
    if (!parse_url(url, p)) {
        set_error(error_out, "endpoint URL must include scheme://");
        return nullptr;
    }
    const std::string target = url_target(p);
    if (target.empty()) {
        set_error(error_out, std::string{p.scheme} + ":// endpoint requires a target");
        return nullptr;
    }
    return platform_open_listener(p.scheme, target, error_out);
}

}  // namespace lsw::protocol
