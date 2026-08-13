#include "lsw/render/dispatch_chain.hpp"

#include "lsw/base/log.hpp"
#include "vk_command_names.h"

namespace lsw::render {

DispatchChain& DispatchChain::add(std::unique_ptr<Handler> h) {
    handlers_.push_back(std::move(h));
    return *this;
}

bool DispatchChain::dispatch(std::uint32_t cmd_id, std::span<const std::uint8_t> payload,
                             ::lsw::protocol::Buffer& reply) {
    for (auto& h : handlers_) {
        switch (h->handle(session_, cmd_id, payload, reply)) {
        case HandleResult::Handled:
            return true;
        case HandleResult::Pass:
            continue;
        case HandleResult::ProtocolError:
            LSW_ERROR("PROTOCOL_ERROR cmd_id=0x{:08x} ({})", cmd_id,
                      ::lsw::vkproto::cmd_id_to_string(cmd_id));
            return false;
        }
    }
    LSW_ERROR("UNKNOWN cmd_id=0x{:08x} ({}) (no handler claimed)", cmd_id,
              ::lsw::vkproto::cmd_id_to_string(cmd_id));
    return false;  // unknown cmd_id
}

}  // namespace lsw::render
