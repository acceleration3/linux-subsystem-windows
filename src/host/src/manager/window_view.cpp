#include "lsw/host/manager/window_view.hpp"

#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/manager/frame_geometry.hpp"
#include "lsw/host/manager/guest_window_icon_fetcher.hpp"
#include "lsw/host/manager/guest_window_props_fetcher.hpp"
#include "lsw/host/manager/input_modifiers.hpp"
#include "lsw/host/manager/non_client_hit.hpp"
#include "lsw/host/manager/popup_placement.hpp"
#include "lsw/host/manager/resize_edge_zones.hpp"
#include "lsw/host/manager/taskbar_entry.hpp"
#include "lsw/host/manager/virtual_key_map.hpp"
#include "lsw/host/manager/window_class_stamper.hpp"

#include "lsw/linkproto/commands.hpp"

#include <QGuiApplication>
#include <QIcon>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>
#include <QWindow>
#include <QtConcurrent>

#include <algorithm>

#include <nlohmann/json.hpp>

namespace lsw::host::manager {

WindowView::WindowView(link::AgentRpcClient& rpc, std::uint64_t hwnd,
                       QString window_title, WindowView* relative_to,
                       QPoint rel_guest_px)
    : QWidget(nullptr), rpc_(rpc), hwnd_(hwnd), input_(rpc, hwnd),
      geometry_([this](int w, int h) { send_resize(w, h); }, this),
      show_gate_(hwnd, [this] { show(); }), owner_view_(relative_to) {
    setWindowTitle(window_title);
    setAttribute(Qt::WA_DeleteOnClose);

    // Receive keyboard focus and continuous mouse-move (not only while a
    // button is held) so the guest sees hover as well as drags.
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    move_coalesce_.setSingleShot(true);
    move_coalesce_.setInterval(8);  // ~120 Hz cap on forwarded moves
    connect(&move_coalesce_, &QTimer::timeout, this, &WindowView::flush_move);

    connect(&rpc_, &link::AgentRpcClient::frame_received, this, &WindowView::on_frame);
    connect(&rpc_, &link::AgentRpcClient::window_gone, this, &WindowView::on_window_gone);
    rpc_.subscribe_window(hwnd_);

    // Caption-drag detector. The agent fires ``caption_drag_started`` the
    // instant DefWindowProc enters its SC_MOVE modal loop on the guest,
    // cancelling that loop in the same breath so the guest never visibly
    // moves. Windows decided this press was a drag; the host carries it out.
    drag_event_token_ = rpc_.on_event(QString::fromLatin1(linkproto::topic::kCaptionDragStarted),
                                      [this](const nlohmann::json& frame) {
        if (frame.value("hwnd", std::uint64_t{0}) != hwnd_) {
            return;
        }
        QMetaObject::invokeMethod(this, [this] {
            if (QWindow* handle = windowHandle()) {
                handle->startSystemMove();
            }
            // Suppress the matching release: after the agent's
            // WM_CANCELMODE the guest would see an orphan button-up.
            stolen_press_buttons_ |= 1u;  // bit 0 = left button
        }, Qt::QueuedConnection);
    });

    if (relative_to != nullptr) {
        configure_as_popup(relative_to, rel_guest_px);
    } else {
        configure_as_main_window();
    }
}

WindowView::~WindowView() {
    if (drag_event_token_ != 0) {
        rpc_.off_event(drag_event_token_);
        drag_event_token_ = 0;
    }
    rpc_.unsubscribe_window(hwnd_);
    // Drop the per-window .desktop here rather than in guest_closed():
    // on the host-driven "user clicked X" path the mirror lives on until
    // the guest acknowledges WM_CLOSE.
    TaskbarEntry::unregister_window(hwnd_);
}

void WindowView::configure_as_popup(WindowView* owner, QPoint rel_guest_px) {
    // Menus, combo drop-downs, flyouts, message boxes. A frameless
    // tooltip-type window transient to the owner: on Wayland Qt maps
    // that to an xdg_popup, which the compositor positions RELATIVE to
    // the parent surface — a plain toplevel cannot be positioned by the
    // client at all. Show-without-activating so opening a menu does not
    // yank host focus off the main mirror. Translucent because the
    // captured shadow margin is trimmed away, leaving transparent area
    // that must not paint as a black rectangle.
    setParent(owner, Qt::ToolTip | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setMinimumSize(1, 1);  // menus are small; do not force 320x240
    pending_rel_guest_px_ = rel_guest_px;
    has_pending_rel_ = true;
    // No props phase for a popup: size and placement are frame-driven,
    // and it shows as soon as it is placed.
    geometry_.mark_ready();
    show_gate_.bypass();
}

void WindowView::configure_as_main_window() {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setStyleSheet("background-color: black;");
    // Minimum comes later from the guest's own constraints; forcing
    // 320x240 here would inflate a smaller guest window.
    setMinimumSize(1, 1);

    auto* props = new GuestWindowPropsFetcher(rpc_, hwnd_, this);
    connect(props, &GuestWindowPropsFetcher::ready, this,
            [this](const GuestWindowProps& p) {
        apply_props(p);
        geometry_.mark_ready();
        show_gate_.props_arrived(windowTitle());
    });
    props->start();

    auto* icon = new GuestWindowIconFetcher(rpc_, hwnd_, this);
    connect(icon, &GuestWindowIconFetcher::ready, this, [this](const QByteArray& png) {
        // setWindowIcon is the X11 path (it writes _NET_WM_ICON). On
        // Wayland it is a no-op: there the taskbar resolves the icon by
        // mapping app_id to the per-window .desktop the show gate writes.
        if (!png.isEmpty()) {
            QPixmap pixmap;
            if (pixmap.loadFromData(png, "PNG")) {
                setWindowIcon(QIcon(pixmap));
            }
        }
        show_gate_.icon_arrived(windowTitle(), png);
    });
    icon->start();
}

void WindowView::apply_props(const GuestWindowProps& props) {
    if (!props.valid) {
        return;
    }
    // Frameless: the captured guest chrome is the only chrome, so there
    // is no double title bar. Caption-drag and edge-resize are handled
    // here instead. Qt::Window keeps it a toplevel, so it still gets a
    // taskbar entry and an Alt-Tab slot.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    resize_border_ = props.resizable ? ResizeEdgeZones::kBorderPx : 0;

    if (!props.resizable) {
        setFixedSize(props.width, props.height);
    } else {
        if (props.min_width > 0 && props.min_height > 0) {
            setMinimumSize(props.min_width, props.min_height);
        }
        // Deliberately no maximum: the user may grow the mirror past the
        // guest's max track size. flush_resize clamps what the guest is
        // told, and the frame letterboxes inside the larger widget.
        geometry_.expect_echo();
        resize(props.width + 2 * resize_border_, props.height + 2 * resize_border_);
    }

    buttons_.adopt(props.button_minimize, props.button_maximize, props.button_close,
                   props.button_help, props.width);
    geometry_.set_guest_screen(props.screen_width, props.screen_height);
}

// ---- geometry --------------------------------------------------------------

QRect WindowView::frame_rect() const {
    if (!presenter_.has_content() || width() <= 0 || height() <= 0) {
        return rect();
    }
    return presenter_.frame_rect(size());
}

QPoint WindowView::map_to_guest(QPointF widget_position) const {
    return presenter_.map_to_guest(widget_position, size());
}

// ---- resize ----------------------------------------------------------------

void WindowView::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (is_popup()) {
        // A menu's size is the guest's; echoing the widget size back
        // shrank it a little on every layout pass.
        return;
    }
    // GeometrySync decides whether this resize is real (ours to forward),
    // our own echo of a guest-driven size, or too early to act on.
    (void)geometry_.widget_resized(size());
}

void WindowView::send_resize(int width, int height) {
    // Make the guest window match the widget so the mirror is ~1:1. The
    // clamp to the guest's own screen already happened in GeometrySync.
    const std::uint64_t hwnd = hwnd_;
    (void)QtConcurrent::run([this, hwnd, width, height] {
        (void)rpc_.call(nlohmann::json{
            {"cmd", linkproto::cmd::kResizeWindow},
            {"params", {{"hwnd", hwnd}, {"width", width}, {"height", height}}}});
    });
}

// ---- input -----------------------------------------------------------------

void WindowView::flush_move() {
    if (!move_pending_) {
        return;
    }
    move_pending_ = false;
    input_.move(move_position_, move_mods_);
}

void WindowView::mouseMoveEvent(QMouseEvent* e) {
    // Resize-halo hover cue. Popups have a zero border, so this is a
    // no-op there.
    const Qt::Edges edges =
        ResizeEdgeZones::at(e->position(), width(), height(), resize_border_);
    if (edges != 0) {
        setCursor(ResizeEdgeZones::cursor_for(edges));
    } else if (testAttribute(Qt::WA_SetCursor)) {
        unsetCursor();
    }
    move_position_ = map_to_guest(e->position());
    move_mods_ = InputModifiers::of(e->modifiers(), e->buttons());
    move_pending_ = true;
    if (!move_coalesce_.isActive()) {
        move_coalesce_.start();
    }
    e->accept();
}

bool WindowView::steal_caption_press(QMouseEvent* e, QPoint guest) {
    QWindow* handle = windowHandle();

    // The halo wins outright: a press in the border zone must always
    // start a system resize, even where map_to_guest would clamp into a
    // button rect's area.
    if (const Qt::Edges edges =
            ResizeEdgeZones::at(e->position(), width(), height(), resize_border_);
        edges != 0) {
        if (handle != nullptr && handle->startSystemResize(edges)) {
            debug_overlay_.record(e->position(), guest, NonClientHit::kBottomRight);
            if (NcHitDebugOverlay::enabled()) {
                update();
            }
            return true;
        }
    }

    // System caption buttons, checked locally against the UIA-sourced
    // rects — no round trip per press.
    int synthetic_hit = -1;
    switch (buttons_.hit(guest)) {
    case CaptionButtonRects::Button::Minimize:
        showMinimized();
        synthetic_hit = NonClientHit::kMinButton;
        break;
    case CaptionButtonRects::Button::Maximize:
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
        synthetic_hit = NonClientHit::kMaxButton;
        break;
    case CaptionButtonRects::Button::Close:
        // Through the normal close path, so the app still gets to
        // prompt for unsaved changes.
        close();
        synthetic_hit = NonClientHit::kClose;
        break;
    case CaptionButtonRects::Button::Help:
    case CaptionButtonRects::Button::None:
        break;
    }
    if (synthetic_hit < 0) {
        // Everything else goes to the guest. If Windows decides the
        // press is a caption drag, the agent's MOVESIZESTART hook tells
        // us and the constructor's handler starts a host system move.
        return false;
    }
    debug_overlay_.record(e->position(), guest, synthetic_hit);
    if (NcHitDebugOverlay::enabled()) {
        update();
    }
    return true;
}

void WindowView::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    flush_move();  // ordering: any pending move lands before the click
    const QPoint guest = map_to_guest(e->position());

