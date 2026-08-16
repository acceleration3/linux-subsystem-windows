#pragma once

#include "lsw/host/manager/geometry_sync.hpp"
#include "lsw/host/manager/guest_input_sender.hpp"
#include "lsw/host/manager/guest_window_props.hpp"
#include "lsw/host/manager/mirror_show_gate.hpp"
#include "lsw/host/manager/nc_hit_debug_overlay.hpp"
#include "lsw/host/manager/shmem_presenter.hpp"

#include <QByteArray>
#include <QImage>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <cstdint>
#include <optional>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::manager {

// A host window mirroring one guest HWND.
//
// Its concern is being that window: routing Qt events to guest input and
// host window operations to guest ones. Everything with a life of its
// own is a collaborator, because each of them is testable alone and a
// QWidget is not:
//
//   Presenter          where guest pixels come from and how they draw
//   GeometrySync       size agreement between mirror and guest window
//   MirrorShowGate     when a mirror may appear, and its desktop identity
//   GuestInputSender   the wire format for injected input
//   FrameGeometry      widget pixels <-> guest pixels
//   VirtualKeyMap      Qt keys -> virtual-key codes
//   NcHitDebugOverlay  the hit-test diagnostics
//
// Frames arrive as ``AgentRpcClient::frame_received`` from the agent's
// shared-memory pushes; gone-window notifications as ``window_gone``.
class WindowView : public QWidget {
    Q_OBJECT
public:
    // ``relative_to`` (optional) is the mirror of this window's guest
    // owner. When set, this view is made transient to it and placed at
    // ``rel_guest_px`` — the guest-pixel offset of this window's client
    // origin from the owner's — so popups land where the app put them.
    WindowView(link::AgentRpcClient& rpc, std::uint64_t hwnd, QString window_title,
               WindowView* relative_to = nullptr, QPoint rel_guest_px = {});
    ~WindowView() override;

    [[nodiscard]] std::uint64_t hwnd() const noexcept { return hwnd_; }

    // The guest window is verifiably gone — tear the mirror down for
    // real, bypassing the "just request close" path in closeEvent.
    void guest_closed();

    // Match the mirror to the guest window's show state, as the last
    // enumeration reported it.
    //
    // This is how a click on the application's own minimize or maximize
    // button reaches the host: the click was forwarded like any other,
    // the application acted on it, and the state it ended up in comes
    // back with the window list. It is also how a state change with no
    // click behind it arrives — the app's Window menu, Win+Down, the
    // guest's own taskbar.
    void apply_guest_state(bool minimized, bool maximized);

    // Overridden to stamp a per-window identity around native-window
    // creation.
    //
    // On Wayland, Qt reads ``QGuiApplication::desktopFileName()`` exactly
    // once at xdg_toplevel creation to set app_id, and it is immutable
    // afterwards. Toggling the process-wide name around the create-and-map
    // sequence is therefore the only way to give each mirror a distinct
    // identity, which is what stops KWin grouping every guest window under
    // the manager. On X11 the same value also goes into WM_CLASS.
    void setVisible(bool visible) override;

protected:
    void paintEvent(QPaintEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void changeEvent(QEvent* e) override;

    // Stop Tab / Shift-Tab being eaten for focus navigation so they
    // reach the guest like any other key.
    bool focusNextPrevChild(bool) override { return false; }

private Q_SLOTS:
    void on_frame(std::uint64_t hwnd, QImage image);
    void on_window_gone(std::uint64_t hwnd);
    void flush_move();

private:
    [[nodiscard]] bool is_popup() const { return !owner_view_.isNull(); }

    // Where the guest content is drawn, and how a widget point maps into
    // guest pixels. Thin wrappers that hand this view's size to the
    // presenter.
    [[nodiscard]] QRect frame_rect() const;
    [[nodiscard]] QPoint map_to_guest(QPointF widget_position) const;

    // Construction-time setup, split by window kind: a popup is placed
    // by its owner and sized by its frames, a main mirror probes the
    // guest for size and chrome first.
    void configure_as_popup(WindowView* owner, QPoint rel_guest_px);
    void configure_as_main_window();
    void apply_props(const GuestWindowProps& props);

    // A left-press that lands on caption chrome drives the HOST window,
    // not the guest — otherwise dragging the captured title bar moves
    // the guest window and minimise/maximise send it off-screen.
    // Returns true when the press was consumed.
    [[nodiscard]] bool steal_caption_press(QMouseEvent* e, QPoint guest);

    // Recompute and apply this popup's position from the owner's
    // current state.
    void reposition_popup();

    // GeometrySync decided the guest window should take this size.
    void send_resize(int width, int height);

    link::AgentRpcClient& rpc_;
    std::uint64_t hwnd_;
    GuestInputSender input_;

    // Where the pixels come from. A ``Presenter`` by design: the Vulkan
    // replay path plugs in here rather than beside it.
    ShmemPresenter presenter_;

    // The mirror shows the guest's client area 1:1, so resizing this
    // widget drives a resize_window RPC — debounced, echo-suppressed and
    // clamped to the guest's screen by GeometrySync.
    GeometrySync geometry_;
    MirrorShowGate show_gate_;
    bool first_frame_ = true;

    // Mouse moves fire far faster than is worth a frame each. The latest
    // position is stashed and flushed on a short timer — and eagerly
    // before any button/wheel/key event, so ordering is preserved.
    QTimer move_coalesce_;
    bool move_pending_ = false;
    QPoint move_position_;
    unsigned move_mods_ = 0;

    // Closing the mirror only REQUESTS that the guest window close; the
    // mirror stays until the guest window actually disappears. If the
    // app refuses to close, so does the mirror — which is the intent.
    bool guest_gone_ = false;

    // Presses rerouted to host window operations. The guest never saw
    // the down, so the matching up must be swallowed too or it is a
    // phantom release. Bit i = button i (0 left, 1 right, 2 middle).
    unsigned stolen_press_buttons_ = 0;

    // Token for the agent's ``caption_drag_started`` push. The host does
    // not hit-test caption areas up front; it forwards the press and the
    // agent tells us the instant DefWindowProc decides it was a drag,
    // cancelling the guest's move loop while we start a host one.
    std::uint64_t drag_event_token_ = 0;

    // The guest's show state as last applied or last sent, so a change
    // arriving from one side is never echoed back to the other.
    Qt::WindowStates guest_state_ = Qt::WindowNoState;

    // Minimized/maximized -> the single Qt state that stands for it.
    [[nodiscard]] static Qt::WindowStates show_state_of(bool minimized, bool maximized);
    NcHitDebugOverlay debug_overlay_;

    // Outermost px of the widget reserved as a resize halo, mirroring
    // Win11's invisible border. Zero for popups and non-resizable
    // windows, where the widget is exactly the frame.
    int resize_border_ = 0;

    // Owned-popup placement.
    QPointer<WindowView> owner_view_;
    QPoint pending_rel_guest_px_;
    bool has_pending_rel_ = false;
    std::optional<QPoint> popup_anchor_;

};

}  // namespace lsw::host::manager
