#pragma once

#include <QImage>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <cstdint>

namespace lsw::manager::vm {
class AgentRpcClient;
}

namespace lsw::manager::ui::manager {

// Top-level mirror window for one HWND inside the guest. Subscribes
// via the multiplexed AgentRpcClient channel (RPC: subscribe_window)
// on construction and unsubscribes on destruction. Frames arrive as
// ``AgentRpcClient::frame_received`` signals from the agent's
// ``"event": "frame"`` pushes; gone-window notifications arrive as
// ``window_gone``. No separate stream socket anymore.
class WindowView : public QWidget {
    Q_OBJECT
public:
    // ``relative_to`` (optional) is the mirror of this window's guest
    // owner; when set, this view is made transient to it and placed
    // at ``rel_guest_px`` (guest-pixel offset of this window's client
    // origin from the owner's) so popups land where the app put them.
    WindowView(vm::AgentRpcClient& rpc, std::uint64_t hwnd, QString window_title,
               WindowView* relative_to = nullptr, QPoint rel_guest_px = {});
    ~WindowView() override;

    [[nodiscard]] std::uint64_t hwnd() const noexcept { return hwnd_; }

    // The guest window is verifiably gone — tear the mirror down for
    // real (bypasses the "just request close" path in closeEvent).
    void guest_closed();

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
    // Stop Tab / Shift-Tab from being eaten for focus navigation so
    // it reaches the guest like any other key.
    bool focusNextPrevChild(bool) override { return false; }

private Q_SLOTS:
    void on_frame(std::uint64_t hwnd, QImage image);
    void on_window_gone(std::uint64_t hwnd);
    void flush_resize();
    void flush_move();

private:
    vm::AgentRpcClient& rpc_;
    std::uint64_t hwnd_;
    QImage image_;  // last frame

    // The mirror shows the guest's client area 1:1. Resizing this
    // widget drives a ``resize_window`` RPC so the guest window's
    // client area tracks the mirror. resizeEvent fires continuously
    // during a drag, so the RPC is debounced behind this timer; only
    // the final size is sent. ``sizing_to_frame_`` suppresses the
    // echo when we programmatically size the widget to the first
    // frame (that's the guest telling us its size, not the reverse).
    QTimer resize_debounce_;
    bool sizing_to_frame_ = false;
    bool first_frame_ = true;

    // Main mirrors don't show themselves until ``window_props`` has
    // been fetched and applied (size, min/max, resizable, min/max/
    // close buttons). Until then resize events are SUPPRESSED so the
    // initial Qt layout / default geometry can't echo a random size
    // back to the guest as a resize_window RPC. Popups skip this; they
    // already have a known size + their own placement protocol.
    bool props_ready_ = false;

    // Mouse-move coalescing: moves fire far faster than is worth a
    // wire frame each. The latest position is stashed and a single
    // ``inject_input`` move is flushed on this short timer (and
    // eagerly before any button/wheel/key event so ordering with
    // those is preserved).
    QTimer move_coalesce_;
    bool move_pending_ = false;
    int move_x_ = 0;
    int move_y_ = 0;
    unsigned move_mods_ = 0;

    // Mouse coordinates are mapped into guest pixels through the
    // aspect-fit rectangle the frame is actually drawn in (computed
    // on demand), so clicks are correct no matter how the frame is
    // scaled/letterboxed and regardless of the host DPR.

    // Closing the mirror only *requests* the guest window close
    // (posts WM_CLOSE) and keeps the mirror alive until the guest
    // window actually disappears. ``guest_gone_`` lets the real
    // teardown through closeEvent.
    bool guest_gone_ = false;

    // Left-button presses that landed on the guest window's NC area
    // (caption, min / max button, resize edge) are rerouted to host-
    // side window operations and NOT forwarded to the guest. The
    // matching release would otherwise be a phantom mouseup for the
    // guest (it never saw the down) — track which button presses we
    // stole so we can suppress their releases too. Bit ``i`` is set
    // when press for button ``i`` (0=left, 1=right, 2=middle) was
    // stolen. Only left presses are NC-hit-tested today.
    unsigned stolen_press_buttons_ = 0;

    // Event token for the agent's ``caption_drag_started`` push. The
    // host no longer hit-tests caption areas synchronously; instead
    // it forwards every non-button press to the guest, and the agent
    // (via SetWinEventHook on ``EVENT_SYSTEM_MOVESIZESTART``) notifies
    // us back the instant DefWindowProc decides a press was a
    // caption drag — the agent cancels the guest's move loop AND we
    // call ``startSystemMove`` on this mirror so the host window
    // moves instead.
    std::uint64_t drag_event_token_ = 0;

