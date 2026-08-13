// lsw-render-server entry point.
//
// Thin on purpose. Everything the server IS lives in ``RenderService``
// inside the library, because the manager has to be able to construct
// the same thing in its own process — a replay can only present into a
// window its own process owns, so hosting it is not optional for
// Phase H. This file is the standalone way to run it: parse a URL, open
// a listener, serve.
//
// Adding new transports: add a scheme to lsw::protocol::open_listener.
// Adding new handlers: add them to RenderService's chain.

#include "lsw/base/log.hpp"
#include "lsw/base/logger.hpp"
#include "lsw/protocol/endpoint.hpp"
#include "lsw/render/render_service.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <getopt.h>
#include <string>

namespace {

std::atomic<lsw::render::RenderService*> g_service{nullptr};

void handle_signal(int) noexcept {
    if (auto* s = g_service.load(std::memory_order_acquire); s != nullptr) {
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
                 "                  or shm:///lsw-vk-0 for shared memory\n"
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
    ::lsw::base::Logger::init("render");

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

    ::lsw::render::RenderService service;
    g_service.store(&service, std::memory_order_release);
    service.serve(*listener, opts.once);
    g_service.store(nullptr, std::memory_order_release);
    return 0;
}
