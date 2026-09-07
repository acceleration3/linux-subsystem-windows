#include "lsw/host/manager/window_view.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/manager/frame_geometry.hpp"
#include "lsw/host/manager/frame_tap.hpp"
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
#include <QSet>
#include <QShowEvent>
#include <QWheelEvent>
#include <QWindow>
#include <QApplication>
#include <QtConcurrent>

#include <algorithm>

#include <nlohmann/json.hpp>

namespace lsw::host::manager {

namespace {
// Guest windows with a mirror on screen right now. See
// ``WindowView::is_mirrored``.
QSet<std::uint64_t>& mirrored_hwnds() {
    static QSet<std::uint64_t> live;
    return live;
}
}  // namespace

bool WindowView::is_mirrored(std::uint64_t hwnd) {
    return mirrored_hwnds().contains(hwnd);
}

WindowView::WindowView(link::AgentRpcClient& rpc, std::uint64_t hwnd,
                       const QString& window_title, WindowView* relative_to,
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

    mirrored_hwnds().insert(hwnd_);
    FrameTap::instance().attach(hwnd_, this);
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
    mirrored_hwnds().remove(hwnd_);
    FrameTap::instance().detach(hwnd_);
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
    // Translucent, like a popup. Not because a main window is usually
    // see-through, but because some are in part: a layered splash
    // screen is shaped entirely by its alpha, and every Win11 window
    // has four transparent corners. An opaque widget with a black
    // background turned all of that black — which is why Photoshop's
    // splash was black on the launch that made it a main mirror, and
    // correct on the launch that happened to make it a popup.
    setAttribute(Qt::WA_TranslucentBackground);
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

    // The opening size is the frame's. Which PART of the frame is shown
    // is not asked here — it arrives with each frame, measured for that
    // frame — so a window that opens maximized is briefly sized to its
    // overhang too, and the first frame settles it.
    const QSize shown(props.width, props.height);
    if (!props.resizable) {
        setFixedSize(shown);
    } else {
        if (props.min_width > 0 && props.min_height > 0) {
            setMinimumSize(props.min_width, props.min_height);
        }
        // Deliberately no maximum: the user may grow the mirror past the
        // guest's max track size. flush_resize clamps what the guest is
        // told, and the frame letterboxes inside the larger widget.
        geometry_.expect_echo();
        resize(shown);
    }

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

    // Nothing else is intercepted. A press on what looks like a
    // caption button is forwarded like any other: the button belongs to
    // the application, which draws it and knows where it is, and the
    // host learns what came of the click from the window's show state
    // on the next enumeration. Trying to recognise the press here meant
    // knowing the button rects, and nothing reports those reliably —
    // UIA lies about custom chrome, and an application that hit-tests
    // its own title bar answers HTCLIENT over every button it drew.
    //
    // If Windows decides the press is a caption drag, the agent's
    // MOVESIZESTART hook tells us and the constructor's handler starts
    // a host system move.
    return false;
}

void WindowView::mousePressEvent(QMouseEvent* e) {
    if (!is_popup()) {
        // Never for a popup. Taking host focus for a menu's mirror
        // deactivates the mirror it belongs to, and what the guest makes
        // of that is a closed menu — see changeEvent. The menu already
        // has the guest's focus; it needs the click, not the focus.
        setFocus(Qt::MouseFocusReason);
    }
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

void WindowView::guest_closed(const char* reason) {
    // Logged because this is the only way a mirror goes away against
    // the user's wishes, and the three callers are indistinguishable
    // afterwards. A mirror that vanished while its application was
    // still on screen — mid save-prompt, say — is a bug in whichever
    // one of them fired, and this line is what names it.
    LSW_INFO("mirror {:#x}: closing ({})", hwnd_, reason);
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
        guest_closed("the agent reported the guest window gone");
    }
}

// ---- frames ----------------------------------------------------------------

void WindowView::on_frame(std::uint64_t hwnd, QImage image, QRect visible) {
    if (hwnd != hwnd_) {
        return;
    }
    // Popups trim the WinUI shadow margin so the menu fills the window;
    // classic Win32 menus have no such margin and keep the whole frame.
    // Everything else takes the sub-rect the guest measured for THIS
    // frame and sent along with it.
    if (!is_popup()) {
        presenter_.set_content_rect(visible.size() == image.size() ? QRect() : visible);
    }
    presenter_.submit(std::move(image), /*trim_shadow=*/is_popup());

    if (is_popup() && presenter_.has_content()) {
        // Always size and anchor: gating on the trim changing skipped
        // sizing for classic menus and mis-placed them.
        reposition_popup();
        first_frame_ = false;
    } else if (first_frame_ && presenter_.has_content()) {
        // Main mirror: open showing exactly what the guest shows —
        // ``natural_size`` is the frame minus any overhang the guest
        // told us about.
        first_frame_ = false;
        geometry_.expect_echo();
        resize(presenter_.natural_size());
    }
    // Snapshot for ``dump_frame``: the frame as it arrived, next to the
    // geometry this mirror is about to draw it with.
    FrameTap::instance().record(
        hwnd_, {presenter_.image(), windowTitle(), size(), frame_rect(),
                presenter_.content_rect(), is_popup(),
                is_popup() ? owner_view_->hwnd_ : std::uint64_t{0}});
    update();
}

void WindowView::reposition_popup() {
    if (owner_view_.isNull()) {
        return;
    }
    // Recompute from the owner's CURRENT state on every frame, not just
    // at show. A popup detected in the same tick as its owner (Notepad
    // pops a Find dialog right after its main window) is shown before
    // the owner has a frame or a configured surface, so the anchor
    // computed then is wrong. By the time the popup's own first frame
    // arrives the owner has settled; Wayland's xdg_popup.reposition
    // honours the later move(), so the user sees the corrected spot.
    const QRect owner_frame = owner_view_->frame_rect();
    const QRect owner_source = owner_view_->presenter_.source_rect();
    const double scale = PopupPlacement::scale(owner_frame, owner_source);

    // Drawn at the owner's scale. A mirror is only 1:1 until the user
    // drags its edge; a menu left at native size beside a mirror scaled
    // to 1.3 is the wrong size, and its offset from the owner would be
    // measured in different units than its own.
    if (presenter_.has_content()) {
        if (const QSize want =
                PopupPlacement::scaled_size(presenter_.natural_size(), scale);
            want != size()) {
            resize(want);
        }
    }

    // What this widget's top-left corner IS, in guest pixels of the
    // owner's frame: where the popup's frame sits relative to the
    // owner's (``pending_rel_guest_px_``), plus however far into that
    // frame its visible content starts (the shadow margin the presenter
    // trimmed). One point, mapped once, through the owner's frame — so
    // the letterbox offset, the scale and the owner's own sub-rect are
    // all applied exactly once each.
    const QPoint corner = pending_rel_guest_px_ + presenter_.source_rect().topLeft();
    popup_anchor_ = owner_view_->mapToGlobal(
        PopupPlacement::to_widget(corner, owner_frame, owner_source));
    move(*popup_anchor_);
}

void WindowView::paintEvent(QPaintEvent*) {
    QPainter painter{this};
    presenter_.paint(painter, size(), /*letterbox=*/!is_popup());

    if (presenter_.has_content() && NcHitDebugOverlay::enabled() && !is_popup()) {
        debug_overlay_.paint(painter, frame_rect(), presenter_.image(),
                             width(), height(), devicePixelRatioF());
    }
}

// ---- host window state -----------------------------------------------------

Qt::WindowStates WindowView::show_state_of(bool minimized, bool maximized) {
    if (minimized) {
        return Qt::WindowMinimized;
    }
    return maximized ? Qt::WindowMaximized : Qt::WindowNoState;
}

void WindowView::apply_guest_state(bool minimized, bool maximized) {
    if (is_popup() || guest_gone_) {
        return;
    }
    const Qt::WindowStates want = show_state_of(minimized, maximized);
    guest_state_ = want;
    if (want == show_state_of((windowState() & Qt::WindowMinimized) != 0,
                              (windowState() & Qt::WindowMaximized) != 0)) {
        return;
    }
    if (minimized) {
        showMinimized();
    } else if (maximized) {
        showMaximized();
    } else {
        showNormal();
    }
    // ``guest_state_`` is what stops the resulting WindowStateChange
    // from being sent straight back as a command. Deliberately a
    // remembered state rather than a flag held across the call: the
    // event arrives whenever the host's window manager gets to it, which
    // under Wayland is not within this function.
}

void WindowView::changeEvent(QEvent* e) {
    QWidget::changeEvent(e);

    if (e->type() == QEvent::ActivationChange && !is_popup() && !guest_gone_) {
        // Mirror host activation onto guest foreground state. Skipped
        // during teardown: closing posts WM_CLOSE and Windows picks the
        // next foreground itself.
        const std::uint64_t hwnd = hwnd_;
        if (isActiveWindow()) {
            (void)QtConcurrent::run([this, hwnd] {
                (void)rpc_.call(nlohmann::json{{"cmd", linkproto::cmd::kFocusWindow},
                                               {"params", {{"hwnd", hwnd}}}});
            });
        } else {
            // Losing activation is not the same as the guest losing
            // focus, and answering it immediately breaks menus.
            //
            // Clicking a menu's mirror deactivates the main mirror it
            // belongs to. Unfocusing the guest window there hands the
            // guest's foreground to the shell — and a menu belongs to
            // its application's foreground thread, so Windows closes it
            // on the spot. The click we were forwarding then arrives at
            // whatever the menu had been covering. That is the whole
            // bug: the item was never clicked, its menu was dismissed
            // out from under it.
            //
            // So decide once the host has settled on a new active
            // window, and only unfocus when the guest really did lose
            // it — i.e. when focus went somewhere that is not another
            // mirror. A mirror that takes it announces itself with its
            // own focus_window.
            QTimer::singleShot(0, this, [this, hwnd] {
                if (guest_gone_ || isActiveWindow()) {
                    return;
                }
                if (qobject_cast<WindowView*>(QApplication::activeWindow()) != nullptr) {
                    return;  // another mirror has it; the guest keeps focus
                }
                (void)QtConcurrent::run([this, hwnd] {
                    (void)rpc_.call(nlohmann::json{{"cmd", linkproto::cmd::kUnfocusWindow},
                                                   {"params", {{"hwnd", hwnd}}}});
                });
            });
        }
    }

    if (e->type() == QEvent::WindowStateChange && !is_popup() && !guest_gone_) {
        // The user did this to the mirror — the host's window menu, its
        // taskbar, a keyboard shortcut. Pass it on as a WM_SYSCOMMAND,
        // which reaches the guest window's own procedure whoever painted
        // its chrome, and needs to know nothing about where its buttons
        // are.
        //
        // Unless the guest is already in this state, which means this
        // event IS the guest's own change arriving back through
        // apply_guest_state. Telling it to do what it just did would
        // bounce between the two forever.
        const Qt::WindowStates now =
            show_state_of((windowState() & Qt::WindowMinimized) != 0,
                          (windowState() & Qt::WindowMaximized) != 0);
        if (now != guest_state_) {
            guest_state_ = now;
            const char* command = now == Qt::WindowMinimized  ? "minimize"
                                  : now == Qt::WindowMaximized ? "maximize"
                                                               : "restore";
            const std::uint64_t hwnd = hwnd_;
            (void)QtConcurrent::run([this, hwnd, command] {
                (void)rpc_.call(nlohmann::json{{"cmd", linkproto::cmd::kWindowCommand},
                                               {"params", {{"hwnd", hwnd},
                                                           {"command", command}}}});
            });
        }
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
            popup->guest_closed("its owner was minimized");
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

    // Place it from what the owner knows right now. The trim and the
    // owner's settled geometry only arrive with this popup's first
    // frame; reposition_popup runs again there and corrects it.
    reposition_popup();
}

}  // namespace lsw::host::manager