    if (!is_popup() && e->button() == Qt::LeftButton && steal_caption_press(e, guest)) {
        stolen_press_buttons_ |= (1u << InputModifiers::button_index(e->button()));
        e->accept();
        return;
    }
    input_.button_down(guest, InputModifiers::button_index(e->button()),
                       InputModifiers::of(e->modifiers(), e->buttons()));
    e->accept();
}

void WindowView::mouseReleaseEvent(QMouseEvent* e) {
    const unsigned bit = 1u << InputModifiers::button_index(e->button());
    if ((stolen_press_buttons_ & bit) != 0) {
        // The matching press became a host window operation, so the
        // guest never saw the down; forwarding the up would be a
        // phantom release.
        stolen_press_buttons_ &= ~bit;
        e->accept();
        return;
    }
    flush_move();
    input_.button_up(map_to_guest(e->position()),
                     InputModifiers::button_index(e->button()),
                     InputModifiers::of(e->modifiers(), e->buttons()));
    e->accept();
}

void WindowView::mouseDoubleClickEvent(QMouseEvent* e) {
    flush_move();
    // Qt delivers press, release, then this in place of the second
    // press — which is exactly what WM_*BUTTONDBLCLK is.
    const QPoint guest = map_to_guest(e->position());
    if (!is_popup() && e->button() == Qt::LeftButton
        && buttons_.hit(guest) != CaptionButtonRects::Button::None) {
        // The single-press path already ran the button's action; swallow
        // the double so the guest does not see a phantom double-click.
        stolen_press_buttons_ |= (1u << InputModifiers::button_index(e->button()));
        e->accept();
        return;
    }
    input_.double_click(guest, InputModifiers::button_index(e->button()),
                        InputModifiers::of(e->modifiers(), e->buttons()));
    e->accept();
}

