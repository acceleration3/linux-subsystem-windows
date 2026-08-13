#include "lsw/host/manager/guest_input_sender.hpp"

#include "lsw/host/link/agent_rpc_client.hpp"

#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::manager {

namespace {

// One inject_input frame. The hwnd is stamped here so no caller has to
// remember it.
void post(link::AgentRpcClient& rpc, std::uint64_t hwnd, nlohmann::json params) {
    params["hwnd"] = hwnd;
    rpc.post_oneway({{"cmd", linkproto::cmd::kInjectInput}, {"params", std::move(params)}});
}

}  // namespace

void GuestInputSender::move(QPoint guest, unsigned mods) {
    post(rpc_, hwnd_, {{"type", "move"}, {"x", guest.x()}, {"y", guest.y()},
                       {"mods", mods}});
}

void GuestInputSender::button_down(QPoint guest, int button, unsigned mods) {
    post(rpc_, hwnd_, {{"type", "down"}, {"x", guest.x()}, {"y", guest.y()},
                       {"button", button}, {"mods", mods}});
}

void GuestInputSender::button_up(QPoint guest, int button, unsigned mods) {
    post(rpc_, hwnd_, {{"type", "up"}, {"x", guest.x()}, {"y", guest.y()},
                       {"button", button}, {"mods", mods}});
}

void GuestInputSender::double_click(QPoint guest, int button, unsigned mods) {
    post(rpc_, hwnd_, {{"type", "dblclick"}, {"x", guest.x()}, {"y", guest.y()},
                       {"button", button}, {"mods", mods}});
}

void GuestInputSender::wheel(QPoint guest, int delta, bool horizontal, unsigned mods) {
    post(rpc_, hwnd_, {{"type", "wheel"}, {"x", guest.x()}, {"y", guest.y()},
                       {"wheel", delta}, {"hwheel", horizontal}, {"mods", mods}});
}

void GuestInputSender::key_down(unsigned virtual_key, unsigned mods) {
    post(rpc_, hwnd_, {{"type", "key_down"}, {"vk", virtual_key}, {"mods", mods}});
}

void GuestInputSender::key_up(unsigned virtual_key, unsigned mods) {
    post(rpc_, hwnd_, {{"type", "key_up"}, {"vk", virtual_key}, {"mods", mods}});
}

void GuestInputSender::text(unsigned code_point, unsigned mods) {
    post(rpc_, hwnd_, {{"type", "char"}, {"cp", code_point}, {"mods", mods}});
}

}  // namespace lsw::host::manager
