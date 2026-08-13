// A1 + B5: one transport, many threads, over every transport there is.
//
// ``ChannelTransport`` serves a whole ``VkInstance``, and every D3D
// translation layer runs a submission thread, so this is not a
// hypothetical shape. Matching a reply to its request by arrival order —
// what the socket transport used to do — fails here within a few hundred
// calls: usually as a reply delivered to the wrong caller, sometimes as
// a stream desynchronised badly enough that everybody waits forever.
//
// The same hammer runs over ``unix://`` and ``shm://``, through
// ``open_transport`` and ``open_listener``, so the two schemes are held
// to one test rather than to two that resemble each other. A divergence
// between them is a shared-memory bug with a socket-shaped control
// already in hand.
//
// The client half is ``hammer_transport``; only the server half is here,
// and it is deliberately the smallest thing that can echo: no dispatch
// chain, no Vulkan, nothing that could absorb a corrupt frame and let
// the run look healthy.

#include "lsw/protocol/endpoint.hpp"
#include "lsw/protocol/frame_channel.hpp"
#include "lsw/protocol/frame_flags.hpp"
#include "lsw/protocol/listener.hpp"
#include "transport_hammer.hpp"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

int g_failures = 0;

void check(bool ok, const std::string& label) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", label.c_str());
        ++g_failures;
    }
}

// Reply to every frame with the payload it carried. A frame that asked
// for no reply is counted and dropped.
class EchoServer {
public:
    [[nodiscard]] bool start(const std::string& url, std::string* error_out) {
        listener_ = lsw::protocol::open_listener(url, error_out);
        if (listener_ == nullptr) {
            return false;
        }
        thread_ = std::thread([this] { run(); });
        return true;
    }

    // Unblock the serving channel wherever it is parked. A socket peer
    // that closed gives the reader an EOF for free; a ring has no such
    // thing, so the reader has to be told.
    void stop() {
        {
            const std::lock_guard lk{mutex_};
            if (channel_ != nullptr) {
                channel_->shutdown_read();
            }
        }
        if (listener_ != nullptr) {
            listener_->close();
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    [[nodiscard]] std::uint64_t notes_seen() const { return notes_seen_.load(); }

private:
    void run() {
        using namespace lsw::protocol;
        auto channel = listener_->accept();
        if (channel == nullptr) {
            return;
        }
        {
            const std::lock_guard lk{mutex_};
            channel_ = channel.get();
        }
        while (true) {
            Frame f;
            const IoResult r = channel->read_frame(f);
            if (r == IoResult::Timeout) {
                continue;
            }
            if (r != IoResult::Ok) {
                break;
            }
            if ((f.flags & FrameFlags::needs_reply) == 0) {
                notes_seen_.fetch_add(1);
                continue;
            }
            if (channel->write_reply(f, {f.payload.data(), f.payload.size()}) != IoResult::Ok) {
                break;
            }
        }
        const std::lock_guard lk{mutex_};
        channel_ = nullptr;
    }

    std::unique_ptr<lsw::protocol::Listener> listener_;
    std::thread thread_;
    std::mutex mutex_;
    lsw::protocol::FrameChannel* channel_ = nullptr;
    std::atomic<std::uint64_t> notes_seen_{0};
};

void run_case(const std::string& scheme, const std::string& listen_url,
              const std::string& connect_url) {
    const auto label = [&](const char* what) { return scheme + ": " + what; };

    EchoServer server;
    std::string err;
    if (!server.start(listen_url, &err)) {
        check(false, label(("listener opened (" + err + ")").c_str()));
        return;
    }

    // ``open_transport`` is the thing under test as much as the
    // transport is: the only difference between running the proxy over a
    // socket and over shared memory is meant to be this URL.
    auto transport = lsw::protocol::open_transport(connect_url, &err);
    if (transport == nullptr) {
        check(false, label(("transport opened (" + err + ")").c_str()));
        server.stop();
        return;
    }

    lsw::protocol::test::HammerResult r;
    lsw::protocol::test::hammer_transport(*transport, r);
    transport.reset();
    server.stop();

    check(r.transport_errors.load() == 0, label("no transport errors"));
    // The interesting one: a nonzero count here means a caller was handed
    // a reply that belonged to a different thread's request.
    check(r.mismatched_replies.load() == 0, label("every reply matched its request"));
    check(r.short_replies.load() == 0, label("no truncated replies"));
    check(r.sync_calls.load() > 0, label("sync calls actually ran"));
    check(r.async_calls.load() > 0, label("async calls actually ran"));
    check(server.notes_seen() == r.async_calls.load(),
          label("every async frame reached the server"));

    std::printf("  %s: %llu sync, %llu async\n", scheme.c_str(),
                static_cast<unsigned long long>(r.sync_calls.load()),
                static_cast<unsigned long long>(r.async_calls.load()));
}

}  // namespace

int main() {
    const std::string tag = std::to_string(::getpid());

    const std::string sock_path =
        (std::filesystem::temp_directory_path() / ("lsw-hammer-" + tag + ".sock")).string();
    run_case("unix", "unix://" + sock_path, "unix://" + sock_path);

    // Same code, same assertions, different mapper. ``shm:///name`` names
    // a POSIX shm region the client finds by name alone.
    const std::string shm_name = "/lsw-hammer-" + tag;
    run_case("shm", "shm://" + shm_name, "shm://" + shm_name);

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("OK");
    return 0;
}
