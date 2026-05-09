#include "lsw/icd/connection.hpp"

#include "lsw/icd/handshake.hpp"
#include "lsw/log.hpp"
#include "lsw/protocol/endpoint.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace lsw::icd {

namespace {

[[nodiscard]] std::string read_proc_self_comm() {
    char buf[256] = {};
    if (FILE* f = std::fopen("/proc/self/comm", "r"); f != nullptr) {
        const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
        std::fclose(f);
        std::string s(buf, n);
        if (!s.empty() && s.back() == '\n') {
            s.pop_back();
        }
        return s;
    }
    return "lsw-client";
}

[[nodiscard]] std::string default_endpoint_url() {
    const char* env = std::getenv("LSW_SERVER_SOCKET");
    if (env == nullptr || *env == '\0') {
        return std::string{::lsw::protocol::kDefaultEndpoint};
    }
    // Backwards-compat: if LSW_SERVER_SOCKET looks like a bare filesystem
    // path (no scheme://), wrap it in a unix:// URL.
    std::string s{env};
    if (s.find("://") == std::string::npos) {
        return "unix://" + s;
    }
    return s;
}

}  // namespace

Connection::~Connection() = default;

int Connection::open(std::string_view endpoint, std::string_view app_name) {
    ::lsw::log::init("icd");

    std::string err;
    auto t = ::lsw::protocol::open_transport(endpoint, &err);
    if (t == nullptr) {
        LSW_ERROR("open_transport({}) failed: {}", endpoint, err);
        return -1;
    }
    ClientHello hello{};
    hello.app_name.assign(app_name);
    hello.pid = static_cast<std::uint32_t>(::getpid());

    ServerHelloAck ack{};
    if (const int rc = initiate_handshake(*t, hello, &ack); rc != kHandshakeOk) {
        LSW_ERROR("handshake with {} failed: {}", endpoint, rc);
        return rc;
    }

    transport_ = std::move(t);
    context_id_ = ack.context_id;
    app_name_.assign(app_name);
    LSW_INFO("connected to {} (context_id={}, app={})", endpoint, context_id_, app_name_);
    return 0;
}

int Connection::open_default() {
    return open(default_endpoint_url(), read_proc_self_comm());
}

Connection* acquire_default_connection() {
    auto c = std::make_unique<Connection>();
    if (c->open_default() != 0) {
        return nullptr;
    }
    return c.release();
}

void release_connection(Connection* c) noexcept {
    delete c;
}

}  // namespace lsw::icd
