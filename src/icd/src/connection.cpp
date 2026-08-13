#include "lsw/icd/connection.hpp"

#include "lsw/icd/handshake.hpp"
#include "lsw/base/log.hpp"
#include "lsw/base/logger.hpp"
#include "lsw/protocol/channel_transport.hpp"
#include "lsw/protocol/endpoint.hpp"
#include "lsw/protocol/protocol_constants.hpp"

#include "lsw/linkproto/region.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace lsw::icd {

namespace {

// The application's own name, for the HELLO and for the host window's
// title bar.
//
// This is the one thing in the ICD that genuinely has no portable
// spelling, and the only place the source differs by target. Note what
// it is NOT: a difference between "local" and "in a VM". The same
// binary asks the same question in the same place either way — a
// Windows build asks Windows and a Linux build asks Linux, and the
// answer means the same thing to everything above.
[[nodiscard]] std::string self_process_name() {
#if defined(_WIN32)
    std::array<char, MAX_PATH> buf{};
    const DWORD n = ::GetModuleFileNameA(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0) {
        return "lsw-client";
    }
    std::string path(buf.data(), n);
    // Basename, and without the .exe — so the window is titled
    // "notepad", not "C:\Windows\notepad.exe".
    if (const auto slash = path.find_last_of("\\/"); slash != std::string::npos) {
        path.erase(0, slash + 1);
    }
    if (path.size() > 4
        && (path.compare(path.size() - 4, 4, ".exe") == 0
            || path.compare(path.size() - 4, 4, ".EXE") == 0)) {
        path.erase(path.size() - 4);
    }
    return path.empty() ? "lsw-client" : path;
#else
    std::array<char, 256> buf{};
    if (FILE* f = std::fopen("/proc/self/comm", "r"); f != nullptr) {
        const size_t n = std::fread(buf.data(), 1, buf.size() - 1, f);
        std::fclose(f);
        std::string s(buf.data(), n);
        if (!s.empty() && s.back() == '\n') {
            s.pop_back();
        }
        return s.empty() ? "lsw-client" : s;
    }
    return "lsw-client";
#endif
}

[[nodiscard]] std::uint32_t self_process_id() {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(::GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
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

Connection::~Connection() {
    if (transport_ == nullptr) {
        return;
    }
    // Say goodbye explicitly. On a socket this is redundant — closing
    // the fd gives the server an EOF — but shared memory has no EOF at
    // all: a peer that simply stops writing is indistinguishable from
    // one that is thinking, and the server would poll its ring forever.
    //
    // So the disconnect becomes a message rather than a property of the
    // transport, which is the only version of it that means the same
    // thing on both.
    static_cast<void>(transport_->send_async(::lsw::protocol::kCmdBye, {}));
}

int Connection::open(std::string_view endpoint, std::string_view app_name) {
    ::lsw::base::Logger::init("icd");

    std::string err;
    auto t = ::lsw::protocol::open_transport(endpoint, &err);
    if (t == nullptr) {
        LSW_ERROR("open_transport({}) failed: {}", endpoint, err);
        return -1;
    }
    ClientHello hello{};
    hello.app_name.assign(app_name);
    hello.pid = self_process_id();

    ServerHelloAck ack{};
    if (const int rc = initiate_handshake(*t, hello, &ack); rc != kHandshakeOk) {
        LSW_ERROR("handshake with {} failed: {}", endpoint, rc);
        return rc;
    }

    transport_ = std::move(t);
    context_id_ = ack.context_id;
    app_name_.assign(app_name);

    // If the transport shares a region with the peer, take the hostmem
    // arena out of it. A socket answers null here and the arena stays
    // unbound, which is exactly how the copy path is selected — by the
    // absence of shared memory, not by a flag.
    if (auto* ct = dynamic_cast<::lsw::protocol::ChannelTransport*>(transport_.get());
        ct != nullptr) {
        if (auto region = ct->backing_region(); region != nullptr && region->valid()) {
            const auto view =
                ::lsw::linkproto::find_channel(region->base(), ::lsw::linkproto::kChannelHostMem);
            if (view.valid() && hostmem_window_.bind(*region, view.offset, view.size)) {
                region_ = std::move(region);
                arena_.bind(hostmem_window_);
                LSW_INFO("host-visible allocations can be shared: {} MiB arena at region "
                         "offset 0x{:x}",
                         view.size / (1024 * 1024), view.offset);
            }
        }
    }

    LSW_INFO("connected to {} (context_id={}, app={})", endpoint, context_id_, app_name_);
    return 0;
}

int Connection::open_default() {
    return open(default_endpoint_url(), self_process_name());
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
