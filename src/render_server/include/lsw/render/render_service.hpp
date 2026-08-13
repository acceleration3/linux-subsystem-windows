#pragma once

#include "lsw/protocol/listener.hpp"
#include "lsw/render/dispatch/translating_dispatch.hpp"
#include "lsw/render/server.hpp"
#include "lsw/render/surface_manager.hpp"

#include <memory>

namespace lsw::render {

// Everything the render server is, minus the process it happens to run
// in.
//
// It used to be assembled in ``main.cpp``: a SurfaceManager, a
// TranslatingDispatch, a chain builder listing five handlers, a Server.
// That was fine while the only way to run it was as its own binary —
// and it is exactly what stood in the way of Phase H, which needs the
// manager to host the same thing IN-PROCESS.
//
// In-process is not a preference. The render server must own the window
// it presents to: presenting into a window belonging to another process
// succeeds at every Vulkan call and paints nothing at all, which is a
// lesson this project has already paid for once. So a mirror can only
// hand its ``winId()`` to a replay running in the manager's own
// process, and that requires the composition root to be constructible
// from somewhere other than ``main``.
//
// The standalone binary now builds one of these and runs it. The
// manager will build one and run it on a thread.
class RenderService {
public:
    RenderService();

    RenderService(const RenderService&) = delete;
    RenderService& operator=(const RenderService&) = delete;
    RenderService(RenderService&&) = delete;
    RenderService& operator=(RenderService&&) = delete;
    ~RenderService() = default;

    // Serve on ``listener`` until stopped. ``once`` takes exactly one
    // connection, which is what the loopback harness wants.
    void serve(::lsw::protocol::Listener& listener, bool once = false);

    // Asynchronously stop ``serve``. Safe from any thread, including a
    // signal handler.
    void request_stop() noexcept;

    // The surfaces this service has minted. Phase H hands this a
    // mirror's native window id instead of letting it create one.
    [[nodiscard]] SurfaceManager& surfaces() noexcept { return surfaces_; }

private:
    SurfaceManager surfaces_;
    dispatch::TranslatingDispatch backend_;
    // Set while ``serve`` is running, so ``request_stop`` can reach it.
    std::atomic<Server*> server_{nullptr};
};

}  // namespace lsw::render
