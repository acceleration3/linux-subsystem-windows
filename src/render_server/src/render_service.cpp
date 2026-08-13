#include "lsw/render/render_service.hpp"

#include "lsw/render/dispatch_chain.hpp"
#include "lsw/render/handlers/batch_handler.hpp"
#include "lsw/render/handlers/hello_handler.hpp"
#include "lsw/render/handlers/import_handler.hpp"
#include "lsw/render/handlers/memory_handler.hpp"
#include "lsw/render/handlers/surface_handler.hpp"
#include "lsw/render/handlers/vulkan_handler.hpp"

#include <memory>

namespace lsw::render {

RenderService::RenderService() : backend_(surfaces_) {}

void RenderService::serve(::lsw::protocol::Listener& listener, bool once) {
    // The chain, in the one place it is described.
    //
    // Order is not decorative. The batch handler is first because a
    // batch is an envelope around ordinary commands and has to be
    // opened before anything tries to interpret it; the Vulkan handler
    // is last because it is the catch-all that claims every generated
    // command id.
    auto build_chain = [&](Session& /*session*/, DispatchChain& chain) {
        chain.add(std::make_unique<handlers::BatchHandler>(chain));
        chain.add(std::make_unique<handlers::HelloHandler>());
        chain.add(std::make_unique<handlers::SurfaceHandler>(surfaces_));
        chain.add(std::make_unique<handlers::MemoryHandler>());
        // Region-backed allocations, when the peer shares memory with us
        // at all. Over a socket the listener has no region and every
        // import politely declines, which is the copy path.
        chain.add(std::make_unique<handlers::ImportHandler>(listener.backing_region(), backend_));
        chain.add(std::make_unique<handlers::VulkanHandler>(backend_));
    };

    Server server{listener, build_chain};
    server_.store(&server, std::memory_order_release);
    if (once) {
        server.serve_one();
    } else {
        server.serve_forever();
    }
    server_.store(nullptr, std::memory_order_release);
}

void RenderService::request_stop() noexcept {
    if (auto* s = server_.load(std::memory_order_acquire); s != nullptr) {
        s->request_stop();
    }
}

}  // namespace lsw::render