void WindowView::wheelEvent(QWheelEvent* e) {
    flush_move();
    const QPoint guest = map_to_guest(e->position());
    const QPoint delta = e->angleDelta();
    const unsigned mods = InputModifiers::of(e->modifiers(), e->buttons());
    if (delta.y() != 0) {
        input_.wheel(guest, delta.y(), /*horizontal=*/false, mods);
    }
    if (delta.x() != 0) {
        input_.wheel(guest, delta.x(), /*horizontal=*/true, mods);
    }
    e->accept();
}

void WindowView::keyPressEvent(QKeyEvent* e) {
    const unsigned mods = InputModifiers::of(e->modifiers());
    if (VirtualKeyMap::is_text_input(e)) {
        // Text is WM_CHAR only. Sending a key-down as well would make
        // the guest's TranslateMessage synthesise a second WM_CHAR and
        // every character would arrive twice.
        for (const QChar ch : e->text()) {
            if (ch.unicode() != 0) {
                input_.text(ch.unicode(), mods);
            }
        }
    } else {
        // Navigation, shortcuts, control keys: the guest's own
        // TranslateMessage produces the right control character, so we
        // must not send one too.
        input_.key_down(VirtualKeyMap::virtual_key_for(e->key()), mods);
    }
    e->accept();
}

