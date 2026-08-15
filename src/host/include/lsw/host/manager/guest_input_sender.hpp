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
    // ``client_space`` describes the coordinates the caller will pass.
    //
    // A mirror shows a captured frame, which covers the window's whole
    // extended frame — title bar included — so its coordinates are
    // frame-relative, and that is the default. A window presented by
    // the Vulkan replay shows the swapchain alone, so its coordinates
    // are client-relative; sending those as frame-relative puts every
    // click low and right by the title bar's height.
    GuestInputSender(link::AgentRpcClient& rpc, std::uint64_t hwnd,
                     bool client_space = false) noexcept
        : rpc_(rpc), hwnd_(hwnd), client_space_(client_space) {}

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
    bool client_space_ = false;
};

}  // namespace lsw::host::manager