    // System caption-button rects in EFB-relative px, fetched once
    // from ``window_props`` (which gets them from the guest's
    // ``WM_GETTITLEBARINFOEX``). The press handler checks these LOCALLY
    // — no per-press RPC — because for modern Win11 apps the wndproc
    // returns HTCAPTION for the whole title bar (DWM owns the system
    // buttons at the frame layer). An empty rect (width or height == 0)
    // means the button isn't present. ``WM_NCHITTEST`` is still used
    // for caption-drag and resize-edge detection below.
    // System caption-button rects in EFB-relative px from UIA. The
    // press handler's local check fires BEFORE any RPC: if the press
    // lands in one of these, we route to the host op (min/max/close)
    // and skip the nchittest round-trip entirely. Caption-drag for
    // the rest of the title bar is decided by a per-press
    // ``nchittest`` RPC — UIA can't discriminate dead caption strip
    // from tabs / address bars / toolbars sharing the caption row,
    // but the OS's WM_NCHITTEST returns HTCAPTION only for the real
    // draggable area.
    QRect btn_min_rect_;
    QRect btn_max_rect_;
    QRect btn_close_rect_;
    QRect btn_help_rect_;

    // For resizable main mirrors, the outermost ``resize_border_`` px
    // of the widget is reserved as a resize halo — same role as Win11's
    // invisible border around real windows. The captured frame is
    // centred inside with that margin (compute_frame_rect aspect-fits)
    // and the halo paints as the widget's black background. Zero for
    // non-resizable windows and popups (no halo, widget = EFB exactly).
    int resize_border_ = 0;

    // Guest's primary screen resolution. The host widget can grow
    // beyond this (user drags the mirror larger than the guest's
    // desktop), but flush_resize never tells the guest to be bigger
    // than its own screen — the captured frame letterboxes inside the
    // larger widget instead. Zero = no clamp yet (window_props hasn't
    // returned).
    int screen_w_ = 0;
    int screen_h_ = 0;

    // Width of the captured EFB the last time we updated the cached
    // ``btn_*_rect_`` values. System caption buttons are top-right
    // anchored: when the guest window grows wider, the buttons shift
    // right by the same delta in EFB px. Each frame we apply the
    // width delta to the rects so clicks stay aligned with the
    // visible buttons across resizes. Zero until window_props has
    // returned with the original rects.
    int last_image_w_for_btns_ = 0;

    // Debug overlay (``LSW_DEBUG_NCHIT=1``): the last point the user
    // pressed (in widget coords) and the diagnostic fields the agent
    // reports back alongside the ``WM_NCHITTEST`` result. Painted on
    // top of the frame so coordinate-space drift between WGC capture
    // and NCHITTEST is attributable in a screenshot — e.g. ``efb``
    // vs ``win`` differing by >0 reveals invisible Win11 borders, a
    // ``dpi`` != 96 with a still-CAPTION hit on a visible button
    // points at SendMessage DPI virtualisation. ``last_ht_`` is < 0
    // (HTERROR) until the first NC hit-test happens.
    int last_ht_ = -1;
    QPointF last_hit_widget_;
    QPoint last_hit_guest_;
    QRect last_efb_;
    QRect last_win_;
    QPoint last_screen_;
    unsigned last_dpi_ = 0;

    // Deferred relative placement for owned popups (applied once the
    // owner mirror has a real position / this view is shown).
    QPointer<WindowView> owner_view_;
    QPoint pending_rel_guest_px_;
    bool has_pending_rel_ = false;
    QPoint popup_anchor_;     // base global pos (owner content + rel)
    bool anchor_set_ = false;  // popup_anchor_ has been computed

    // For popups only: the opaque sub-rectangle of the captured frame
    // (the menu itself), with the WinUI shadow's transparent margin
    // trimmed off. The popup window is sized to this and only this
    // region is drawn, so the menu fills it and the single outer
    // shadow is the compositor's — not a captured-shadow-inside-
    // another-shadow nest. Empty = use the whole frame (main windows).
    QRect content_rect_;

    [[nodiscard]] bool is_popup() const { return !owner_view_.isNull(); }
    // Opaque-content bounding box of ``img`` (alpha-trimmed). Falls
    // back to the full image if the result would be degenerate.
    [[nodiscard]] static QRect opaque_bounds(const QImage& img);

    // Recompute the rect the frame is drawn in for the current widget
    // + (content-trimmed) image size; aspect-fit, centered.
    [[nodiscard]] QRect compute_frame_rect() const;
    // Map a widget-local point to guest client pixels.
    [[nodiscard]] QPoint map_to_guest(QPointF widget_pos) const;
};

}  // namespace lsw::manager::ui::manager
