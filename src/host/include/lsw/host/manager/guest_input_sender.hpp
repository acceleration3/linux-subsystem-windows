#pragma once

#include <QPoint>
#include <QString>

#include <cstdint>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::manager {

// Forwards host input events to one guest window.
//
// Everything here is fire-and-forget: input is high-frequency and
// loss-tolerant, and a blocking round trip per mouse-move would stall
// the GUI thread. Coordinates are guest client pixels — the caller has
// already mapped them through FrameGeometry.
class GuestInputSender {
public:
    GuestInputSender(link::AgentRpcClient& rpc, std::uint64_t hwnd) noexcept
        : rpc_(rpc), hwnd_(hwnd) {}

    void move(QPoint guest, unsigned mods);
    void button_down(QPoint guest, int button, unsigned mods);
    void button_up(QPoint guest, int button, unsigned mods);
    void double_click(QPoint guest, int button, unsigned mods);
    void wheel(QPoint guest, int delta, bool horizontal, unsigned mods);
    void key_down(unsigned virtual_key, unsigned mods);
    void key_up(unsigned virtual_key, unsigned mods);
    void text(unsigned code_point, unsigned mods);

private:
    link::AgentRpcClient& rpc_;
    std::uint64_t hwnd_;
};

}  // namespace lsw::host::manager
