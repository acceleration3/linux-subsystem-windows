// lsw-render-server entry point.
//
// Composition root: pick a Listener (Unix socket today), pick a Vulkan
// backend (real / stub), and wire a DispatchChain that begins with
// HELLO and falls through to the Vulkan handler.
//
// Adding new transports: add a scheme to lsw::protocol::open_listener.
// Adding new backends: add a flag here and a concrete ServerDispatch.

#include "lsw/log.hpp"
#include "lsw/protocol/endpoint.hpp"
#include "lsw/server/dispatch/translating_dispatch.hpp"
#include "lsw/server/handler.hpp"
#include "lsw/server/handlers/hello_handler.hpp"
#include "lsw/server/handlers/memory_handler.hpp"
#include "lsw/server/handlers/surface_handler.hpp"
#include "lsw/server/handlers/vulkan_handler.hpp"
#include "lsw/server/server.hpp"
#include "lsw/server/surface_manager.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <getopt.h>
#include <memory>
#include <string>

namespace {

std::atomic<lsw::server::Server*> g_server{nullptr};

void handle_signal(int) noexcept {
    if (auto* s = g_server.load(std::memory_order_acquire); s != nullptr) {
        s->request_stop();
    }
}

struct Options {
    std::string endpoint = std::string{::lsw::protocol::kDefaultEndpoint};
    bool once = false;
};

void print_usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s [--endpoint URL] [--once]\n"
                 "  --endpoint URL  listen on URL, e.g. unix:///tmp/lsw.sock (default)\n"
                 "  --once          accept exactly one connection then exit\n"
                 "  --help          show this message\n",
                 argv0);
}

[[nodiscard]] int parse_args(int argc, char** argv, Options* out) {
    static const option opts[] = {
        {"endpoint", required_argument, nullptr, 'e'},
        {"once", no_argument, nullptr, 'o'},
        {"help", no_argument, nullptr, 'h'},
        // Backwards-compat: --socket PATH expanded into --endpoint unix://PATH.
        {"socket", required_argument, nullptr, 's'},
        {nullptr, 0, nullptr, 0},
    };
    int c = 0;
    while ((c = ::getopt_long(argc, argv, "e:os:h", opts, nullptr)) != -1) {
        switch (c) {
        case 'e':
            out->endpoint = optarg;
            break;
        case 's':
            out->endpoint = std::string{"unix://"} + optarg;
            break;
        case 'o':
            out->once = true;
            break;
        case 'h':
            print_usage(argv[0]);
            return 1;
        default:
            print_usage(argv[0]);
            return -1;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    ::lsw::log::init("server");

    Options opts;
    if (const int rc = parse_args(argc, argv, &opts); rc != 0) {
        return rc < 0 ? 2 : 0;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

    std::string err;
    auto listener = ::lsw::protocol::open_listener(opts.endpoint, &err);
    if (listener == nullptr) {
        LSW_ERROR("open {} failed: {}", opts.endpoint, err);
        return 1;
    }
    LSW_INFO("listening on {}", listener->description());

    ::lsw::server::SurfaceManager surface_mgr;
    ::lsw::server::dispatch::TranslatingDispatch dispatch_backend{surface_mgr};

    auto build_chain = [&](::lsw::server::Session& /*s*/, ::lsw::server::DispatchChain& chain) {
        chain.add(std::make_unique<::lsw::server::handlers::HelloHandler>());
        chain.add(std::make_unique<::lsw::server::handlers::SurfaceHandler>(surface_mgr));
        chain.add(std::make_unique<::lsw::server::handlers::MemoryHandler>());
        chain.add(std::make_unique<::lsw::server::handlers::VulkanHandler>(dispatch_backend));
    };

    ::lsw::server::Server server{*listener, build_chain};
    g_server.store(&server, std::memory_order_release);

    if (opts.once) {
        server.serve_one();
    } else {
        server.serve_forever();
    }

    g_server.store(nullptr, std::memory_order_release);
    return 0;
}