void WindowView::keyReleaseEvent(QKeyEvent* e) {
    // Text keys sent no key-down, so they get no key-up either.
    if (!VirtualKeyMap::is_text_input(e)) {
        input_.key_up(VirtualKeyMap::virtual_key_for(e->key()),
                      InputModifiers::of(e->modifiers()));
    }
    e->accept();
}

// ---- lifecycle -------------------------------------------------------------

void WindowView::guest_closed() {
    guest_gone_ = true;
    close();
}

void WindowView::closeEvent(QCloseEvent* e) {
    if (guest_gone_) {
        rpc_.unsubscribe_window(hwnd_);
        QWidget::closeEvent(e);
        return;
    }
    // The user clicked the mirror's X. Only ASK the guest window to
    // close; the mirror stays up. If the app raises a save-changes
    // prompt that prompt becomes its own mirror. The mirror goes away
    // once the guest window actually does — so an app that refuses to
    // close keeps its mirror, which is the intent.
    e->ignore();
    const std::uint64_t hwnd = hwnd_;
    (void)QtConcurrent::run([this, hwnd] {
        (void)rpc_.call(nlohmann::json{{"cmd", linkproto::cmd::kCloseWindow},
                                       {"params", {{"hwnd", hwnd}}}});
    });
}

void WindowView::on_window_gone(std::uint64_t hwnd) {
    if (hwnd == hwnd_) {
        guest_closed();
    }
}

// ---- frames ----------------------------------------------------------------

void WindowView::on_frame(std::uint64_t hwnd, QImage image) {
    if (hwnd != hwnd_) {
        return;
    }
    // Popups trim the WinUI shadow margin so the menu fills the window;
    // classic Win32 menus have no such margin and keep the whole frame.
    presenter_.submit(std::move(image), /*trim_shadow=*/is_popup());

    if (!is_popup()) {
        buttons_.track_frame_width(presenter_.source_size().width());
    }

    if (is_popup() && presenter_.has_content()) {
        // Always size and anchor: gating on the trim changing skipped
        // sizing for classic menus and mis-placed them.
        if (const QSize want = presenter_.natural_size(); want != size()) {
            geometry_.expect_echo();
            resize(want);
        }
        reposition_popup();
        first_frame_ = false;
    } else if (first_frame_ && presenter_.has_content()) {
        // Main mirror: open at the guest's client size.
        first_frame_ = false;
        geometry_.expect_echo();
        resize(presenter_.source_size());
    }
    update();
}

void WindowView::reposition_popup() {
    if (!popup_anchor_.has_value() || owner_view_.isNull()) {
        return;
    }
    // Recompute from the owner's CURRENT state on every frame, not just
    // at show. A popup detected in the same tick as its owner (Notepad
    // pops a Find dialog right after its main window) is shown before
    // the owner has a frame or a configured surface, so the anchor
    // computed then is wrong. By the time the popup's own first frame
    // arrives the owner has settled; Wayland's xdg_popup.reposition
    // honours the later move(), so the user sees the corrected spot.
    popup_anchor_ = PopupPlacement::anchor(owner_view_->mapToGlobal(QPoint(0, 0)),
                                           owner_view_->frame_rect(),
                                           owner_view_->presenter_.source_size(),
                                           pending_rel_guest_px_);
    move(*popup_anchor_ + presenter_.content_rect().topLeft());
}

