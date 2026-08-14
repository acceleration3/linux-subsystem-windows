#pragma once

#include "lsw/host/manager/guest_input_sender.hpp"

#include <QSize>
#include <QWidget>

#include <cstdint>
#include <optional>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::manager {

// A window the Vulkan replay presents into, and the guest application's
// input surface.
//
// The WGC mirror (``WindowView``) receives captured frames and blits
// them; this one receives nothing. Its pixels are put there by the
// replay running in this process, which created a Vulkan surface on
// this widget's native window — so ``paintEvent`` deliberately draws
// nothing, and Qt is told not to paint a background either. Anything
// this class painted would be overwritten by the GPU at best, and would
// flicker over the application at worst.
//
// What it does own is input. A guest application's window is inside the
// VM and cannot be clicked; the mirror is the thing on the user's
// desktop, so it forwards keyboard and mouse to the guest window over
// the agent's input RPC — exactly as the WGC mirror does, through the
// same ``GuestInputSender``.
//
// It is created BEFORE the application launches, because the surface is
// what it exists to provide: the guest asks for one within a moment of
// starting, and a window that appears afterwards is too late. That is
// also why it starts unbound — the guest window it forwards input to
// does not exist yet either — and gets its HWND from the window watcher
// once one shows up.
class VulkanMirror : public QWidget {
    Q_OBJECT
public:
    VulkanMirror(link::AgentRpcClient& rpc, const QString& title, QSize size,
                 QWidget* parent = nullptr);

    // The guest window this mirror drives. Until this is called input
    // has nowhere to go and is dropped.
    void bind(std::uint64_t hwnd);

    [[nodiscard]] bool is_bound() const noexcept { return input_.has_value(); }
    [[nodiscard]] std::uint64_t hwnd() const noexcept { return hwnd_; }

    // The guest window went away: stop forwarding and close.
    void guest_closed();

Q_SIGNALS:
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    // Widget position -> guest client pixel.
    //
    // The mirror is the size of the guest's client area and the replay
    // presents at that size, so the mapping is 1:1 until the user
    // resizes the window — which is not offered yet, for the same
    // reason the replay's own window was never resizable: the
    // application sizes its swapchain from its own window, which did
    // not change.
    [[nodiscard]] QPoint to_guest(QPointF widget_position) const;

    link::AgentRpcClient& rpc_;
    std::uint64_t hwnd_ = 0;
    std::optional<GuestInputSender> input_;
    bool guest_gone_ = false;
};

}  // namespace lsw::host::manager