void WindowView::paintEvent(QPaintEvent*) {
    QPainter painter{this};
    presenter_.paint(painter, size(), /*translucent=*/is_popup());

    if (presenter_.has_content() && NcHitDebugOverlay::enabled() && !is_popup()) {
        debug_overlay_.paint(painter, buttons_, frame_rect(), presenter_.image(),
                             width(), height(), devicePixelRatioF());
    }
}

// ---- host window state -----------------------------------------------------

void WindowView::changeEvent(QEvent* e) {
    QWidget::changeEvent(e);

    if (e->type() == QEvent::ActivationChange && !is_popup() && !guest_gone_) {
        // Mirror host activation onto guest foreground state. Skipped
        // during teardown: closing posts WM_CLOSE and Windows picks the
        // next foreground itself.
        const std::uint64_t hwnd = hwnd_;
        const bool active = isActiveWindow();
        (void)QtConcurrent::run([this, hwnd, active] {
            (void)rpc_.call(nlohmann::json{
                {"cmd", active ? linkproto::cmd::kFocusWindow : linkproto::cmd::kUnfocusWindow},
                {"params", {{"hwnd", hwnd}}}});
        });
    }

    if (e->type() == QEvent::WindowStateChange && !is_popup()
        && (windowState() & Qt::WindowMinimized)) {
        // Minimising dismisses the window's menus: ask each guest popup
        // to close and tear its mirror down, so no orphaned floating
        // menus are left behind the icon.
        const auto popups =
            findChildren<WindowView*>(QString{}, Qt::FindDirectChildrenOnly);
        for (WindowView* popup : popups) {
            if (popup == nullptr) {
                continue;
            }
            const std::uint64_t popup_hwnd = popup->hwnd_;
            (void)QtConcurrent::run([this, popup_hwnd] {
                (void)rpc_.call(nlohmann::json{{"cmd", linkproto::cmd::kCloseWindow},
                                               {"params", {{"hwnd", popup_hwnd}}}});
            });
            popup->guest_closed();
        }
    }
}

void WindowView::setVisible(bool visible) {
    // Only toggle the global identity around the FIRST show, when no
    // native window exists yet. Later show/hide cycles reuse the same
    // toplevel, whose app_id is already locked in.
    if (visible && windowHandle() == nullptr) {
        const QString unique = QStringLiteral("lsw-window-%1").arg(hwnd_);
        const QString previous = QGuiApplication::desktopFileName();
        QGuiApplication::setDesktopFileName(unique);
        QWidget::setVisible(true);
        QGuiApplication::setDesktopFileName(previous);
        // X11 can take WM_CLASS after creation; no-op on Wayland.
        WindowClassStamper::apply(windowHandle(), unique);
        return;
    }
    QWidget::setVisible(visible);
}

void WindowView::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (!has_pending_rel_ || owner_view_.isNull()) {
        return;
    }
    has_pending_rel_ = false;

    // Force both native windows to exist so the transient-parent link is
    // set BEFORE the surface maps — otherwise Qt's Wayland backend makes
    // a plain toplevel, which the compositor places arbitrarily, instead
    // of an xdg_popup.
    owner_view_->createWinId();
    createWinId();
    if (QWindow* owner_window = owner_view_->windowHandle()) {
        if (QWindow* self = windowHandle()) {
            self->setTransientParent(owner_window);
        }
    }

    // Anchor at where the FULL captured frame's origin would sit; the
    // visible menu is inset by content_rect_.topLeft() within it, which
    // reposition_popup applies once the trim is known.
    popup_anchor_ = PopupPlacement::anchor(owner_view_->mapToGlobal(QPoint(0, 0)),
                                           owner_view_->frame_rect(),
                                           owner_view_->presenter_.source_size(),
                                           pending_rel_guest_px_);
    move(*popup_anchor_ + presenter_.content_rect().topLeft());
}

}  // namespace lsw::host::manager
